#include "pch.h"
#include "App_LoaderApp_Actions_InstallUninstall.h"

#include "App_ConfigRepository.h"
#include "App_LoaderApp.h"
#include "DllMain.h"
#include "resource.h"
#include "XivAlexanderCommon/XivAlex.h"

App::LoaderApp::Actions::InstallUninstall::InstallUninstall(const Arguments& args)
	: m_args(args) {
}

int App::LoaderApp::Actions::InstallUninstall::Run() {
	switch (m_args.m_action) {
		case XivAlexDll::LoaderAction::Install:
			Install(m_args.m_runProgram);
			return 0;
		case XivAlexDll::LoaderAction::Uninstall:
			Uninstall(m_args.m_runProgram);
			return 0;
		default:
			throw std::invalid_argument("invalid action for InstallUninstall");
	}
}

void App::LoaderApp::Actions::InstallUninstall::Install(const std::filesystem::path& gamePath) {
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
	// * dinput8.dll is for manual troubleshooting for now.

	for (const auto& f : {d3d9, d3d11, dxgi, dinput8}) {
		if (!exists(f) || !XivAlex::IsXivAlexanderDll(f))
			continue;

		std::filesystem::path temp;
		for (size_t i = 0; ; i++)
			if (!exists(temp = std::filesystem::path(f).replace_filename(std::format(L"_temp.{}.dll", i))))
				break;
		rename(f, temp);
		revert += [f, temp, &success]() {
			if (success)
				remove(temp);
			else
				rename(temp, f);
		};
	}

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
	copy_file(cdir / XivAlex::XivAlexDll64NameW, d3d11);
	revert += [d3d11, &success]() { if (!success) remove(d3d11); };
	copy_file(cdir / XivAlex::XivAlexDll32NameW, d3d9);
	revert += [d3d9, &success]() { if (!success) remove(d3d9); };

	for (const auto& f : std::filesystem::directory_iterator(cdir)) {
		if (f.is_directory())
			continue;

		if (f.path().filename().wstring().starts_with(L"game.")
			&& f.path().filename().wstring().ends_with(L".json"))
			copy_file(f, dataPath / f.path().filename(), std::filesystem::copy_options::overwrite_existing);

		if (f.path().filename().wstring().ends_with(L".exe")
			|| f.path().filename().wstring().ends_with(L".dll"))
			copy_file(f, exePath / f.path().filename(), std::filesystem::copy_options::overwrite_existing);
	}

	config64.Save(configPath);
	config32.Save(configPath);

	success = true;
	revert.Clear();

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

	const auto d3d11 = gamePath / "d3d11.dll";
	const auto d3d9 = gamePath / "d3d9.dll";
	const auto dxgi = gamePath / "dxgi.dll";
	const auto dinput8 = gamePath / "dinput8.dll";

	for (const auto& f : {d3d9, d3d11, dxgi, dinput8}) {
		if (!exists(f) || !XivAlex::IsXivAlexanderDll(f))
			continue;

		std::filesystem::path temp;
		for (size_t i = 0; ; i++)
			if (!exists(temp = std::filesystem::path(f).replace_filename(std::format(L"_temp.{}.dll", i))))
				break;
		rename(f, temp);
		revert += [f, temp, &success]() {
			if (success)
				remove(temp);
			else
				rename(temp, f);
		};
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
			}
		} catch (...) {
			// pass
		}
	}

	config64.Save(configPath);
	config32.Save(configPath);

	success = true;
	revert.Clear();

	if (Dll::MessageBoxF(nullptr, MB_YESNO | MB_ICONQUESTION, IDS_NOTIFY_XIVALEXINSTALL_UNINSTALLCOMPLETE, gamePath.wstring()) == IDYES)
		ShellExecutePathOrThrow(dataPath);
}
