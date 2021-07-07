#include "pch.h"
#include "XivAlex.h"
#include "Utils_CallOnDestruction.h"
#include "Utils_Win32.h"
#include "Utils_Win32_Closeable.h"
#include "Utils_Win32_Handle.h"

#include <cryptopp/base64.h>
#include <cryptopp/blowfish.h>
#include <cryptopp/modes.h>

const std::string XivAlex::SqexChecksumTable = "fX1pGtdS5CAP4_VL";

std::tuple<std::wstring, std::wstring> XivAlex::ResolveGameReleaseRegion() {
	std::wstring path(PATHCCH_MAX_CCH, L'\0');
	path.resize(GetModuleFileNameW(nullptr, &path[0], static_cast<DWORD>(path.size())));
	return ResolveGameReleaseRegion(path);
}

static std::wstring TestPublisher(const std::filesystem::path& path) {
	// See: https://docs.microsoft.com/en-US/troubleshoot/windows/win32/get-information-authenticode-signed-executables

	constexpr auto ENCODING = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;

	HCERTSTORE hStore = nullptr;
	HCRYPTMSG hMsg = nullptr;
	DWORD dwEncoding = 0, dwContentType = 0, dwFormatType = 0;
	std::vector<Utils::CallOnDestruction> cleanupList;
	if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE,
		path.c_str(),
		CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
		CERT_QUERY_FORMAT_FLAG_BINARY,
		0,
		&dwEncoding,
		&dwContentType,
		&dwFormatType,
		&hStore,
		&hMsg,
		nullptr))
		return L"";
	if (hMsg) cleanupList.emplace_back([hMsg]() { CryptMsgClose(hMsg); });
	if (hStore) cleanupList.emplace_back([hStore]() { CertCloseStore(hStore, 0); });

	DWORD cbData = 0;
	std::vector<uint8_t> signerInfoBuf;
	for (size_t i = 0; i < 2; ++i) {
		if (!CryptMsgGetParam(hMsg,
			CMSG_SIGNER_INFO_PARAM,
			0,
			signerInfoBuf.empty() ? nullptr : &signerInfoBuf[0],
			&cbData))
			return L"";
		signerInfoBuf.resize(cbData);
	}

	const auto& signerInfo = *reinterpret_cast<CMSG_SIGNER_INFO*>(&signerInfoBuf[0]);

	CERT_INFO certInfo{};
	certInfo.Issuer = signerInfo.Issuer;
	certInfo.SerialNumber = signerInfo.SerialNumber;
	const auto pCertContext = CertFindCertificateInStore(hStore,
		ENCODING,
		0,
		CERT_FIND_SUBJECT_CERT,
		&certInfo,
		nullptr);
	if (!pCertContext)
		return L"";
	if (pCertContext) cleanupList.emplace_back([pCertContext]() { CertFreeCertificateContext(pCertContext); });

	std::wstring country;
	const auto pvTypePara = const_cast<char*>(szOID_COUNTRY_NAME);
	country.resize(CertGetNameStringW(pCertContext, CERT_NAME_ATTR_TYPE, 0, pvTypePara, nullptr, 0));
	country.resize(CertGetNameStringW(pCertContext, CERT_NAME_ATTR_TYPE, 0, pvTypePara, &country[0], static_cast<DWORD>(country.size())) - 1);

	return country;
}

std::tuple<std::wstring, std::wstring> XivAlex::ResolveGameReleaseRegion(const std::filesystem::path& path) {
	const auto installationDir = path.parent_path().parent_path(); // remove "\game", "\ffxiv_dx11.exe"
	const auto gameDir = installationDir / L"game";
	const auto gameVerPath = gameDir / L"ffxivgame.ver";

	std::wstring gameVer;
	{
		const Utils::Win32::Handle hGameVer(
			CreateFileW(gameVerPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr),
			INVALID_HANDLE_VALUE,
			L"ResolveGameReleaseRegion: Failed to open game version file({})",
			gameVerPath);
		LARGE_INTEGER size{};
		GetFileSizeEx(hGameVer, &size);
		if (size.QuadPart > 64)
			throw std::runtime_error("ResolveGameReleaseRegion: Game version file size too big.");
		std::string buf;
		buf.resize(static_cast<size_t>(size.QuadPart));
		DWORD read = 0;
		if (!ReadFile(hGameVer, &buf[0], size.LowPart, &read, nullptr))
			throw Utils::Win32::Error("ResolveGameReleaseRegion: Failed to read game version file");
		if (read != size.LowPart)
			throw std::runtime_error("ResolveGameReleaseRegion: Failed to read game version file in entirety.");
		gameVer = Utils::FromUtf8(buf);

		for (auto& chr : gameVer) {
			for (auto i : L"<>:\"/\\|?*") {
				if (chr == i || chr < 32)
					chr = L'_';
			}
		}
	}

	std::map<std::wstring, size_t> publisherCountries;
	for (const auto& possibleRegionSpecificFilesDir : {
		installationDir / L"boot" / L"ffxiv*.exe",
		installationDir / L"sdo" / L"sdologinentry.dll",
		}) {
		WIN32_FIND_DATAW data{};
		const auto hFindFile = Utils::Win32::FindFile(
			FindFirstFileW(possibleRegionSpecificFilesDir.c_str(), &data),
			INVALID_HANDLE_VALUE);
		if (!hFindFile)
			continue;

		do {
			const auto path = possibleRegionSpecificFilesDir.parent_path() / data.cFileName;
			const auto publisherCountry = TestPublisher(path);
			if (!publisherCountry.empty())
				publisherCountries[publisherCountry]++;
		} while (FindNextFileW(hFindFile, &data));
	}

	if (!publisherCountries.empty()) {
		auto maxElem = std::max_element(publisherCountries.begin(), publisherCountries.end());
		return std::make_tuple(
			maxElem->first,
			gameVer
		);
	}

	auto buf = installationDir.wstring();
	CharLowerW(&buf[0]);
	uLong crc = crc32(crc32(0L, nullptr, 0),
		reinterpret_cast<Bytef*>(&buf[0]),
		static_cast<uInt>(buf.size() * sizeof buf[0]));

	return std::make_tuple(
		std::format(L"unknown_{:08x}", crc),
		gameVer
	);
}

XivAlex::VersionInformation XivAlex::CheckUpdates() {
	std::ostringstream os;

	curlpp::Easy req;
	req.setOpt(curlpp::options::Url("https://api.github.com/repos/Soreepeong/XivAlexander/releases/latest"));
	req.setOpt(curlpp::options::UserAgent("Mozilla/5.0"));
	os << req;
	const auto parsed = nlohmann::json::parse(os.str());
	const auto assets = parsed.at("assets");
	if (assets.empty())
		throw std::runtime_error("Could not detect updates. Please try again at a later time.");
	const auto item = assets[0];

	std::istringstream in(parsed.at("published_at").get<std::string>());
	std::chrono::sys_seconds tp;
	from_stream(in, "%FT%TZ", tp);
	if (in.fail())
		throw std::format_error(std::format("Failed to parse datetime string \"{}\"", in.str()));

	return {
		.Name = parsed.at("name").get<std::string>(),
		.Body = parsed.at("body").get<std::string>(),
		.PublishDate = std::chrono::zoned_time(std::chrono::current_zone(), tp),
		.DownloadLink = item.at("browser_download_url").get<std::string>(),
		.DownloadSize = item.at("size").get<size_t>(),
	};
}

static std::wstring ReadRegistryAsString(const wchar_t* lpSubKey, const wchar_t* lpValueName, int mode = 0) {
	if (mode == 0) {
		auto res1 = ReadRegistryAsString(lpSubKey, lpValueName, KEY_WOW64_32KEY);
		if (res1.empty())
			res1 = ReadRegistryAsString(lpSubKey, lpValueName, KEY_WOW64_64KEY);
		return res1;
	}
	HKEY hKey;
	if (const auto err = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
		lpSubKey,
		0, KEY_READ | mode, &hKey))
		return {};
	Utils::CallOnDestruction c([hKey]() { RegCloseKey(hKey); });

	DWORD buflen = 0;
	if (RegQueryValueExW(hKey, lpValueName, nullptr, nullptr, nullptr, &buflen))
		return {};

	std::wstring buf;
	buf.resize(buflen);
	if (RegQueryValueExW(hKey, lpValueName, nullptr, nullptr, reinterpret_cast<LPBYTE>(&buf[0]), &buflen))
		return {};

	return buf;
}

std::map<XivAlex::GameRegion, XivAlex::GameRegionInfo> XivAlex::FindGameLaunchers() {
	std::map<GameRegion, GameRegionInfo> result;

	if (const auto reg = ReadRegistryAsString(
		LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{2B41E132-07DF-4925-A3D3-F2D1765CCDFE})",
		L"DisplayIcon"
	); !reg.empty()) {
		GameRegionInfo info{
			.Type = GameRegion::International,
			.RootPath = std::filesystem::path(reg).parent_path().parent_path(),
#if INTPTR_MAX == INT32_MAX

			.BootApp = info.RootPath / L"boot" / L"ffxivboot.exe",

#elif INTPTR_MAX == INT64_MAX

			.BootApp = info.RootPath / L"boot" / L"ffxivboot64.exe",

#endif
			.BootAppRequiresAdmin = false,
			.RelatedApps = {
				info.RootPath / L"boot" / L"ffxivboot.exe",
				info.RootPath / L"boot" / L"ffxivboot64.exe",
				info.RootPath / L"boot" / L"ffxivconfig.exe",
				info.RootPath / L"boot" / L"ffxivconfig64.exe",
				info.RootPath / L"boot" / L"ffxivlauncher.exe",
				info.RootPath / L"boot" / L"ffxivlauncher64.exe",
				info.RootPath / L"boot" / L"ffxivupdater.exe",
				info.RootPath / L"boot" / L"ffxivupdater.exe",
			},
		};
		
		result.emplace(GameRegion::International, info);
	}

	if (const auto reg = ReadRegistryAsString(
		LR"(SOFTWARE\Classes\ff14kr\shell\open\command)",
		L""
	); !reg.empty()) {
		int cnt = 0;
		const auto argv = CommandLineToArgvW(&reg[0], &cnt);
		if (!argv)
			return {};

		Utils::CallOnDestruction c2([argv]() {LocalFree(argv); });
		if (cnt < 1)
			return {};

		GameRegionInfo info{
			.Type = GameRegion::Korean,
			.RootPath = std::filesystem::path(argv[0]).parent_path().parent_path(),
			.BootApp = info.RootPath / L"boot" / L"FFXIV_Boot.exe",
			.BootAppRequiresAdmin = true,
			.RelatedApps = {
				info.RootPath / L"boot" / L"FFXIV_Boot.exe",
				info.RootPath / L"boot" / L"FFXIV_Launcher.exe",
			},
		};
		result.emplace(GameRegion::Korean, info);
	}

	if (const auto reg = ReadRegistryAsString(
		LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\FFXIV)",
		L"DisplayIcon"
	); !reg.empty()) {
		GameRegionInfo info{
			.Type = GameRegion::Chinese,
			.RootPath = std::filesystem::path(reg).parent_path(),
			.BootApp = info.RootPath / L"FFXIVBoot.exe",
			.BootAppRequiresAdmin = true,
			.RelatedApps = {
				info.RootPath / "LauncherUpdate" / "LauncherUpdater.exe",
				info.RootPath / "FFXIVBoot.exe",
				info.RootPath / "sdo" / "sdologin" / "sdologin.exe",
				info.RootPath / "sdo" / "sdologin" / "Launcher.exe",
				info.RootPath / "sdo" / "sdologin" / "sdolplugin.exe",
				info.RootPath / "sdo" / "sdologin" / "update.exe",
			},
		};
		result.emplace(GameRegion::Chinese, info);
	}
	return result;
}

std::vector<std::pair<std::string, std::string>> XivAlex::ParseGameCommandLine(std::string source, bool* wasObfuscated) {
	std::vector<std::pair<std::string, std::string>> res;
	
	if (source.starts_with("//**sqex0003") && source.ends_with("**//") && source.size() >= 17) {
		const auto chksum = SqexChecksumTable.find(source[source.size() - 5]);
		{
			CryptoPP::Base64URLDecoder b64decoder;
			b64decoder.Put(reinterpret_cast<const uint8_t*>(&source[12]), source.size() - 17);
			b64decoder.MessageEnd();
			source.resize(static_cast<size_t>(b64decoder.MaxRetrievable()));
			b64decoder.Get(reinterpret_cast<uint8_t*>(&source[0]), source.size());
			SqexBlowfishModifier(source);
		}
		
		FILETIME ct, xt, kt, ut, nft;
		if (!GetProcessTimes(GetCurrentProcess(), &ct, &xt, &kt, &ut))
			throw Utils::Win32::Error("GetProcessTimes(GetCurrentProcess(), ...)");
		GetSystemTimeAsFileTime(&nft);
		const auto creationTickCount = GetTickCount64() - (ULARGE_INTEGER{{ nft.dwLowDateTime, nft.dwHighDateTime}}.QuadPart - ULARGE_INTEGER{{ct.dwLowDateTime, ct.dwHighDateTime}}.QuadPart) / 10000;
		
		CryptoPP::ECB_Mode<CryptoPP::Blowfish>::Decryption dec;
		for (auto [val, count] : {
			std::make_pair(chksum << 16 | creationTickCount & 0xFF000000, 16),
			std::make_pair(chksum << 16 | 0ULL, 0xFFF),
		}) {
			for (; --count; val += 0x100000) {
				const auto key = std::format("{:08x}", val & 0xFFFF0000);
				
				std::string decrypted = source;
				dec.SetKey(reinterpret_cast<const uint8_t*>(&key[0]), key.size());
				dec.ProcessString(reinterpret_cast<uint8_t*>(&decrypted[0]), decrypted.size());
				if (!decrypted.starts_with("= T ") && !decrypted.starts_with(" T/ "))
					continue;
				
				SqexBlowfishModifier(decrypted);
				source.clear();

				for (const auto& item : SqexSplit(decrypted, '/', SIZE_MAX)) {
					const auto keyValue = SqexSplit(item, '=', 1);
					if (keyValue.size() == 1)
						res.emplace_back(Utils::StringReplaceAll(keyValue[0], "  ", " "), "");
					else
						res.emplace_back(Utils::StringReplaceAll(keyValue[0], "  ", " "),
							Utils::StringReplaceAll(keyValue[1], "  ", " "));
				}
				if (wasObfuscated)
					*wasObfuscated = true;
				return res;
			}
		}
		throw std::invalid_argument("bad encoded string");
		
	} else {
		if (int nArgs; LPWSTR* szArgList = CommandLineToArgvW(std::format(L"test.exe {}", source).c_str(), &nArgs)) {
			const auto cleanup = Utils::CallOnDestruction([szArgList](){ LocalFree(szArgList); });
			for (int i = 1; i < nArgs; i++) {
				const auto arg = Utils::ToUtf8(szArgList[i]);
				const auto eq = arg.find_first_of("=");
				if (eq == std::string::npos)
					res.emplace_back(arg, "");
				else
					res.emplace_back(arg.substr(0, eq), arg.substr(eq + 1));
			}
		}
		if (wasObfuscated)
			*wasObfuscated = false;
		return res;
	}
}

std::string XivAlex::CreateGameCommandLine(const std::vector<std::pair<std::string, std::string>>& map, bool obfuscate) {
	if (obfuscate) {
		const auto tick = static_cast<DWORD>(GetTickCount64() & 0xFFFFFFFF);  // Practically GetTickCount, but silencing the warning
		const auto key = std::format("{:08x}", tick & 0xFFFF0000);
		const auto chksum = SqexChecksumTable[(tick >> 16) & 0xF];

		std::string encrypted;
		{
			std::ostringstream plain;
			plain << " T =" << tick;
			for (const auto& [k, v] : map) {
				if (k == "T")
					continue;
				
				plain << " /" << Utils::StringReplaceAll(k, " ", "  ")
					<< " =" << Utils::StringReplaceAll(v, " ", "  ");
			}
			encrypted = plain.str();
		}
		encrypted.resize((encrypted.size() + 7) / 8 * 8, '\0');
		{
			CryptoPP::ECB_Mode<CryptoPP::Blowfish>::Encryption enc;
			enc.SetKey(reinterpret_cast<const uint8_t*>(&key[0]), key.size());
			SqexBlowfishModifier(encrypted);
			enc.ProcessString(reinterpret_cast<uint8_t*>(&encrypted[0]), encrypted.size());
			SqexBlowfishModifier(encrypted);
		}
		{
			CryptoPP::Base64URLEncoder b64encoder;
			b64encoder.Put(reinterpret_cast<const uint8_t*>(&encrypted[0]), encrypted.size());
			b64encoder.MessageEnd();
			encrypted.resize(static_cast<size_t>(b64encoder.MaxRetrievable()));
			b64encoder.Get(reinterpret_cast<uint8_t*>(&encrypted[0]), encrypted.size());
		}

		return std::format("//**sqex0003{}{}**//", encrypted, chksum);
		
	} else {
		std::vector<std::string> args;
		for (const auto& [k, v] : map) {
			if (k == "T")
				continue;
			args.emplace_back(std::format("{}={}", k, v));
		}
		return Utils::Win32::ReverseCommandLineToArgv(args);
	}
}

void XivAlex::SqexBlowfishModifier(std::string& s) {
	if (s.size() % 4)
		throw std::invalid_argument("string length % 4 != 0");
	for (auto& i : std::span(reinterpret_cast<uint32_t*>(&s[0]), s.size() / 4))
		i = _byteswap_ulong(i);
}

std::vector<std::string> XivAlex::SqexSplit(const std::string& source, char delim, size_t maxc) {
	std::vector<std::string> split;
	split.resize(1);
	auto begin = false;
	for (const auto c : source) {
		if (c != ' ')
			begin = true;
		else if (!begin)
			continue;

		if (c == delim)
			split.emplace_back(&c, 1);
		else
			split.back() += c;
	}
	for (size_t i = 1; i < split.size();) {
		const auto nonspace = split[i - 1].find_last_not_of(' ');
		if (i > maxc || nonspace == std::string::npos || (split[i - 1].size() - nonspace - 1) % 2 == 0) {
			split[i - 1] += split[i];
			split.erase(split.begin() + i);
		} else {
			++i;
		}
	}
	for (auto& s : split) {
		const auto off = !s.empty() && s.front() == delim ? 1 : 0;
		const auto to = !s.empty() && s.back() == ' ' ? s.size() - 1 : s.size();
		s = s.substr(off, to - off);
	}
	return split;
}
