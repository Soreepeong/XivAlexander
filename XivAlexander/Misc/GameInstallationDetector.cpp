#include "pch.h"
#include "Misc/GameInstallationDetector.h"

#include "Utils/StringUtils.h"
#include "Utils/Win32/Process.h"

namespace {
	std::string TestPublisher(const std::filesystem::path& path) {
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
		country.resize(CertGetNameStringW(pCertContext, CERT_NAME_ATTR_TYPE, 0, pvTypePara, country.data(), static_cast<DWORD>(country.size())) - 1);

		return xivres::util::unicode::convert<std::string>(country);
	}

	std::wstring ReadRegistryAsString(const wchar_t* lpSubKey, const wchar_t* lpValueName, HKEY hRoot = HKEY_LOCAL_MACHINE, int mode = 0) {
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
		if (RegQueryValueExW(hKey, lpValueName, nullptr, nullptr, reinterpret_cast<LPBYTE>(buf.data()), &buflen))
			return {};

		buf.erase(std::ranges::find(buf, L'\0'), buf.end());

		return buf;
	}

	std::vector<std::wstring> ListRegistrySubkeys(const wchar_t* lpSubKey) {
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

	std::vector<std::wstring> ListRegistryStringValues(const wchar_t* lpSubKey) {
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
}

static XivAlexander::Misc::GameInstallationDetector::GameReleaseInfo ReadRootInfo(std::filesystem::path root);

XivAlexander::Misc::GameInstallationDetector::GameReleaseInfo XivAlexander::Misc::GameInstallationDetector::GetGameReleaseInfo(std::filesystem::path deepestLookupPath) {
	if (deepestLookupPath.empty())
		deepestLookupPath = Utils::Win32::Process::Current().PathOf();

	while (!exists(deepestLookupPath / "game" / "ffxivgame.ver")) {
		auto parentPath = deepestLookupPath.parent_path();
		if (parentPath == deepestLookupPath)
			throw std::runtime_error("Game installation not found");
		deepestLookupPath = std::move(parentPath);
	}

	auto result = ReadRootInfo(std::move(deepestLookupPath));
	if (result.Region == xivres::game_release_publisher::Unspecified)
		throw std::runtime_error(result.CountryCode.empty() ? "Could not determine game region" : std::format("{} is unsupported", result.CountryCode));
	return result;
}

// Region stays Unspecified when no known publisher signed the programs.
static XivAlexander::Misc::GameInstallationDetector::GameReleaseInfo ReadRootInfo(std::filesystem::path root) {
	using XivAlexander::Misc::GameInstallationDetector::GameReleaseInfo;
	const auto gameVersionPath = root / "game" / "ffxivgame.ver";
	GameReleaseInfo result{};
	result.RootPath = std::move(root);
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
		} else if (result.CountryCode == "CN") {
			result.Region = xivres::game_release_publisher::ShengquGames;
			result.BootApp = result.RootPath / L"FFXIVBoot.exe";
		} else if (result.CountryCode == "KR") {
			result.Region = xivres::game_release_publisher::ActozSoft;
			result.BootApp = result.RootPath / L"boot" / L"FFXIV_Boot.exe";
		} else if (result.CountryCode == "TW") {
			result.Region = xivres::game_release_publisher::UserjoyGames;
			result.BootApp = result.RootPath / L"boot" / L"FfxivLauncherTC.exe";
		}
	}
	return result;
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

namespace {
	using namespace XivAlexander::Misc::GameInstallationDetector;

	std::string ReadSmallFile(const std::filesystem::path& path, size_t maxSize = 4 * 1024 * 1024) {
		const auto f = Utils::Win32::Handle::FromCreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0);
		const auto size = f.GetFileSize();
		if (size > maxSize)
			throw std::runtime_error("File too big");
		std::string buf(static_cast<size_t>(size), '\0');
		buf.resize(f.Read(0, buf.data(), buf.size(), Utils::Win32::Handle::PartialIoMode::AllowPartial));
		return buf;
	}

	using Utils::EndsWithI;
	using Utils::StartsWithI;

	bool LooksLikeAbsolutePath(std::wstring_view s) {
		return (s.size() >= 3 && std::iswalpha(s[0]) && s[1] == L':' && (s[2] == L'\\' || s[2] == L'/')) || s.starts_with(L"\\\\");
	}

	std::filesystem::path ExpandEnvironment(const std::wstring& s) {
		std::wstring buf(PATHCCH_MAX_CCH, L'\0');
		const auto length = ExpandEnvironmentStringsW(s.c_str(), buf.data(), static_cast<DWORD>(buf.size()));
		if (!length || length > buf.size())
			return s;
		buf.resize(length - 1);
		return buf;
	}

	std::optional<std::filesystem::path> XivLauncherGamePath(const std::filesystem::path& configPath) {
		try {
			const auto json = nlohmann::json::parse(ReadSmallFile(configPath), nullptr, false);
			if (json.is_discarded() || !json.is_object())
				return std::nullopt;
			if (const auto it = json.find("GamePath"); it != json.end() && it->is_string())
				return xivres::util::unicode::convert<std::wstring>(it->get<std::string>());
		} catch (...) {
		}
		return std::nullopt;
	}

	void AddRegistryCandidates(std::vector<InstallationCandidate>& out) {
		const auto add = [&out](std::filesystem::path path, DiscoverySource source, std::wstring detail) {
			if (!path.empty())
				out.emplace_back(InstallationCandidate{std::move(path), source, std::move(detail)});
		};

		constexpr auto SquareEnixKey = LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{2B41E132-07DF-4925-A3D3-F2D1765CCDFE})";
		add(ReadRegistryAsString(SquareEnixKey, L"DisplayIcon"), DiscoverySource::RegistrySquareEnix, std::format(L"HKLM\\{}", SquareEnixKey));

		for (const auto steamAppId : {39210, 312060}) {
			const auto key = std::format(LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App {})", steamAppId);
			add(ReadRegistryAsString(key.c_str(), L"InstallLocation"), DiscoverySource::Steam, std::format(L"HKLM\\{}", key));
		}

		if (const auto reg = ReadRegistryAsString(LR"(SOFTWARE\Classes\ff14kr\shell\open\command)", L""); !reg.empty()) {
			if (const auto args = Utils::Win32::CommandLineToArgs(reg); !args.empty())
				add(args[0], DiscoverySource::RegistryActoz, LR"(HKLM\SOFTWARE\Classes\ff14kr)");
		}

		add(ReadRegistryAsString(LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\FFXIV)", L"DisplayIcon"),
			DiscoverySource::RegistryShengqu, LR"(HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\FFXIV)");

		add(ReadRegistryAsString(LR"(Software\Classes\com.userjoy.ffxiv)", L"InstallDir", HKEY_CURRENT_USER),
			DiscoverySource::RegistryUserjoy, LR"(HKCU\Software\Classes\com.userjoy.ffxiv)");

		constexpr auto UserData = LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Installer\UserData)";
		for (const auto& sid : ListRegistrySubkeys(UserData)) {
			const auto key = std::format(LR"({}\{}\Components\F5FB90B6D9D62AE0A7D105975A987C2B)", UserData, sid);
			for (auto& path : ListRegistryStringValues(key.c_str()))
				add(std::move(path), DiscoverySource::MsiComponent, std::format(L"HKLM\\{}", key));
		}

		for (const auto& [folderId, relative] : std::initializer_list<std::pair<KNOWNFOLDERID, const wchar_t*>>{
				{FOLDERID_ProgramFilesX86, LR"(SquareEnix\FINAL FANTASY XIV - A Realm Reborn)"},
				{FOLDERID_ProgramFiles, LR"(USERJOY GAMES\FINAL FANTASY XIV TC)"},
			}) {
			PWSTR psz{};
			if (SUCCEEDED(SHGetKnownFolderPath(folderId, 0, nullptr, &psz))) {
				const auto path = std::filesystem::path(psz) / relative;
				if (exists(path))
					add(path, DiscoverySource::KnownFolder, path.wstring());
			}
			CoTaskMemFree(psz);
		}
	}

	void AddSteamLibraryCandidates(std::vector<InstallationCandidate>& out) {
		auto steamPath = ReadRegistryAsString(LR"(Software\Valve\Steam)", L"SteamPath", HKEY_CURRENT_USER);
		if (steamPath.empty())
			return;
		std::ranges::replace(steamPath, L'/', L'\\');

		std::vector<std::filesystem::path> libraries{steamPath};
		for (const auto& vdf : {std::filesystem::path(steamPath) / L"steamapps" / L"libraryfolders.vdf", std::filesystem::path(steamPath) / L"config" / L"libraryfolders.vdf"}) {
			try {
				const auto text = ReadSmallFile(vdf);
				static const std::regex pathPattern(R"x("path"\s+"((?:[^"\\]|\\.)*)")x");
				for (auto it = std::sregex_iterator(text.begin(), text.end(), pathPattern); it != std::sregex_iterator(); ++it) {
					std::string value;
					const auto raw = (*it)[1].str();
					for (size_t i = 0; i < raw.size(); ++i)
						value.push_back(raw[i] == '\\' && i + 1 < raw.size() ? raw[++i] : raw[i]);
					libraries.emplace_back(xivres::util::unicode::convert<std::wstring>(value));
				}
				break;
			} catch (...) {
			}
		}

		for (const auto& library : libraries) {
			const auto apps = library / L"steamapps";
			for (const auto appId : {39210, 312060}) {
				const auto manifest = apps / std::format(L"appmanifest_{}.acf", appId);
				try {
					const auto text = ReadSmallFile(manifest);
					static const std::regex installDirPattern(R"x("installdir"\s+"([^"]*)")x");
					if (std::smatch m; std::regex_search(text, m, installDirPattern))
						out.emplace_back(InstallationCandidate{apps / L"common" / xivres::util::unicode::convert<std::wstring>(m[1].str()), DiscoverySource::Steam, manifest.wstring()});
				} catch (...) {
				}
			}
			if (const auto common = apps / L"common" / L"FINAL FANTASY XIV Online"; exists(common))
				out.emplace_back(InstallationCandidate{common, DiscoverySource::Steam, common.wstring()});
		}
	}

	void AddXivLauncherCandidates(std::vector<InstallationCandidate>& out) {
		try {
			const auto appData = Utils::Win32::EnsureKnownFolderPath(FOLDERID_RoamingAppData);
			for (const auto& item : std::filesystem::directory_iterator(appData)) {
				if (!item.is_directory() || !StartsWithI(item.path().filename().wstring(), L"XIVLauncher"))
					continue;
				const auto config = item.path() / L"launcherConfigV3.json";
				if (const auto gamePath = XivLauncherGamePath(config))
					out.emplace_back(InstallationCandidate{*gamePath, DiscoverySource::XivLauncher, config.wstring()});
			}
		} catch (...) {
		}
	}

	void AddShortcutCandidates(std::vector<InstallationCandidate>& out, std::stop_token stop) {
		constexpr size_t MaxShortcuts = 2000;
		constexpr int MaxDepth = 4;
		size_t count = 0;

		IShellLinkW* pLink{};
		if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pLink))))
			return;
		const auto releaseLink = xivres::util::on_dtor([pLink] { pLink->Release(); });
		IPersistFile* pFile{};
		if (FAILED(pLink->QueryInterface(IID_PPV_ARGS(&pFile))))
			return;
		const auto releaseFile = xivres::util::on_dtor([pFile] { pFile->Release(); });

		for (const auto& [folderId, source] : std::initializer_list<std::pair<KNOWNFOLDERID, DiscoverySource>>{
				{FOLDERID_Desktop, DiscoverySource::ShortcutDesktop},
				{FOLDERID_PublicDesktop, DiscoverySource::ShortcutDesktop},
				{FOLDERID_Programs, DiscoverySource::ShortcutStartMenu},
				{FOLDERID_CommonPrograms, DiscoverySource::ShortcutStartMenu},
			}) {
			PWSTR psz{};
			if (FAILED(SHGetKnownFolderPath(folderId, 0, nullptr, &psz))) {
				CoTaskMemFree(psz);
				continue;
			}
			const auto root = std::filesystem::path(psz);
			CoTaskMemFree(psz);

			std::error_code ec;
			for (auto it = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec);
				!ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
				if (stop.stop_requested() || count >= MaxShortcuts)
					return;
				if (it.depth() >= MaxDepth)
					it.disable_recursion_pending();
				if (!it->is_regular_file(ec) || !EndsWithI(it->path().native(), L".lnk"))
					continue;
				++count;

				// No IShellLink::Resolve: it may search drives and hang on network paths.
				if (FAILED(pFile->Load(it->path().c_str(), STGM_READ)))
					continue;
				std::wstring target(PATHCCH_MAX_CCH, L'\0');
				if (SUCCEEDED(pLink->GetPath(target.data(), static_cast<int>(target.size()), nullptr, SLGP_RAWPATH))) {
					target.resize(wcsnlen(target.c_str(), target.size()));
					if (!target.empty())
						out.emplace_back(InstallationCandidate{ExpandEnvironment(target), source, it->path().wstring()});
				}
				std::wstring arguments(32768, L'\0');
				if (FAILED(pLink->GetArguments(arguments.data(), static_cast<int>(arguments.size()))))
					continue;
				arguments.resize(wcsnlen(arguments.c_str(), arguments.size()));
				if (arguments.empty())
					continue;
				for (const auto& arg : Utils::Win32::CommandLineToArgs(L"x.exe " + arguments)) {
					if (constexpr std::wstring_view roaming = L"--roamingPath="; StartsWithI(arg, roaming)) {
						const auto config = std::filesystem::path(arg.substr(roaming.size())) / L"launcherConfigV3.json";
						if (const auto gamePath = XivLauncherGamePath(config))
							out.emplace_back(InstallationCandidate{*gamePath, DiscoverySource::XivLauncher, config.wstring()});
					} else if (LooksLikeAbsolutePath(arg)) {
						out.emplace_back(InstallationCandidate{ExpandEnvironment(arg), source, it->path().wstring()});
					}
				}
			}
		}
	}

	void AddProcessCandidates(std::vector<InstallationCandidate>& out) {
		const auto hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (hSnapshot == INVALID_HANDLE_VALUE)
			return;
		const auto closeSnapshot = Utils::Win32::Handle(hSnapshot, true);
		PROCESSENTRY32W entry{.dwSize = sizeof entry};
		for (auto ok = Process32FirstW(hSnapshot, &entry); ok; ok = Process32NextW(hSnapshot, &entry)) {
			auto name = std::wstring(entry.szExeFile);
			CharLowerW(name.data());
			if (std::ranges::find(GameProgramNames(), name) == GameProgramNames().end())
				continue;
			for (const auto access : {PROCESS_QUERY_LIMITED_INFORMATION, PROCESS_QUERY_INFORMATION}) {
				const auto hProcess = OpenProcess(access, FALSE, entry.th32ProcessID);
				if (!hProcess)
					continue;
				const auto closeProcess = Utils::Win32::Handle(hProcess, true);
				std::wstring path(PATHCCH_MAX_CCH, L'\0');
				auto length = static_cast<DWORD>(path.size());
				if (QueryFullProcessImageNameW(hProcess, 0, path.data(), &length)) {
					path.resize(length);
					// sdo's Launcher.exe is too common a name to count elsewhere.
					if (name != L"launcher.exe" || path.find(L"\\sdo\\sdologin\\") != std::wstring::npos)
						out.emplace_back(InstallationCandidate{path, DiscoverySource::RunningProcess, std::format(L"PID {}", entry.th32ProcessID)});
				}
				break;
			}
		}
	}

	void AddXivAlexanderConfigCandidates(std::vector<InstallationCandidate>& out, const std::filesystem::path& configPath) {
		if (configPath.empty() || !exists(configPath))
			return;
		try {
			const auto json = nlohmann::json::parse(ReadSmallFile(configPath, 64 * 1024 * 1024), nullptr, false);
			if (!json.is_object())
				return;
			for (const auto& [key, _] : json.items()) {
				const auto path = xivres::util::unicode::convert<std::wstring>(key);
				if (EndsWithI(path, L"\\game\\ffxiv_dx11.exe"))
					out.emplace_back(InstallationCandidate{path, DiscoverySource::XivAlexanderConfig, configPath.wstring()});
			}
		} catch (...) {
		}
	}

	bool IsRoot(const std::filesystem::path& dir) {
		std::error_code ec;
		return std::filesystem::is_regular_file(dir / L"game" / L"ffxivgame.ver", ec) && std::filesystem::is_regular_file(dir / L"game" / L"ffxiv_dx11.exe", ec);
	}

	struct VersionStrings {
		std::wstring FileDescription;
		std::wstring ProductName;
		std::wstring CompanyName;
		std::string Version;
	};

	VersionStrings ReadVersionStrings(const std::filesystem::path& path) {
		VersionStrings result;
		DWORD handle{};
		std::vector<BYTE> block(GetFileVersionInfoSizeW(path.c_str(), &handle));
		if (block.empty() || !GetFileVersionInfoW(path.c_str(), 0, static_cast<DWORD>(block.size()), block.data()))
			return result;

		VS_FIXEDFILEINFO* pFixed{};
		UINT fixedSize{};
		if (VerQueryValueW(block.data(), L"\\", reinterpret_cast<LPVOID*>(&pFixed), &fixedSize) && pFixed && fixedSize >= sizeof *pFixed) {
			result.Version = std::format("{}.{}.{}.{}",
				HIWORD(pFixed->dwFileVersionMS), LOWORD(pFixed->dwFileVersionMS), HIWORD(pFixed->dwFileVersionLS), LOWORD(pFixed->dwFileVersionLS));
		}

		struct LangAndCodePage {
			WORD Language;
			WORD CodePage;
		}* pTranslate{};
		UINT translateSize{};
		if (!VerQueryValueW(block.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<LPVOID*>(&pTranslate), &translateSize) || !translateSize)
			return result;
		const auto query = [&](const wchar_t* name) -> std::wstring {
			wchar_t* buf{};
			UINT size{};
			if (!VerQueryValueW(block.data(), std::format(L"\\StringFileInfo\\{:04x}{:04x}\\{}", pTranslate->Language, pTranslate->CodePage, name).c_str(), reinterpret_cast<LPVOID*>(&buf), &size) || !buf)
				return {};
			return std::wstring(buf, wcsnlen(buf, size));
		};
		result.FileDescription = query(L"FileDescription");
		result.ProductName = query(L"ProductName");
		result.CompanyName = query(L"CompanyName");
		return result;
	}

	bool IsPe32(const std::filesystem::path& path) {
		try {
			const auto data = ReadSmallFile(path, 512 * 1024 * 1024);
			if (data.size() < 0x40)
				return false;
			const auto lfanew = *reinterpret_cast<const LONG*>(&data[0x3c]);
			if (lfanew < 0 || static_cast<size_t>(lfanew) + sizeof(IMAGE_NT_HEADERS32) > data.size())
				return false;
			return reinterpret_cast<const IMAGE_NT_HEADERS32*>(&data[lfanew])->FileHeader.Machine == IMAGE_FILE_MACHINE_I386;
		} catch (...) {
			return false;
		}
	}

	ManagedDllKind Classify(const VersionStrings& v) {
		if (v.FileDescription == L"XivAlexander Loader DLL" || v.FileDescription == L"XivAlexander Main DLL")
			return ManagedDllKind::XivAlexander;
		auto text = std::format(L"{}|{}|{}", v.FileDescription, v.ProductName, v.CompanyName);
		CharLowerW(text.data());
		if (text.find(L"gshade") != std::wstring::npos)
			return ManagedDllKind::GShade;
		if (text.find(L"reshade") != std::wstring::npos)
			return ManagedDllKind::ReShade;
		if (text.find(L"special k") != std::wstring::npos || text.find(L"specialk") != std::wstring::npos)
			return ManagedDllKind::SpecialK;
		if (text.find(L"dxvk") != std::wstring::npos)
			return ManagedDllKind::Dxvk;
		if (text.find(L"enbseries") != std::wstring::npos || text.find(L"enb series") != std::wstring::npos)
			return ManagedDllKind::Enb;
		return ManagedDllKind::Unknown;
	}
}

const std::vector<std::wstring>& XivAlexander::Misc::GameInstallationDetector::GameProgramNames() {
	static const std::vector<std::wstring> names{
		L"ffxiv_dx11.exe",
		L"ffxivboot.exe", L"ffxivboot64.exe",
		L"ffxivlauncher.exe", L"ffxivlauncher64.exe",
		L"ffxivconfig.exe", L"ffxivconfig64.exe",
		L"ffxivupdater.exe", L"ffxivupdater64.exe",
		L"sdologin.exe", L"launcher.exe",
		L"ffxiv_boot.exe", L"ffxiv_launcher.exe",
		L"ffxivlaunchertc.exe",
	};
	return names;
}

std::vector<XivAlexander::Misc::GameInstallationDetector::InstallationCandidate> XivAlexander::Misc::GameInstallationDetector::CollectCandidates(const CollectOptions& options, std::stop_token stop) {
	std::vector<InstallationCandidate> out;
	for (const auto& path : options.CommandLine)
		out.emplace_back(InstallationCandidate{path, DiscoverySource::CommandLine, path.wstring()});
	for (const auto& path : options.UserAdded)
		out.emplace_back(InstallationCandidate{path, DiscoverySource::UserAdded, path.wstring()});
	AddProcessCandidates(out);
	AddXivAlexanderConfigCandidates(out, options.XivAlexanderConfigPath);
	AddRegistryCandidates(out);
	AddSteamLibraryCandidates(out);
	AddXivLauncherCandidates(out);
	if (options.Shortcuts && !stop.stop_requested())
		AddShortcutCandidates(out, stop);
	return out;
}

std::optional<std::filesystem::path> XivAlexander::Misc::GameInstallationDetector::FindRootStructurally(std::filesystem::path path, int maxLevels) {
	if (path.empty())
		return std::nullopt;
	path = path.lexically_normal();
	for (int i = 0; i <= maxLevels; ++i) {
		if (IsRoot(path))
			return path;
		auto parent = path.parent_path();
		if (parent == path)
			break;
		path = std::move(parent);
	}
	return std::nullopt;
}

std::vector<std::filesystem::path> XivAlexander::Misc::GameInstallationDetector::FindRootsBelow(const std::filesystem::path& dir, int depth) {
	if (IsRoot(dir))
		return {dir};
	std::vector<std::filesystem::path> result;
	if (depth <= 0)
		return result;
	std::error_code ec;
	for (auto it = std::filesystem::directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
		if (it->is_directory(ec) && !it->is_symlink(ec))
			std::ranges::move(FindRootsBelow(it->path(), depth - 1), std::back_inserter(result));
	}
	return result;
}

std::optional<FILE_ID_INFO> XivAlexander::Misc::GameInstallationDetector::GetDirectoryId(const std::filesystem::path& dir) {
	const auto hDir = CreateFileW(dir.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (hDir == INVALID_HANDLE_VALUE)
		return std::nullopt;
	const auto closeDir = Utils::Win32::Handle(hDir, true);
	FILE_ID_INFO info{};
	if (!GetFileInformationByHandleEx(hDir, FileIdInfo, &info, sizeof info))
		return std::nullopt;
	return info;
}

bool XivAlexander::Misc::GameInstallationDetector::FoundInstallation::IsSameAs(const FoundInstallation& r) const {
	if (GameDirId && r.GameDirId)
		return GameDirId->VolumeSerialNumber == r.GameDirId->VolumeSerialNumber
			&& std::memcmp(&GameDirId->FileId, &r.GameDirId->FileId, sizeof GameDirId->FileId) == 0;
	std::error_code ec1, ec2;
	const auto l = weakly_canonical(RootPath, ec1);
	const auto rr = weakly_canonical(r.RootPath, ec2);
	return _wcsicmp((ec1 ? RootPath : l).c_str(), (ec2 ? r.RootPath : rr).c_str()) == 0;
}

XivAlexander::Misc::GameInstallationDetector::FoundInstallation XivAlexander::Misc::GameInstallationDetector::ResolveInstallation(const std::filesystem::path& root) {
	if (!IsRoot(root))
		throw std::runtime_error("Game installation not found");
	FoundInstallation found{.RootPath = root};
	found.Info = ReadRootInfo(root);
	found.GameDirId = GetDirectoryId(root / L"game");
	return found;
}

void XivAlexander::Misc::GameInstallationDetector::FindInstallations(const CollectOptions& options, const std::function<void(FoundInstallation)>& onFound, std::stop_token stop) {
	std::vector<FoundInstallation> roots;
	for (const auto& candidate : CollectCandidates(options, stop)) {
		if (stop.stop_requested())
			return;
		const auto root = FindRootStructurally(candidate.Path);
		if (!root)
			continue;

		FoundInstallation found{.RootPath = *root, .GameDirId = GetDirectoryId(*root / L"game")};
		auto it = std::ranges::find_if(roots, [&found](const auto& r) { return r.IsSameAs(found); });
		if (it == roots.end())
			it = roots.insert(roots.end(), std::move(found));
		it->Sources |= static_cast<uint32_t>(candidate.Source);
		if (std::ranges::find(it->Details, std::pair(candidate.Source, candidate.Detail)) == it->Details.end())
			it->Details.emplace_back(candidate.Source, candidate.Detail);
	}

	for (auto& root : roots) {
		if (stop.stop_requested())
			return;
		try {
			root.Info = ReadRootInfo(root.RootPath);
		} catch (...) {
			continue;  // vanished meanwhile
		}
		onFound(std::move(root));
	}
}

const XivAlexander::Misc::GameInstallationDetector::ManagedDll* XivAlexander::Misc::GameInstallationDetector::InstallState::Installed() const {
	for (const auto& dll : Dlls) {
		if (dll.Kind == ManagedDllKind::XivAlexander && !dll.Is32Bit && _wcsicmp(dll.Name.c_str(), L"d3d9.dll") != 0)
			return &dll;
	}
	return nullptr;
}

bool XivAlexander::Misc::GameInstallationDetector::InstallState::HasAnyXivAlexander() const {
	return std::ranges::any_of(Dlls, [](const auto& dll) { return dll.Kind == ManagedDllKind::XivAlexander; });
}

XivAlexander::Misc::GameInstallationDetector::InstallState XivAlexander::Misc::GameInstallationDetector::ProbeInstallState(const std::filesystem::path& gamePath) {
	InstallState state;
	for (const auto name : {L"d3d11.dll", L"dxgi.dll", L"dinput8.dll", L"d3d9.dll"}) {
		const auto path = gamePath / name;
		std::error_code ec;
		if (!std::filesystem::is_regular_file(path, ec))
			continue;
		const auto version = ReadVersionStrings(path);
		state.Dlls.emplace_back(ManagedDll{
			.Name = name,
			.Kind = Classify(version),
			.Is32Bit = IsPe32(path),
			.IsOldStyleXivAlexander = version.FileDescription == L"XivAlexander Main DLL",
			.Version = version.Version,
			.Product = version.ProductName.empty() ? version.FileDescription : version.ProductName,
		});
	}

	std::error_code ec;
	for (auto it = std::filesystem::directory_iterator(gamePath, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
		const auto name = it->path().filename().wstring();
		if (it->is_directory(ec) && StartsWithI(name, L"xivalex.chaindll.")) {
			std::error_code ec2;
			for (auto sub = std::filesystem::directory_iterator(it->path(), ec2); !ec2 && sub != std::filesystem::directory_iterator(); sub.increment(ec2)) {
				if (EndsWithI(sub->path().native(), L".dll"))
					state.ChainDlls.emplace_back(sub->path());
			}
		} else if (StartsWithI(name, L"_xivalex_temp_delete_me_") || (StartsWithI(name, L"_temp.") && EndsWithI(name, L".dll"))) {
			state.HasLeftoverTempFiles = true;
		}
	}
	return state;
}
