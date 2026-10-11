#include "pch.h"
#include "InstallUninstall.h"

#include "Utils/Win32/Resource.h"

#include "LoaderApp/App.h"
#include "Config.h"
#include "resource.h"
#include "XivAlexander.h"

namespace {
	constexpr const wchar_t* ManagedDllNames[]{L"d3d11.dll", L"dxgi.dll", L"dinput8.dll", L"d3d9.dll"};

	std::filesystem::path NormalizeGamePath(std::filesystem::path path) {
		if (!exists(path / Dll::GameExecutable64NameW) && exists(path / L"game" / Dll::GameExecutable64NameW))
			path /= L"game";
		if (!exists(path / Dll::GameExecutable64NameW)) {
			const auto pathString = path.wstring();
			throw std::runtime_error(xivres::util::unicode::convert<std::string>(std::vformat(
				Utils::Win32::FindStringResourceEx(Dll::Module(), IDS_LOADER_ERROR_NOT_GAME_FOLDER) + 1, std::make_wformat_args(pathString))));
		}
		return path;
	}

	void QueueRemoval(const std::filesystem::path& path, const bool& success, xivres::util::on_dtor::multi& revert) {
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

	// Whether a ChainLoad entry is a DLL that install moved aside, i.e. <game>\xivalex.chaindll.N\<slot name>.
	bool IsMovedAsideChainDll(const std::filesystem::path& entry, const std::filesystem::path& gamePath, const std::filesystem::path& slotName) {
		const auto full = Utils::Win32::TranslatePath(entry, gamePath);
		if (_wcsicmp(full.filename().c_str(), slotName.c_str()) != 0)
			return false;
		if (_wcsnicmp(full.parent_path().filename().c_str(), L"xivalex.chaindll.", 17) != 0)
			return false;
		std::error_code ec;
		return equivalent(full.parent_path().parent_path(), gamePath, ec) && !ec;
	}
}

XivAlexander::LoaderApp::Actions::InstallUninstall::Paths XivAlexander::LoaderApp::Actions::InstallUninstall::Paths::ForCurrentUser() {
	return {
		.ConfigDir = Utils::Win32::EnsureKnownFolderPath(FOLDERID_RoamingAppData) / L"XivAlexander",
		.ProgramDir = Utils::Win32::EnsureKnownFolderPath(FOLDERID_LocalAppData) / L"XivAlexander",
		.SourceDir = Utils::Win32::Process::Current().PathOf().parent_path(),
	};
}

XivAlexander::LoaderApp::Actions::InstallUninstall::Paths XivAlexander::LoaderApp::Actions::InstallUninstall::Paths::FromArguments(const Arguments& args) {
	auto paths = ForCurrentUser();
	if (!args.m_configDir.empty())
		paths.ConfigDir = args.m_configDir;
	if (!args.m_programDir.empty())
		paths.ProgramDir = args.m_programDir;
	return paths;
}

XivAlexander::LoaderApp::Actions::InstallUninstall::InstallUninstall(const Arguments& args)
	: m_args(args) {
}

int XivAlexander::LoaderApp::Actions::InstallUninstall::Run() {
	const auto paths = Paths::FromArguments(m_args);
	const auto install = m_args.m_action == Dll::LoaderAction::Install;
	if (!install && m_args.m_action != Dll::LoaderAction::Uninstall)
		throw std::invalid_argument("invalid action for InstallUninstall");

	const auto gamePath = NormalizeGamePath(m_args.m_runProgram);
	if (m_args.m_gameFilesOnly) {
		if (install)
			InstallGameFiles(gamePath, m_args.m_installMode, paths);
		else
			UninstallGameFiles(gamePath, paths);
		return 0;
	}

	try {
		if (!(install ? Install(gamePath, m_args.m_installMode, paths, !m_args.m_disableAutoRunAs) : Uninstall(gamePath, paths, !m_args.m_disableAutoRunAs)))
			return ERROR_CANCELLED;
	} catch (const Utils::Win32::CancelledError&) {
		return -1;
	}

	if (!m_args.m_quiet) {
		if (install)
			Dll::MessageBoxF(nullptr, MB_OK, IDS_NOTIFY_XIVALEXINSTALL_INSTALLCOMPLETE, gamePath.wstring());
		else if (Dll::MessageBoxF(nullptr, MB_YESNO | MB_ICONQUESTION, IDS_NOTIFY_XIVALEXINSTALL_UNINSTALLCOMPLETE, gamePath.wstring()) == IDYES)
			Utils::Win32::ShellExecutePathOrThrow(paths.ConfigDir);
	}
	return 0;
}

bool XivAlexander::LoaderApp::Actions::InstallUninstall::Install(const std::filesystem::path& gamePath, InstallMode installMode, const Paths& paths, bool allowElevation) {
	// The game folder stub looks for the program files, so those go first.
	InstallProgramFiles(paths);
	return RunGameFilesPart(true, NormalizeGamePath(gamePath), installMode, paths, allowElevation);
}

bool XivAlexander::LoaderApp::Actions::InstallUninstall::Uninstall(const std::filesystem::path& gamePath, const Paths& paths, bool allowElevation) {
	return RunGameFilesPart(false, NormalizeGamePath(gamePath), InstallMode::D3D, paths, allowElevation);
}

bool XivAlexander::LoaderApp::Actions::InstallUninstall::RunGameFilesPart(bool install, const std::filesystem::path& gamePath, InstallMode installMode, const Paths& paths, bool allowElevation) {
	if (!allowElevation || Utils::Win32::IsUserAnAdmin() || IsDirectoryWritable(gamePath)) {
		if (install)
			InstallGameFiles(gamePath, installMode, paths);
		else
			UninstallGameFiles(gamePath, paths);
		return true;
	}

	const std::vector<std::wstring> args{
		L"--disable-runas",
		L"--game-files-only",
		L"--config-dir", paths.ConfigDir.wstring(),
		L"--program-dir", paths.ProgramDir.wstring(),
		L"--action", xivres::util::unicode::convert<std::wstring>(Dll::LoaderActionToString(install ? Dll::LoaderAction::Install : Dll::LoaderAction::Uninstall)),
		L"--install-mode", xivres::util::unicode::convert<std::wstring>(argparse::details::repr(installMode)),
		gamePath.wstring(),
	};
	const auto process = Utils::Win32::RunProgram({
		.args = Utils::Win32::ReverseCommandLineToArgv(std::span(args)),
		.elevateMode = Utils::Win32::RunProgramParams::Force,
	});
	if (!process)
		return false;

	// The child reports its own errors.
	if (const auto exitCode = process.WaitAndGetExitCode(); exitCode != 0)
		throw Utils::Win32::CancelledError();
	return true;
}

bool XivAlexander::LoaderApp::Actions::InstallUninstall::IsDirectoryWritable(const std::filesystem::path& dir) {
	const auto testPath = dir / std::format(L".xivalex_write_test_{}", GetCurrentProcessId());
	const auto hFile = CreateFileW(testPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;
	CloseHandle(hFile);
	return true;
}

const wchar_t* XivAlexander::LoaderApp::Actions::InstallUninstall::SlotNameOf(InstallMode installMode) {
	switch (installMode) {
		case InstallMode::DInput8: return L"dinput8.dll";
		case InstallMode::Dxgi: return L"dxgi.dll";
		default: return L"d3d11.dll";
	}
}

void XivAlexander::LoaderApp::Actions::InstallUninstall::InstallProgramFiles(const Paths& paths) {
	create_directories(paths.ConfigDir);
	create_directories(paths.ProgramDir);
	remove(paths.ConfigDir / "config.xivalexinit.json");
	remove(paths.ProgramDir / "config.xivalexinit.json");

	for (const auto& f : std::filesystem::directory_iterator(paths.SourceDir)) {
		if (f.is_directory())
			continue;

		const auto name = f.path().filename().wstring();
		std::filesystem::path target;
		if (name.starts_with(L"game.") && name.ends_with(L".json"))
			target = paths.ConfigDir / f.path().filename();
		else if (name.ends_with(L".exe") || name.ends_with(L".dll"))
			target = paths.ProgramDir / f.path().filename();
		else
			continue;

		if (std::error_code ec; !exists(target) || !equivalent(f, target, ec))
			copy_file(f, target, std::filesystem::copy_options::overwrite_existing);
	}
}

void XivAlexander::LoaderApp::Actions::InstallUninstall::InstallGameFiles(const std::filesystem::path& gamePath, InstallMode installMode, const Paths& paths) {
	create_directories(paths.ConfigDir);
	const auto configPath = paths.ConfigDir / "config.runtime.json";
	auto config64 = RuntimeConfigRepository({}, xivres::util::unicode::convert<std::string>((gamePath / Dll::GameExecutable64NameW).wstring()));
	config64.Reload(configPath);

	auto success = false;
	xivres::util::on_dtor::multi revert;

	RevertChainLoadDlls(gamePath, success, revert, config64);

	const auto slotName = std::filesystem::path(SlotNameOf(installMode));
	auto& chainLoad = installMode == InstallMode::DInput8 ? config64.ChainLoad.Dinput8 : installMode == InstallMode::Dxgi ? config64.ChainLoad.Dxgi : config64.ChainLoad.D3d11;
	const auto slot = gamePath / slotName;
	if (exists(slot)) {
		std::filesystem::path fn;
		for (size_t i = 0; ; i++) {
			fn = gamePath / std::format("xivalex.chaindll.{}", i) / slotName;
			if (!exists(fn))
				break;
		}
		create_directories(fn.parent_path());
		rename(slot, fn);
		revert += [slot, fn, &success]() { if (!success) rename(fn, slot); };
		auto list = chainLoad.Value();
		list.emplace_back(std::move(fn));
		chainLoad = list;
	}
	remove(gamePath / "config.xivalexinit.json");
	copy_file(paths.SourceDir / Dll::XivAlexLoaderDll64NameW, slot);
	revert += [slot, &success]() { if (!success) remove(slot); };

	config64.Save(configPath);

	success = true;
	revert.clear();

	RemoveTemporaryFiles(gamePath);
}

void XivAlexander::LoaderApp::Actions::InstallUninstall::UninstallGameFiles(const std::filesystem::path& gamePath, const Paths& paths) {
	const auto configPath = paths.ConfigDir / "config.runtime.json";
	auto config64 = RuntimeConfigRepository({}, xivres::util::unicode::convert<std::string>((gamePath / Dll::GameExecutable64NameW).wstring()));
	config64.Reload(configPath);

	auto success = false;
	xivres::util::on_dtor::multi revert;

	RevertChainLoadDlls(gamePath, success, revert, config64);

	if (exists(configPath) || exists(paths.ConfigDir))
		config64.Save(configPath);

	success = true;
	revert.clear();

	RemoveTemporaryFiles(gamePath);
}

void XivAlexander::LoaderApp::Actions::InstallUninstall::RevertChainLoadDlls(
	const std::filesystem::path& gamePath,
	const bool& success,
	xivres::util::on_dtor::multi& revert,
	RuntimeConfigRepository& config64
) {
	for (const auto name : ManagedDllNames) {
		const auto f = gamePath / name;
		try {
			if (exists(f) && Dll::IsXivAlexanderDll(f))
				QueueRemoval(f, success, revert);
		} catch (const Utils::Win32::Error&) {
			// not a PE file with version information; not ours
		}
	}

	// Only DLLs that install moved aside go back; a ChainLoad entry the user set elsewhere stays where it is.
	const auto restore = [&](const wchar_t* slotName, ConfigItem<std::vector<std::filesystem::path>>& item) {
		const auto slot = gamePath / slotName;
		auto list = item.Value();
		const auto original = list;
		if (!exists(slot)) {
			for (auto it = list.rbegin(); it != list.rend(); ++it) {
				if (!IsMovedAsideChainDll(*it, gamePath, slotName))
					continue;
				const auto full = Utils::Win32::TranslatePath(*it, gamePath);
				if (!exists(full))
					continue;
				rename(full, slot);
				revert += [slot, full, &success]() { if (!success) rename(slot, full); };
				list.erase(std::next(it).base());
				break;
			}
		}
		std::erase_if(list, [&](const auto& entry) {
			return IsMovedAsideChainDll(entry, gamePath, slotName) && !exists(Utils::Win32::TranslatePath(entry, gamePath));
		});
		if (list != original)
			item = list;
	};
	restore(L"d3d11.dll", config64.ChainLoad.D3d11);
	restore(L"dinput8.dll", config64.ChainLoad.Dinput8);
	restore(L"dxgi.dll", config64.ChainLoad.Dxgi);
}

void XivAlexander::LoaderApp::Actions::InstallUninstall::RemoveTemporaryFiles(const std::filesystem::path& gamePath) {
	for (const auto& item : std::filesystem::directory_iterator(gamePath)) {
		try {
			if (item.is_directory() && item.path().filename().wstring().starts_with(L"xivalex.chaindll.")) {
				if (std::filesystem::is_empty(item))
					remove(item);
			} else if (item.path().filename().wstring().starts_with(L"_xivalex_temp_delete_me_")) {
				remove_all(item);
			}
		} catch (...) {
		}
	}
}
