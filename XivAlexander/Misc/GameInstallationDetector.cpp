#include "pch.h"
#include "Misc/GameInstallationDetector.h"

#include "Utils/Win32/Process.h"

static std::string TestPublisher(const std::filesystem::path& path) {
	// See: https://docs.microsoft.com/en-US/troubleshoot/windows/win32/get-information-authenticode-signed-executables

	constexpr auto ENCODING = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;

	HCERTSTORE hStore = nullptr;
	HCRYPTMSG hMsg = nullptr;
	DWORD dwEncoding = 0, dwContentType = 0, dwFormatType = 0;
	std::vector<xivres::util::on_dtor> cleanupList;
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
		return {};
	if (hMsg) cleanupList.emplace_back([hMsg] { CryptMsgClose(hMsg); });
	if (hStore) cleanupList.emplace_back([hStore] { CertCloseStore(hStore, 0); });

	DWORD cbData = 0;
	std::vector<uint8_t> signerInfoBuf;
	for (size_t i = 0; i < 2; ++i) {
		if (!CryptMsgGetParam(hMsg,
			CMSG_SIGNER_INFO_PARAM,
			0,
			signerInfoBuf.empty() ? nullptr : &signerInfoBuf[0],
			&cbData))
			return {};
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
		return {};
	if (pCertContext) cleanupList.emplace_back([pCertContext] { CertFreeCertificateContext(pCertContext); });

	std::wstring country;
	const auto pvTypePara = const_cast<char*>(szOID_COUNTRY_NAME);
	country.resize(CertGetNameStringW(pCertContext, CERT_NAME_ATTR_TYPE, 0, pvTypePara, nullptr, 0));
	country.resize(CertGetNameStringW(pCertContext, CERT_NAME_ATTR_TYPE, 0, pvTypePara, &country[0], static_cast<DWORD>(country.size())) - 1);

	return xivres::util::unicode::convert<std::string>(country);
}

static std::wstring ReadRegistryAsString(const wchar_t* lpSubKey, const wchar_t* lpValueName, HKEY hRoot = HKEY_LOCAL_MACHINE, int mode = 0) {
	if (mode == 0) {
		auto res1 = ReadRegistryAsString(lpSubKey, lpValueName, hRoot, KEY_WOW64_32KEY);
		if (res1.empty())
			res1 = ReadRegistryAsString(lpSubKey, lpValueName, hRoot, KEY_WOW64_64KEY);
		return res1;
	}
	HKEY hKey;
	if (RegOpenKeyExW(hRoot,
		lpSubKey,
		0, KEY_READ | mode, &hKey))
		return {};
	xivres::util::on_dtor c([hKey] { RegCloseKey(hKey); });

	DWORD buflen = 0;
	if (RegQueryValueExW(hKey, lpValueName, nullptr, nullptr, nullptr, &buflen))
		return {};

	std::wstring buf;
	buf.resize(buflen + 1);
	if (RegQueryValueExW(hKey, lpValueName, nullptr, nullptr, reinterpret_cast<LPBYTE>(&buf[0]), &buflen))
		return {};

	buf.erase(std::ranges::find(buf, L'\0'), buf.end());

	return buf;
}

static std::vector<std::wstring> ListRegistrySubkeys(const wchar_t* lpSubKey) {
	HKEY hKey;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, lpSubKey, 0, KEY_READ | KEY_WOW64_64KEY, &hKey))
		return {};
	xivres::util::on_dtor c([hKey] { RegCloseKey(hKey); });

	std::vector<std::wstring> res;
	std::wstring name(256, L'\0');
	for (DWORD i = 0;; ++i) {
		auto length = static_cast<DWORD>(name.size());
		if (RegEnumKeyExW(hKey, i, name.data(), &length, nullptr, nullptr, nullptr, nullptr))
			break;
		res.emplace_back(name.data(), length);
	}
	return res;
}

static std::vector<std::wstring> ListRegistryStringValues(const wchar_t* lpSubKey) {
	HKEY hKey;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, lpSubKey, 0, KEY_READ | KEY_WOW64_64KEY, &hKey))
		return {};
	xivres::util::on_dtor c([hKey] { RegCloseKey(hKey); });

	std::vector<std::wstring> res;
	std::wstring name(256, L'\0');
	std::wstring data(MAX_PATH * 4, L'\0');
	for (DWORD i = 0;; ++i) {
		auto nameLength = static_cast<DWORD>(name.size());
		auto dataBytes = static_cast<DWORD>(data.size() * sizeof(wchar_t));
		DWORD type{};
		if (RegEnumValueW(hKey, i, name.data(), &nameLength, nullptr, &type, reinterpret_cast<LPBYTE>(data.data()), &dataBytes))
			break;
		if (type == REG_SZ)
			res.emplace_back(data.data(), wcsnlen(data.data(), dataBytes / sizeof(wchar_t)));
	}
	return res;
}

XivAlexander::Misc::GameInstallationDetector::GameReleaseInfo XivAlexander::Misc::GameInstallationDetector::GetGameReleaseInfo(std::filesystem::path deepestLookupPath) {
	if (deepestLookupPath.empty())
		deepestLookupPath = Utils::Win32::Process::Current().PathOf();

	std::filesystem::path gameVersionPath;
	while (!exists(gameVersionPath = deepestLookupPath / "game" / "ffxivgame.ver")) {
		auto parentPath = deepestLookupPath.parent_path();
		if (parentPath == deepestLookupPath)
			throw std::runtime_error("Game installation not found");
		deepestLookupPath = std::move(parentPath);
	}

	GameReleaseInfo result{};
	result.RootPath = std::move(deepestLookupPath);
	const auto gvBuffer = Utils::Win32::Handle::FromCreateFile(gameVersionPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0).Read<char>(0, 256, Utils::Win32::Handle::PartialIoMode::AllowPartial);
	result.GameVersion = std::string(gvBuffer.begin(), gvBuffer.end());
	for (auto& chr : result.PathSafeGameVersion = result.GameVersion) {
		for (auto i : "<>:\"/\\|?*") {
			if (chr == i || chr < 32)
				chr = '_';
		}
	}

	std::map<std::string, size_t> publisherCountries;
	for (const auto& [path, filenamePattern] : std::vector<std::pair<std::filesystem::path, std::wregex>>{
			{result.RootPath, std::wregex(LR"(^ffxiv.*bench.*\.exe$)", std::regex::icase)},
			{result.RootPath / L"boot", std::wregex(LR"(^ffxiv.*\.exe$)", std::regex::icase)},
			{result.RootPath / L"sdo", std::wregex(LR"(^sdologinentry\.dll$)", std::regex::icase)},
		}) {
		try {
			for (const auto& item : std::filesystem::directory_iterator(path)) {
				if (!std::regex_search(item.path().filename().wstring(), filenamePattern))
					continue;
				const auto publisherCountry = TestPublisher(item);
				if (!publisherCountry.empty())
					publisherCountries[publisherCountry]++;
			}
		} catch (...) {
			// pass
		}
	}

	if (!publisherCountries.empty()) {
		// a signer other than Square Enix's only shows up in other publishers' releases
		result.CountryCode = std::ranges::max_element(publisherCountries, [](const auto& l, const auto& r) {
			return std::make_pair(l.first != "JP", l.second) < std::make_pair(r.first != "JP", r.second);
		})->first;
		if (result.CountryCode == "JP") {
			result.Region = xivres::game_release_publisher::SquareEnix;
#if INTPTR_MAX == INT32_MAX
			result.BootApp = result.RootPath / L"boot" / L"ffxivboot.exe";
#elif INTPTR_MAX == INT64_MAX
			result.BootApp = result.RootPath / L"boot" / L"ffxivboot64.exe";
#endif
			result.RelatedApps = {
				result.RootPath / L"boot" / L"ffxivboot.exe",
				result.RootPath / L"boot" / L"ffxivboot64.exe",
				result.RootPath / L"boot" / L"ffxivconfig.exe",
				result.RootPath / L"boot" / L"ffxivconfig64.exe",
				result.RootPath / L"boot" / L"ffxivlauncher.exe",
				result.RootPath / L"boot" / L"ffxivlauncher64.exe",
				result.RootPath / L"boot" / L"ffxivupdater.exe",
				result.RootPath / L"boot" / L"ffxivupdater64.exe",
			};

		} else if (result.CountryCode == "CN") {
			result.Region = xivres::game_release_publisher::ShengquGames;
			result.BootApp = result.RootPath / L"FFXIVBoot.exe";
			result.RelatedApps = {
				result.RootPath / L"LauncherUpdate" / L"LauncherUpdater.exe",
				result.RootPath / L"FFXIVBoot.exe",
				result.RootPath / L"sdo" / L"sdologin" / L"sdologin.exe",
				result.RootPath / L"sdo" / L"sdologin" / L"Launcher.exe",
				result.RootPath / L"sdo" / L"sdologin" / L"sdolplugin.exe",
				result.RootPath / L"sdo" / L"sdologin" / L"update.exe",
			};

		} else if (result.CountryCode == "KR") {
			result.Region = xivres::game_release_publisher::ActozSoft;
			result.BootApp = result.RootPath / L"boot" / L"FFXIV_Boot.exe";
			result.RelatedApps = {
				result.RootPath / L"boot" / L"FFXIV_Boot.exe",
				result.RootPath / L"boot" / L"FFXIV_Launcher.exe",
			};

		} else if (result.CountryCode == "TW") {
			result.Region = xivres::game_release_publisher::UserjoyGames;
			result.BootApp = result.RootPath / L"boot" / L"FfxivLauncherTC.exe";
			result.RelatedApps = {
				result.RootPath / L"boot" / L"FfxivLauncherTC.exe",
			};

		} else
			throw std::runtime_error(std::format("{} is unsupported", result.CountryCode));
		return result;
	}

	throw std::runtime_error("Could not determine game region");
}

std::vector<XivAlexander::Misc::GameInstallationDetector::GameReleaseInfo> XivAlexander::Misc::GameInstallationDetector::FindInstallations() {
	std::vector<GameReleaseInfo> result;

	if (const auto reg = ReadRegistryAsString(
		LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{2B41E132-07DF-4925-A3D3-F2D1765CCDFE})",
		L"DisplayIcon"
	); !reg.empty()) {
		try {
			result.emplace_back(GetGameReleaseInfo(reg));
		} catch (...) {
			// pass
		}
	}

	for (const auto steamAppId : {
			39210,  // paid
			312060,  // free trial
		}) {
		if (const auto reg = ReadRegistryAsString(std::format(LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App {})", steamAppId).c_str(), L"InstallLocation"); !reg.empty()) {
			try {
				result.emplace_back(GetGameReleaseInfo(reg));
			} catch (...) {
				// pass
			}
		}
	}

	if (const auto reg = ReadRegistryAsString(
		LR"(SOFTWARE\Classes\ff14kr\shell\open\command)",
		L""
	); !reg.empty()) {
		try {
			result.emplace_back(GetGameReleaseInfo(Utils::Win32::CommandLineToArgs(reg)[0]));
		} catch (...) {
			// pass
		}
	}

	if (const auto reg = ReadRegistryAsString(
		LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\FFXIV)",
		L"DisplayIcon"
	); !reg.empty()) {
		try {
			result.emplace_back(GetGameReleaseInfo(reg));
		} catch (...) {
			// pass
		}
	}

	{
		std::vector<std::filesystem::path> candidates;
		if (auto dir = ReadRegistryAsString(LR"(Software\Classes\com.userjoy.ffxiv)", L"InstallDir", HKEY_CURRENT_USER); !dir.empty())
			candidates.emplace_back(std::move(dir));

		constexpr auto UserData = LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Installer\UserData)";
		for (const auto& sid : ListRegistrySubkeys(UserData)) {
			for (auto& path : ListRegistryStringValues(std::format(LR"({}\{}\Components\F5FB90B6D9D62AE0A7D105975A987C2B)", UserData, sid).c_str()))
				candidates.emplace_back(std::move(path));
		}

		for (const auto& candidate : candidates) {
			try {
				result.emplace_back(GetGameReleaseInfo(candidate));
			} catch (...) {
				// pass
			}
		}

		PWSTR pszProgramFiles{};
		const auto hr = SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &pszProgramFiles);
		const auto programFiles = SUCCEEDED(hr) ? std::filesystem::path(pszProgramFiles) : std::filesystem::path();
		CoTaskMemFree(pszProgramFiles);
		if (const auto root = programFiles / L"USERJOY GAMES" / L"FINAL FANTASY XIV TC";
			!programFiles.empty() && exists(root / L"game" / L"ffxivgame.ver")) {
			try {
				result.emplace_back(GetGameReleaseInfo(root));
			} catch (...) {
				// pass
			}
		}
	}

    std::set<std::filesystem::path> seen;
    std::erase_if(result, [&seen](const auto& value) {
		return !seen.insert(value.RootPath).second;
	});

	std::ranges::sort(result, [](const auto& l, const auto& r) {
		if (l.Region != r.Region)
			return l.Region < r.Region;
		if (l.GameVersion != r.GameVersion)
			return l.GameVersion< r.GameVersion;
		return l.RootPath < r.RootPath;
	});

	return result;
}
