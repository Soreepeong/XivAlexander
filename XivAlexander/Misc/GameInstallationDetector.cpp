#include "pch.h"
#include "Misc/GameInstallationDetector.h"

#include "XivAlexanderCommon/Utils/Win32/Process.h"

static std::string TestPublisher(const std::filesystem::path& path) {
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

	return Utils::ToUtf8(country);
}

static std::wstring ReadRegistryAsString(const wchar_t* lpSubKey, const wchar_t* lpValueName, int mode = 0) {
	if (mode == 0) {
		auto res1 = ReadRegistryAsString(lpSubKey, lpValueName, KEY_WOW64_32KEY);
		if (res1.empty())
			res1 = ReadRegistryAsString(lpSubKey, lpValueName, KEY_WOW64_64KEY);
		return res1;
	}
	HKEY hKey;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
		lpSubKey,
		0, KEY_READ | mode, &hKey))
		return {};
	Utils::CallOnDestruction c([hKey] { RegCloseKey(hKey); });

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
		result.CountryCode = std::ranges::max_element(publisherCountries)->first;
		if (result.CountryCode == "JP") {
			result.Region = Sqex::GameReleaseRegion::International;
			result.BootAppDirectlyInjectable = true;
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
			result.Region = Sqex::GameReleaseRegion::Chinese;
			result.BootApp = result.RootPath / L"FFXIVBoot.exe";
			result.BootAppRequiresAdmin = true;
			result.BootAppDirectlyInjectable = true;
			result.RelatedApps = {
				result.RootPath / L"LauncherUpdate" / L"LauncherUpdater.exe",
				result.RootPath / L"FFXIVBoot.exe",
				result.RootPath / L"sdo" / L"sdologin" / L"sdologin.exe",
				result.RootPath / L"sdo" / L"sdologin" / L"Launcher.exe",
				result.RootPath / L"sdo" / L"sdologin" / L"sdolplugin.exe",
				result.RootPath / L"sdo" / L"sdologin" / L"update.exe",
			};

		} else if (result.CountryCode == "KR") {
			result.Region = Sqex::GameReleaseRegion::Korean;
			result.BootApp = result.RootPath / L"boot" / L"FFXIV_Boot.exe";
			result.BootAppRequiresAdmin = true;
			result.BootAppDirectlyInjectable = true;
			result.RelatedApps = {
				result.RootPath / L"boot" / L"FFXIV_Boot.exe",
				result.RootPath / L"boot" / L"FFXIV_Launcher.exe",
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
