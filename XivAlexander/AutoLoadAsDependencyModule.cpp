#include "pch.h"

#define DIRECTINPUT_VERSION 0x0800  // NOLINT(cppcoreguidelines-macro-usage)

#include <dinput.h>
#include <XivAlexander/XivAlexander.h>
#include <XivAlexanderCommon/Sqex_CommandLine.h>
#include <XivAlexanderCommon/Utils_Win32.h>
#include <XivAlexanderCommon/Utils_Win32_Resource.h>
#include <XivAlexanderCommon/XivAlex.h>

#include "App_ConfigRepository.h"
#include "DllMain.h"
#include "resource.h"

static WORD s_wLanguage = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);

static Utils::Win32::LoadedModule EnsureOriginalDependencyModule(const char* szDllName, std::filesystem::path originalDllPath);
static void AutoLoadAsDependencyModule();

static std::set<std::filesystem::path> s_ignoreDlls;
static bool s_useSystemDll = false;

template<typename T_Fn, typename Ret>
Ret ChainCall(const char* szDllName, const char* szFunctionName, std::vector<std::filesystem::path> chainLoadDlls, std::function<Ret(T_Fn, bool discardImmediately)> cb) {
	AutoLoadAsDependencyModule();

	const auto systemDll = Utils::Win32::GetSystem32Path() / szDllName;

	if (s_useSystemDll) {
		chainLoadDlls.clear();
	} else {
		std::vector<std::filesystem::path> temp;
		for (auto& path : temp)
			if (!path.empty())
				temp.emplace_back(App::Config::Config::TranslatePath(path));
		chainLoadDlls = std::move(temp);
	}

	if (chainLoadDlls.empty())
		chainLoadDlls.emplace_back(systemDll);
	
	for (size_t i = 0; i < chainLoadDlls.size(); ++i) {
		const auto& dll = chainLoadDlls[i];
		const auto isLast = i == chainLoadDlls.size() - 1;

		if (s_ignoreDlls.find(dll) != s_ignoreDlls.end())
			continue;

		try {
			const auto mod = Utils::Win32::LoadedModule(dll);
			const auto pOriginalFunction =
				EnsureOriginalDependencyModule(szDllName, dll).GetProcAddress<T_Fn>(szFunctionName, true);

			mod.SetPinned();
			
			if (isLast)
				return cb(pOriginalFunction, !isLast);
			else
				cb(pOriginalFunction, !isLast);

		} catch (const std::runtime_error& e) {
			const auto activationContextCleanup = Dll::ActivationContext().With();
			const auto choice = Utils::Win32::MessageBoxF(
				Dll::FindGameMainWindow(false), MB_ICONWARNING | MB_ABORTRETRYIGNORE,
				FindStringResourceEx(Dll::Module(), IDS_APP_NAME, s_wLanguage) + 1,
				L"Failed to load {}.\nReason: {}\n\nPress Abort to exit.\nPress Retry to keep on loading.\nPress Ignore to skip right ahead to system DLL.",
				dll, e.what());
			switch (choice) {
				case IDRETRY:
					s_ignoreDlls.insert(dll);
					return ChainCall(szDllName, szFunctionName, std::move(chainLoadDlls), cb);

				case IDIGNORE:
					s_useSystemDll = true;
					chainLoadDlls = { systemDll };
					i = static_cast<size_t>(-1);
					break;

				case IDABORT:
					TerminateProcess(GetCurrentProcess(), -1);
			}
		}

		if (isLast && std::find(chainLoadDlls.begin(), chainLoadDlls.end(), systemDll) == chainLoadDlls.end())
			chainLoadDlls.emplace_back(systemDll);
	}
	
	const auto activationContextCleanup = Dll::ActivationContext().With();
	Utils::Win32::MessageBoxF(
		Dll::FindGameMainWindow(false), MB_ICONERROR,
		FindStringResourceEx(Dll::Module(), IDS_APP_NAME, s_wLanguage) + 1,
		L"Failed to load any of the possible {}. Aborting.", szDllName);
	TerminateProcess(GetCurrentProcess(), -1);
	ExitProcess(-1);  // Mark noreturn
}

#if INTPTR_MAX == INT64_MAX

#include <d3d11.h>
#include <dxgi1_3.h>

HRESULT WINAPI FORWARDER_D3D11CreateDevice(
	IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	const D3D_FEATURE_LEVEL* pFeatureLevels,
	UINT FeatureLevels,
	UINT SDKVersion,
	ID3D11Device** ppDevice,
	D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext
) {
	return ChainCall<decltype(&D3D11CreateDevice), HRESULT>("d3d11.dll", "D3D11CreateDevice", App::Config::Acquire()->Runtime.ChainLoadPath_d3d11.Value(), [&](auto pfn, auto discardImmediately) {
		const auto res = pfn(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion, ppDevice, pFeatureLevel, ppImmediateContext);
		if (res == S_OK && discardImmediately) {
			if (ppDevice)
				(*ppDevice)->Release();
			if (ppImmediateContext)
				(*ppImmediateContext)->Release();
		}
		return res;
	});
}

HRESULT WINAPI FORWARDER_CreateDXGIFactory(
	REFIID riid,
	IDXGIFactory** ppFactory
) {
	return ChainCall<decltype(&CreateDXGIFactory), HRESULT>("dxgi.dll", "CreateDXGIFactory", App::Config::Acquire()->Runtime.ChainLoadPath_dxgi.Value(), [&](auto pfn, auto discardImmediately) {
		const auto res = pfn(riid, reinterpret_cast<void**>(ppFactory));
		if (res == S_OK && discardImmediately) {
			if (ppFactory)
				(*ppFactory)->Release();
		}
		return res;
	});
}

HRESULT WINAPI FORWARDER_CreateDXGIFactory1(
	REFIID riid,
	IDXGIFactory1** ppFactory
) {
	return ChainCall<decltype(&CreateDXGIFactory1), HRESULT>("dxgi.dll", "CreateDXGIFactory1", App::Config::Acquire()->Runtime.ChainLoadPath_dxgi.Value(), [&](auto pfn, auto discardImmediately) {
		const auto res = pfn(riid, reinterpret_cast<void**>(ppFactory));
		if (res == S_OK && discardImmediately) {
			if (ppFactory)
				(*ppFactory)->Release();
		}
		return res;
	});
}

HRESULT WINAPI FORWARDER_CreateDXGIFactory2(
	UINT   Flags,
	REFIID riid,
	IDXGIFactory2** ppFactory
) {
	return ChainCall<decltype(&CreateDXGIFactory2), HRESULT>("dxgi.dll", "CreateDXGIFactory1", App::Config::Acquire()->Runtime.ChainLoadPath_dxgi.Value(), [&](auto pfn, auto discardImmediately) {
		const auto res = pfn(Flags, riid, reinterpret_cast<void**>(ppFactory));
		if (res == S_OK && discardImmediately) {
			if (ppFactory)
				(*ppFactory)->Release();
		}
		return res;
	});
}

#elif INTPTR_MAX == INT32_MAX

#include <d3d9.h>

void WINAPI FORWARDER_D3DPERF_SetOptions(DWORD dwOptions) {
	return ChainCall<decltype(&D3DPERF_SetOptions), void>("d3d9.dll", "D3DPERF_SetOptions", App::Config::Acquire()->Runtime.ChainLoadPath_d3d9.Value(), [&](auto pfn, auto discardImmediately) {
		pfn(dwOptions);
	});
}

IDirect3D9* WINAPI FORWARDER_Direct3DCreate9(UINT SDKVersion) {
	return ChainCall<decltype(&Direct3DCreate9), IDirect3D9*>("d3d9.dll", "Direct3DCreate9", App::Config::Acquire()->Runtime.ChainLoadPath_d3d9.Value(), [&](auto pfn, auto discardImmediately) {
		const auto res = pfn(SDKVersion);
		if (res && discardImmediately)
			res->Release();
		return res;
	});
}

#endif

HRESULT WINAPI FORWARDER_DirectInput8Create(
	HINSTANCE hinst,
	DWORD dwVersion,
	REFIID riidltf,
	IUnknown** ppvOut,
	LPUNKNOWN punkOuter
) {
	return ChainCall<decltype(&DirectInput8Create), HRESULT>("dinput8.dll", "DirectInput8Create", App::Config::Acquire()->Runtime.ChainLoadPath_dinput8.Value(), [&](auto pfn, auto discardImmediately) {
		const auto res = pfn(hinst, dwVersion, riidltf, reinterpret_cast<void**>(ppvOut), punkOuter);
		if (res == S_OK && discardImmediately) {
			if (ppvOut)
				(*ppvOut)->Release();
		}
		return res;
	});
}

static Utils::Win32::LoadedModule EnsureOriginalDependencyModule(const char* szDllName, std::filesystem::path originalDllPath) {
	static std::mutex preventDuplicateLoad;
	std::lock_guard lock(preventDuplicateLoad);

	if (originalDllPath.empty())
		originalDllPath = Utils::Win32::GetSystem32Path() / szDllName;

	auto mod = Utils::Win32::LoadedModule(originalDllPath);
	mod.SetPinned();
	return mod;
}

// void LoadDalamud();

void AutoLoadAsDependencyModule() {
	static std::mutex s_singleRunMutex;
	static bool s_loaded = false;

	if (s_loaded)
		return;

	std::lock_guard lock(s_singleRunMutex);
	if (s_loaded)
		return;

	Dll::DisableUnloading(std::format("Loaded as DLL dependency in place of {}", Dll::Module().PathOf().filename()).c_str());

	std::filesystem::path loadPath;
	try {
		std::shared_ptr<App::Config> conf = App::Config::Acquire();
		loadPath = conf->Init.ResolveXivAlexInstallationPath() / XivAlex::XivAlexDllNameW;
		const auto loadTarget = Utils::Win32::LoadedModule(loadPath);
		const auto params = XivAlexDll::PatchEntryPointForInjection(GetCurrentProcess());
		params->SkipFree = true;
		loadTarget.SetPinned();
		loadTarget.GetProcAddress<decltype(&XivAlexDll::SetLoadedAsDependency)>("XA_SetLoadedAsDependency")(nullptr);
		loadTarget.GetProcAddress<decltype(&XivAlexDll::InjectEntryPoint)>("XA_InjectEntryPoint")(params);

	} catch (const std::runtime_error& e) {
		const auto activationContextCleanup = Dll::ActivationContext().With();
		auto loop = true;
		while (loop) {
			const auto choice = Utils::Win32::MessageBoxF(
				Dll::FindGameMainWindow(false), MB_ICONWARNING | MB_ABORTRETRYIGNORE,
				FindStringResourceEx(Dll::Module(), IDS_APP_NAME, s_wLanguage) + 1,
				L"{}\nReason: {}\n\nPress Abort to exit.\nPress Retry to open XivAlexander help webpage.\nPress Ignore to skip loading XivAlexander.",
				loadPath.empty() ? L"Failed to resolve XivAlexander installation path." : std::format(L"Failed to load {}.", loadPath.wstring()),
				e.what());
			switch (choice) {
				case IDRETRY:
				{
					SHELLEXECUTEINFOW shex{};
					shex.cbSize = sizeof shex;
					shex.nShow = SW_SHOW;
					shex.lpFile = FindStringResourceEx(Dll::Module(), IDS_URL_HELP, s_wLanguage) + 1;
					if (!ShellExecuteExW(&shex)) {
						Utils::Win32::MessageBoxF(
							Dll::FindGameMainWindow(false), MB_OK | MB_ICONERROR,
							FindStringResourceEx(Dll::Module(), IDS_APP_NAME, s_wLanguage) + 1,
							std::format(FindStringResourceEx(Dll::Module(), IDS_ERROR_UNEXPECTED, s_wLanguage) + 1, Utils::Win32::FormatWindowsErrorMessage(GetLastError())));
					}
					break;
				}
				case IDIGNORE:
				{
					loop = false;
					break;
				}
				case IDABORT:
					TerminateProcess(GetCurrentProcess(), -1);
			}
		}
	}

	//if (conf && conf->Runtime.ChainLoadDalamud_Enable) {
	//	if (conf->Runtime.ChainLoadDalamud_WaitMs) {
	//		Utils::Win32::Thread(L"Dalamud Loader", LoadDalamud);
	//	} else
	//		LoadDalamud();
	//}
	
	s_loaded = true;
}

//void LoadDalamud() {
//	try {
//		const auto conf = App::Config::Acquire();
//		Sleep(conf->Runtime.ChainLoadDalamud_WaitMs);
//
//		auto workingDirectory = conf->Runtime.ChainLoadDalamud_WorkingDirectory.Value();
//		if (workingDirectory.empty())
//			workingDirectory = Utils::Win32::EnsureKnownFolderPath(FOLDERID_RoamingAppData) / L"XIVLauncher";
//		else
//			workingDirectory = App::Config::Config::TranslatePath(workingDirectory);
//
//		const auto dalamudDllPath = workingDirectory / L"addon" / L"Hooks" / L"dev" / L"Dalamud.dll";
//		const auto dalamud = Utils::Win32::LoadedModule(dalamudDllPath);
//		dalamud.SetPinned();
//
//		auto configurationPath = App::Config::Config::TranslatePath(conf->Runtime.ChainLoadDalamud_ConfigurationPath.Value(), workingDirectory);
//		auto pluginDirectory = App::Config::Config::TranslatePath(conf->Runtime.ChainLoadDalamud_PluginDirectory.Value(), workingDirectory);
//		auto defaultPluginDirectory = App::Config::Config::TranslatePath(conf->Runtime.ChainLoadDalamud_DefaultPluginDirectory.Value(), workingDirectory);
//		auto assetDirectory = App::Config::Config::TranslatePath(conf->Runtime.ChainLoadDalamud_ConfigurationPath.Value(), workingDirectory);
//		if (configurationPath.empty())
//			configurationPath = workingDirectory / L"dalamudConfig.json";
//		if (pluginDirectory.empty())
//			pluginDirectory = workingDirectory / L"installedPlugins";
//		if (defaultPluginDirectory.empty())
//			defaultPluginDirectory = workingDirectory / L"devPlugins";
//		if (assetDirectory.empty())
//			assetDirectory = workingDirectory / L"dalamudAssets";
//
//		std::string gameVersion;
//		int language = static_cast<int>(Sqex::Language::English) - 1;
//		auto pairs = Sqex::CommandLine::FromString(Utils::ToUtf8(Utils::Win32::GetCommandLineWithoutProgramName()));
//		for (auto& [k, v] : pairs) {
//			if (k == "ver")
//				gameVersion = std::move(v);
//			else if (k == "language") {
//				language = strtol(v.c_str(), nullptr, 10);
//			}
//		}
//		if (gameVersion.empty())
//			gameVersion = (std::stringstream() << std::ifstream(Utils::Win32::Process::Current().PathOf().parent_path() / L"ffxivgame.ver").rdbuf()).str();
//		auto optOutMbCollection = conf->Runtime.ChainLoadDalamud_OptOutMbCollection.Value();
//
//		// https://github.com/goatcorp/Dalamud/blob/master/Dalamud.Injector/EntryPoint.cs
//		auto json = nlohmann::json::object({
//			{"WorkingDirectory", workingDirectory},
//			{"ConfigurationPath", configurationPath},
//			{"PluginDirectory", pluginDirectory},
//			{"DefaultPluginDirectory",defaultPluginDirectory},
//			{"AssetDirectory", assetDirectory},
//			{"Langauge", language},
//			{"GameVersion", gameVersion},
//			{"OptOutMbCollection", optOutMbCollection },
//			}).dump(4, ' ');
//
//		MessageBoxA(nullptr, json.c_str(), "", MB_OK);
//		dalamud.GetProcAddress<DWORD(WINAPI*)(LPVOID)>("Initialize", true)(&json[0]);
//	} catch (const std::runtime_error& e) {
//		Utils::Win32::MessageBoxF(
//			Dll::FindGameMainWindow(false), MB_ICONWARNING,
//			FindStringResourceEx(Dll::Module(), IDS_APP_NAME, s_wLanguage) + 1,
//			L"Failed to load Dalamud.\nReason: {}", e.what());
//	}
//}
