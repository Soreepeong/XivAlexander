#include "pch.h"
#include "App_LoaderApp_Actions_InstallUninstall.h"

#include <XivAlexanderCommon/Utils_Win32_TaskDialogBuilder.h>

#include "App_ConfigRepository.h"
#include "App_LoaderApp.h"
#include "DllMain.h"
#include "resource.h"

App::LoaderApp::Actions::InstallUninstall::InstallUninstall(const Arguments& args)
	: m_args(args) {
}

int App::LoaderApp::Actions::InstallUninstall::Run() {
	switch (m_args.m_action) {
		case XivAlexDll::LoaderAction::Install:
			Install(m_args.m_runProgram, m_args.m_installMode);
			return 0;
		case XivAlexDll::LoaderAction::Uninstall:
			Uninstall(m_args.m_runProgram);
			return 0;
		default:
			throw std::invalid_argument("invalid action for InstallUninstall");
	}
}

void App::LoaderApp::Actions::InstallUninstall::Install(const std::filesystem::path& gamePath, InstallMode installMode) {
	if (EnsureNoWow64Emulation())
		return;

	const auto cdir = Utils::Win32::Process::Current().PathOf().parent_path();
	const auto dataPath = Utils::Win32::EnsureKnownFolderPath(FOLDERID_RoamingAppData) / L"XivAlexander";
	const auto exePath = Utils::Win32::EnsureKnownFolderPath(FOLDERID_LocalAppData) / L"XivAlexander";
	create_directories(dataPath);
	create_directories(exePath);

	const auto configPath = dataPath / "config.runtime.json";
	auto config64 = Config::RuntimeRepository(nullptr, {}, Utils::ToUtf8((gamePath / "ffxiv_dx11.exe").wstring()));
	auto config32 = Config::RuntimeRepository(nullptr, {}, Utils::ToUtf8((gamePath / "ffxiv.exe").wstring()));
	config64.Reload(configPath);
	config32.Reload(configPath);

	auto success = false;
	Utils::CallOnDestruction::Multiple revert;

	const auto d3d11 = gamePath / "d3d11.dll";
	const auto d3d9 = gamePath / "d3d9.dll";
	const auto dxgi = gamePath / "dxgi.dll";
	const auto dinput8 = gamePath / "dinput8.dll";
	// * Don't use dxgi for now, as d3d11 overriders may choose to call system DLL directly,
	//   and XIVQuickLauncher warns something about broken GShade install.
	
	RevertChainLoadDlls(gamePath, success, revert, config64, config32);

	switch (installMode) {
		case InstallMode::D3D: {
			if (exists(d3d9)) {
				std::filesystem::path fn;
				for (size_t i = 0; ; i++) {
					fn = (d3d9.parent_path() / std::format("xivalex.chaindll.{}", i)) / "d3d9.dll";
					if (!exists(fn))
						break;
				}
				create_directories(fn.parent_path());
				rename(d3d9, fn);
				revert += [d3d9, fn, &success]() { if (!success) rename(fn, d3d9); };
				auto list = config32.ChainLoadPath_d3d9.Value();
				list.emplace_back(std::move(fn));
				config32.ChainLoadPath_d3d9 = list;
			}
			if (exists(d3d11)) {
				std::filesystem::path fn;
				for (size_t i = 0; ; i++) {
					fn = (d3d11.parent_path() / std::format("xivalex.chaindll.{}", i)) / "d3d11.dll";
					if (!exists(fn))
						break;
				}
				create_directories(fn.parent_path());
				rename(d3d11, fn);
				revert += [d3d11, fn, &success]() { if (!success) rename(fn, d3d11); };
				auto list = config64.ChainLoadPath_d3d11.Value();
				list.emplace_back(std::move(fn));
				config64.ChainLoadPath_d3d11 = list;
			}
			remove(gamePath / "config.xivalexinit.json");
			copy_file(cdir / XivAlexDll::XivAlexDll64NameW, d3d11);
			revert += [d3d11, &success]() { if (!success) remove(d3d11); };
			copy_file(cdir / XivAlexDll::XivAlexDll32NameW, d3d9);
			revert += [d3d9, &success]() { if (!success) remove(d3d9); };
			break;
		}
		case InstallMode::DInput8x86: {
			if (exists(dinput8)) {
				std::filesystem::path fn;
				for (size_t i = 0; ; i++) {
					fn = (dinput8.parent_path() / std::format("xivalex.chaindll.{}", i)) / "dinput8.dll";
					if (!exists(fn))
						break;
				}
				create_directories(fn.parent_path());
				rename(dinput8, fn);
				revert += [dinput8, fn, &success]() { if (!success) rename(fn, dinput8); };
				auto list = config32.ChainLoadPath_dinput8.Value();
				list.emplace_back(std::move(fn));
				config32.ChainLoadPath_dinput8 = list;
			}
			remove(gamePath / "config.xivalexinit.json");
			copy_file(cdir / XivAlexDll::XivAlexDll32NameW, dinput8);
			revert += [dinput8, &success]() { if (!success) remove(dinput8); };
			break;
		}
		case InstallMode::DInput8x64: {
			if (exists(dinput8)) {
				std::filesystem::path fn;
				for (size_t i = 0; ; i++) {
					fn = (dinput8.parent_path() / std::format("xivalex.chaindll.{}", i)) / "dinput8.dll";
					if (!exists(fn))
						break;
				}
				create_directories(fn.parent_path());
				rename(dinput8, fn);
				revert += [dinput8, fn, &success]() { if (!success) rename(fn, dinput8); };
				auto list = config64.ChainLoadPath_dinput8.Value();
				list.emplace_back(std::move(fn));
				config64.ChainLoadPath_dinput8 = list;
			}
			remove(gamePath / "config.xivalexinit.json");
			copy_file(cdir / XivAlexDll::XivAlexDll64NameW, dinput8);
			revert += [dinput8, &success]() { if (!success) remove(dinput8); };
			break;
		}
	}

	for (const auto& f : std::filesystem::directory_iterator(cdir)) {
		if (f.is_directory())
			continue;

		if (f.path().filename().wstring().starts_with(L"game.")
			&& f.path().filename().wstring().ends_with(L".json"))
			if (!exists(dataPath / f.path().filename()) || !equivalent(f, dataPath / f.path().filename()))
				copy_file(f, dataPath / f.path().filename(), std::filesystem::copy_options::overwrite_existing);

		if (f.path().filename().wstring().ends_with(L".exe")
			|| f.path().filename().wstring().ends_with(L".dll"))
			if (!exists(exePath / f.path().filename()) || !equivalent(f, exePath / f.path().filename()))
				copy_file(f, exePath / f.path().filename(), std::filesystem::copy_options::overwrite_existing);
	}

	config64.Save(configPath);
	config32.Save(configPath);

	success = true;
	revert.Clear();

	RemoveTemporaryFiles(gamePath);

	Dll::MessageBoxF(nullptr, MB_OK, IDS_NOTIFY_XIVALEXINSTALL_INSTALLCOMPLETE, gamePath.wstring());
}

void App::LoaderApp::Actions::InstallUninstall::Uninstall(const std::filesystem::path& gamePath) {
	if (EnsureNoWow64Emulation())
		return;

	const auto cdir = Utils::Win32::Process::Current().PathOf().parent_path();
	const auto dataPath = Utils::Win32::EnsureKnownFolderPath(FOLDERID_RoamingAppData) / L"XivAlexander";
	const auto exePath = Utils::Win32::EnsureKnownFolderPath(FOLDERID_LocalAppData) / L"XivAlexander";

	const auto configPath = dataPath / "config.runtime.json";
	auto config64 = Config::RuntimeRepository(nullptr, {}, Utils::ToUtf8((gamePath / "ffxiv_dx11.exe").wstring()));
	auto config32 = Config::RuntimeRepository(nullptr, {}, Utils::ToUtf8((gamePath / "ffxiv.exe").wstring()));
	config64.Reload(configPath);
	config32.Reload(configPath);

	auto success = false;
	Utils::CallOnDestruction::Multiple revert;
	
	RevertChainLoadDlls(gamePath, success, revert, config64, config32);

	config64.Save(configPath);
	config32.Save(configPath);

	success = true;
	revert.Clear();

	RemoveTemporaryFiles(gamePath);

	if (Dll::MessageBoxF(nullptr, MB_YESNO | MB_ICONQUESTION, IDS_NOTIFY_XIVALEXINSTALL_UNINSTALLCOMPLETE, gamePath.wstring()) == IDYES)
		Utils::Win32::ShellExecutePathOrThrow(dataPath);
}

static void QueueRemoval(const std::filesystem::path& path, const bool& success, Utils::CallOnDestruction::Multiple& revert) {
	std::filesystem::path temp;
	for (size_t i = 0; ; i++)
		if (!exists(temp = std::filesystem::path(path).replace_filename(std::format(L"_temp.{}.dll", i))))
			break;
	rename(path, temp);
	revert += [path, temp, &success]() {
		if (success) {
			try {
				remove(temp);
			} catch (...) {
				std::filesystem::path temp2;
				for (size_t i = 0; ; i++)
					if (!exists(temp2 = std::filesystem::path(path).replace_filename(std::format(L"_xivalex_temp_delete_me_{}.tmp", i))))
						break;
				rename(temp, temp2);
			}
		} else
			rename(temp, path);
	};
}

static std::string GetSha1(const std::filesystem::path& f) {
	uint8_t hash[20]{};
	{
		const auto file = Utils::Win32::Handle::FromCreateFile(f, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
		CryptoPP::SHA1 sha1;
		std::vector<uint8_t> buf(8192);
		for (uint64_t i = 0, len = file.GetFileSize(); i < len; i += 8192) {
			const auto read = file.Read(i, &buf[0], static_cast<size_t>(std::min<uint64_t>(len - i, buf.size())));
			sha1.Update(buf.data(), read);
		}

		sha1.Final(hash);
	}
	return std::string(reinterpret_cast<const char*>(hash), sizeof hash);
}

void App::LoaderApp::Actions::InstallUninstall::RevertChainLoadDlls(
	const std::filesystem::path& gamePath,
	const bool& success,
	Utils::CallOnDestruction::Multiple& revert,
	Config::RuntimeRepository& config64, Config::RuntimeRepository& config32
) {
	static const auto ReplaceableDinput8FkMod = std::string("\x9F\xFC\x74\xAE\x3F\xB4\x25\x97\x48\x78\x5B\xB6\x8E\xF2\x8C\xA6\xDC\xD6\xFE\x1F", 20);
	const auto d3d11 = gamePath / "d3d11.dll";
	const auto d3d9 = gamePath / "d3d9.dll";
	const auto dxgi = gamePath / "dxgi.dll";
	const auto dinput8 = gamePath / "dinput8.dll";
	
	for (const auto& f : { d3d9, d3d11, dxgi, dinput8 }) {
		if (!exists(f))
			continue;

		const auto hash = GetSha1(f);

		if (XivAlexDll::IsXivAlexanderDll(f)) {
			QueueRemoval(f, success, revert);
		} else if (ReplaceableDinput8FkMod == hash) {
			const auto res = Utils::Win32::TaskDialog::Builder()
				.WithWindowTitle(Dll::GetGenericMessageBoxTitle())
				.WithInstance(Dll::Module())
				.WithAllowDialogCancellation()
				.WithCanBeMinimized()
				.WithHyperlinkShellExecute()
				.WithMainIcon(IDI_TRAY_ICON)
				.WithMainInstruction(L"기존 한글 패치 제거")
				.WithContent(
					// Only native Korean speakers use this patch, so hardcoding this
					L"기존 dinput8.dll을 이용하는 한글 패치가 감지되었습니다. XivAlexander를 설치함과 동시에 기존 패치를 제거할까요?\n"
					LR"(XivAlexander를 통해서도 게임 내에서 한글을 이용할 수 있습니다. <a href="https://github.com/Soreepeong/XivAlexander/wiki/%ED%8F%B0%ED%8A%B8-%EA%B5%90%EC%B2%B4-%EC%84%A4%EC%A0%95">폰트 교체 설정</a> 위키 페이지를 참조하세요.)"
					L"\n\n"
					L"Korean font patch using dinput8.dll has been detected. Do you want to remove that when XivAlexander installs successfully?\n"
					LR"(You can use Korean text using XivAlexander too. See <a href="https://github.com/Soreepeong/XivAlexander/wiki/Set-up-font-replacement">Set up font replacement</a> wiki page for how.)"
				)
				.WithCommonButton(TDCBF_OK_BUTTON)
				.WithCommonButton(TDCBF_CANCEL_BUTTON)
				.WithButtonDefault(TDCBF_OK_BUTTON)
				.Build()
				.Show();
			if (res.Button == IDCANCEL)
				continue;

			static const char* ChainDeleteHashes[][2]{
				{ "data/common/font/font_krn_1.tex", "\x28\xA2\xD9\x77\x3A\xC1\x4A\xD9\x48\xAD\xD9\xC7\x1E\xB0\x90\x77\x4B\x60\xA7\x8E" },
				{ "data/common/font/font_krn_2.tex", "\x66\x8A\x6E\xB4\x39\x4B\x46\x33\xD3\x00\xA4\x66\xBD\xCC\xC3\xE9\xED\x1E\xCC\x6F" },
				{ "data/common/font/font_krn_3.tex", "\x21\x68\x7B\x3F\x6D\xF4\x5B\x23\xF3\xC9\xA7\x28\xB7\x26\x09\xF4\x49\x6E\x9E\xFE" },
				{ "data/common/font/KrnAXIS_120.fdt", "\xEE\xE1\x83\xD3\x7E\xE1\xD4\xA2\x36\xFC\xCB\x0D\xB9\xEC\x40\x29\xCE\x4D\x40\x1B" },
				{ "data/common/font/KrnAXIS_140.fdt", "\x8E\x38\x1A\x1D\x75\x0B\x12\x9F\x40\x4F\xE8\xEF\xA0\x6C\xA2\xEC\x47\xF7\x1D\x93" },
				{ "data/common/font/KrnAXIS_180.fdt", "\x4B\xEE\x04\x00\x07\xBF\x6F\x88\x9A\xF4\xFA\x9E\x09\x00\xF4\xF2\xDE\xC7\x73\x00" },
			};

			// This comes first, as Utils::CallOnDestruction::Multiple is FILO (stack).
			revert += [&success, gamePath]() {
				if (!success)
					return;
				try {
					for (auto path = gamePath / "data" / "common" / "font";
						path != gamePath && path.parent_path() != path;
						path = path.parent_path()) {
						if (is_directory(path) && is_empty(path))
							remove(path);
					}
				} catch (...) {
					// courtesy removal of old files can fail silently
				}
			};

			for (const auto [checkFile, checkFileHash] : ChainDeleteHashes) {
				if (!exists(gamePath / checkFile))
					continue;
				if (GetSha1(gamePath / checkFile) != std::string_view(checkFileHash, 20))
					continue;
				QueueRemoval(gamePath / checkFile, success, revert);
			}
			QueueRemoval(f, success, revert);
			continue;
		}
	}

	if (!exists(d3d9) && config32.ChainLoadPath_d3d9.Value().size() == 1) {
		std::filesystem::path fn = config32.ChainLoadPath_d3d9.Value().back();
		rename(fn, d3d9);
		revert += [d3d9, fn, &success]() { if (!success) rename(d3d9, fn); };
		config32.ChainLoadPath_d3d9 = std::vector<std::filesystem::path>();
	}
	if (!exists(d3d11) && config64.ChainLoadPath_d3d11.Value().size() == 1) {
		std::filesystem::path fn = config64.ChainLoadPath_d3d11.Value().back();
		rename(fn, d3d11);
		revert += [d3d11, fn, &success]() { if (!success) rename(d3d11, fn); };
		config64.ChainLoadPath_d3d11 = std::vector<std::filesystem::path>();
	}
	if (!exists(dinput8) && config32.ChainLoadPath_dinput8.Value().size() == 1) {
		std::filesystem::path fn = config32.ChainLoadPath_dinput8.Value().back();
		rename(fn, dinput8);
		revert += [dinput8, fn, &success]() { if (!success) rename(dinput8, fn); };
		config32.ChainLoadPath_dinput8 = std::vector<std::filesystem::path>();
	}
	if (!exists(dinput8) && config64.ChainLoadPath_dinput8.Value().size() == 1) {
		std::filesystem::path fn = config64.ChainLoadPath_dinput8.Value().back();
		rename(fn, dinput8);
		revert += [dinput8, fn, &success]() { if (!success) rename(dinput8, fn); };
		config64.ChainLoadPath_dinput8 = std::vector<std::filesystem::path>();
	}
}

void App::LoaderApp::Actions::InstallUninstall::RemoveTemporaryFiles(const std::filesystem::path& gamePath) {
	for (const auto& item : std::filesystem::directory_iterator(gamePath)) {
		try {
			if (item.is_directory() && item.path().filename().wstring().starts_with(L"xivalex.chaindll.")) {
				auto anyFile = false;
				for (const auto& _ : std::filesystem::directory_iterator(item)) {
					anyFile = true;
					break;
				}
				if (!anyFile)
					remove(item);
			} else if (item.path().filename().wstring().starts_with(L"_xivalex_temp_delete_me_")) {
				remove_all(item);
			}
		} catch (...) {
			// pass
		}
	}
}
