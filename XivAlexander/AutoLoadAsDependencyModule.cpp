#include "pch.h"

#define DIRECTINPUT_VERSION 0x0800  // NOLINT(cppcoreguidelines-macro-usage)

#include <dinput.h>
#include <XivAlexander/XivAlexander.h>
#include <XivAlexanderCommon/Sqex_CommandLine.h>
#include <XivAlexanderCommon/Utils_Win32.h>

#include "App_ConfigRepository.h"
#include "DllMain.h"

static WORD s_wLanguage = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);

static Utils::Win32::LoadedModule EnsureOriginalDependencyModule(const char* szDllName, std::filesystem::path originalDllPath);

static std::set<std::filesystem::path> s_ignoreDlls;
static bool s_useSystemDll = false;

template<typename T_Fn, typename Ret>
Ret ChainCall(const char* szDllName, const char* szFunctionName, std::vector<std::filesystem::path> chainLoadDlls, std::function<Ret(T_Fn, bool discardImmediately)> cb) {
	const auto systemDll = Utils::Win32::GetSystem32Path() / szDllName;

	if (s_useSystemDll) {
		chainLoadDlls.clear();
	} else {
		std::vector<std::filesystem::path> temp;
		for (auto& path : chainLoadDlls)
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

		} catch (const std::exception& e) {
			const auto activationContextCleanup = Dll::ActivationContext().With();
			const auto choice = Dll::MessageBoxF(
				Dll::FindGameMainWindow(false), MB_ICONWARNING | MB_ABORTRETRYIGNORE,
				L"Failed to load {}.\nReason: {}\n\nPress Abort to exit.\nPress Retry to keep on loading.\nPress Ignore to skip right ahead to system DLL.",
				dll, e.what());
			switch (choice) {
				case IDRETRY:
					s_ignoreDlls.insert(dll);
					return ChainCall(szDllName, szFunctionName, std::move(chainLoadDlls), cb);

				case IDIGNORE:
					s_useSystemDll = true;
					chainLoadDlls = {systemDll};
					i = static_cast<size_t>(-1);
					break;

				case IDABORT:
					TerminateProcess(GetCurrentProcess(), -1);
			}
		}

		if (isLast && std::ranges::find(chainLoadDlls, systemDll) == chainLoadDlls.end())
			chainLoadDlls.emplace_back(systemDll);
	}

	const auto activationContextCleanup = Dll::ActivationContext().With();
	Dll::MessageBoxF(
		Dll::FindGameMainWindow(false), MB_ICONERROR,
		L"Failed to load any of the possible {}. Aborting.",
		szDllName);
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
	UINT Flags,
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
