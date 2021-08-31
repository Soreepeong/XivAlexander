#include "pch.h"
#include "XivAlexander/XivAlexander.h"
#include "App_Feature_HashTracker.h"

#include <dinput.h>

static Utils::Win32::LoadedModule EnsureOriginalDependencyModule(const char* szDllName);
static void AutoLoadAsDependencyModule();

#if INTPTR_MAX == INT64_MAX

#include <d3d11.h>
#include <dxgi.h>
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
	static auto pOriginalFunction =
		EnsureOriginalDependencyModule("d3d11.dll").GetProcAddress<decltype(D3D11CreateDevice)>("D3D11CreateDevice");

	AutoLoadAsDependencyModule();

	return pOriginalFunction(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion, ppDevice, pFeatureLevel, ppImmediateContext);
}

HRESULT WINAPI FORWARDER_CreateDXGIFactory(
	REFIID riid,
	void** ppFactory
) {
	static auto pOriginalFunction =
		EnsureOriginalDependencyModule("dxgi.dll").GetProcAddress<decltype(CreateDXGIFactory)>("CreateDXGIFactory");

	AutoLoadAsDependencyModule();

	return pOriginalFunction(riid, ppFactory);
}

HRESULT WINAPI FORWARDER_CreateDXGIFactory1(
	REFIID riid,
	void** ppFactory
) {
	static auto pOriginalFunction =
		EnsureOriginalDependencyModule("dxgi.dll").GetProcAddress<decltype(CreateDXGIFactory1)>("CreateDXGIFactory1");

	AutoLoadAsDependencyModule();

	return pOriginalFunction(riid, ppFactory);
}

HRESULT WINAPI FORWARDER_CreateDXGIFactory2(
	UINT   Flags,
	REFIID riid,
	void** ppFactory
) {
	static auto pOriginalFunction =
		EnsureOriginalDependencyModule("dxgi.dll").GetProcAddress<decltype(CreateDXGIFactory2)>("CreateDXGIFactory2");

	AutoLoadAsDependencyModule();

	return pOriginalFunction(Flags, riid, ppFactory);
}

#elif INTPTR_MAX == INT32_MAX

#include <d3d9.h>

void WINAPI FORWARDER_D3DPERF_SetOptions(DWORD dwOptions) {
	static auto pOriginalFunction =
		EnsureOriginalDependencyModule("d3d9.dll").GetProcAddress<decltype(D3DPERF_SetOptions)>("D3DPERF_SetOptions");

	AutoLoadAsDependencyModule();

	return pOriginalFunction(dwOptions);
}

IDirect3D9* WINAPI FORWARDER_Direct3DCreate9(UINT SDKVersion) {
	static auto pOriginalFunction =
		EnsureOriginalDependencyModule("d3d9.dll").GetProcAddress<decltype(Direct3DCreate9)>("Direct3DCreate9");

	AutoLoadAsDependencyModule();

	return pOriginalFunction(SDKVersion);
}

#endif

HRESULT WINAPI FORWARDER_DirectInput8Create(
	HINSTANCE hinst,
	DWORD dwVersion,
	REFIID riidltf,
	LPVOID* ppvOut,
	LPUNKNOWN punkOuter
) {
	static auto pOriginalFunction =
		EnsureOriginalDependencyModule("dinput8.dll").GetProcAddress<decltype(DirectInput8Create)>("DirectInput8Create");

	AutoLoadAsDependencyModule();

	return pOriginalFunction(hinst, dwVersion, riidltf, ppvOut, punkOuter);
}

static Utils::Win32::LoadedModule EnsureOriginalDependencyModule(const char* szDllName) {
	static std::mutex preventDuplicateLoad;
	std::lock_guard lock(preventDuplicateLoad);
	const auto originalDllPath = Utils::Win32::GetSystem32Path() / szDllName;

	HMODULE hModule = GetModuleHandleW(originalDllPath.c_str());
	if (!hModule)
		hModule = LoadLibraryW(originalDllPath.c_str());
	auto mod = Utils::Win32::LoadedModule(hModule, false);
	mod.Pin();
	return mod;
}

void AutoLoadAsDependencyModule() {
	static std::mutex s_singleRunMutex;
	static App::Feature::HashTracker s_pinnedHashTracker;
	static bool s_loaded = false;

	if (s_loaded)
		return;

	std::lock_guard lock(s_singleRunMutex);
	if (s_loaded)
		return;

	s_loaded = true;
	XivAlexDll::DisableUnloading(std::format("Loaded as DLL dependency in place of {}", Dll::Module().PathOf().filename()).c_str());
	InjectEntryPoint(XivAlexDll::PatchEntryPointForInjection(GetCurrentProcess()));
}
