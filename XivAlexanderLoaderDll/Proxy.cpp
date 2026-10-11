#include "Base.h"

#define DIRECTINPUT_VERSION 0x0800  // NOLINT(cppcoreguidelines-macro-usage)
#include <d3d11.h>
#include <dinput.h>
#include <dxgi1_3.h>

#include "ProxyExports.h"

// XivAlexanderLoader64.dll installed into the game folder as d3d11.dll, dxgi.dll or dinput8.dll.

extern "C" void* XA_ProxySlots[];

namespace {
	struct Slot {
		const char* DllName;
		const wchar_t* DllNameW;
		const char* ConfigName;  // the item in the runtime config's ChainLoad group
		const char* LegacyConfigName;
	};

	constexpr Slot Slots[]{
		{"d3d11.dll", L"d3d11.dll", "D3d11", "ChainLoadPath_d3d11"},
		{"dxgi.dll", L"dxgi.dll", "Dxgi", "ChainLoadPath_dxgi"},
		{"dinput8.dll", L"dinput8.dll", "Dinput8", "ChainLoadPath_dinput8"},
	};
	constexpr size_t SlotCount = sizeof Slots / sizeof Slots[0];

	SRWLOCK s_chainLock = SRWLOCK_INIT;
	bool s_chainRead[SlotCount];
	Loader::PathList s_chain[SlotCount];

	// Chain DLLs skipped after the user chose to keep on loading without them.
	Loader::PathList s_ignoreDlls;
	bool s_useSystemDll;

	const Slot* s_proxySlot;

	size_t SlotIndexOf(const char* dllName) {
		for (size_t i = 0; i < SlotCount; ++i) {
			if (lstrcmpiA(Slots[i].DllName, dllName) == 0)
				return i;
		}
		return SlotCount;
	}

	bool Contains(const Loader::PathList& list, const Loader::Str& path) {
		for (size_t i = 0; i < list.Count; ++i) {
			if (Loader::IEquals(list.Items[i].c_str(), path.c_str()))
				return true;
		}
		return false;
	}

	// The configured ChainLoad DLLs of the slot, read once; never this DLL itself, as an entry for the game folder would be.
	void CopyChainLoadDlls(size_t slot, Loader::PathList& out) {
		using namespace Loader;
		AcquireSRWLockExclusive(&s_chainLock);
		if (!s_chainRead[slot]) {
			s_chainRead[slot] = true;
			PathList read;
			ReadChainLoadDlls(Slots[slot].ConfigName, Slots[slot].LegacyConfigName, read);
			for (size_t i = 0; i < read.Count; ++i) {
				if (!SameFile(read.Items[i].c_str(), g_modulePath))
					s_chain[slot].Add(read.Items[i]);
			}
			read.Reset();
		}
		for (size_t i = 0; i < s_chain[slot].Count; ++i)
			out.Add(s_chain[slot].Items[i]);
		ReleaseSRWLockExclusive(&s_chainLock);
	}

	FARPROC LoadPinned(const Loader::Str& path, const char* name, Loader::Str& error) {
		using namespace Loader;
		const auto hModule = LoadLibraryW(path.c_str());
		if (!hModule) {
			error = LastErrorText(L"LoadLibraryW");
			return nullptr;
		}
		PinModule(hModule);
		const auto pfn = GetProcAddress(hModule, name);
		if (!pfn)
			error = LastErrorText(L"GetProcAddress");
		FreeLibrary(hModule);
		return pfn;
	}

	// Calls the function in each configured DLL in order and returns what the last one returned; the system DLL is last.
	template<typename TFn, typename TCallback>
	HRESULT ChainCall(size_t slot, const char* functionName, const TCallback& cb) {
		using namespace Loader;
		const auto systemDll = Join(SystemDirectory(), Slots[slot].DllNameW);

		PathList list;
		if (!s_useSystemDll)
			CopyChainLoadDlls(slot, list);
		if (!list.Count)
			list.Add(systemDll);

		for (size_t i = 0; i < list.Count; ++i) {
			const auto dll = list.Items[i];
			const auto isLast = i == list.Count - 1;

			if (Contains(s_ignoreDlls, dll)) {
				if (isLast && !Contains(list, systemDll))
					list.Add(systemDll);
				continue;
			}

			Str error;
			if (const auto pfn = reinterpret_cast<TFn>(LoadPinned(dll, functionName, error))) {
				if (isLast) {
					const auto result = cb(pfn, false);
					list.Reset();
					return result;
				}
				cb(pfn, true);
				continue;
			}

			const auto text = Format(
				L"Failed to load {}.\nReason: {}\n\nPress Abort to exit.\nPress Retry to keep on loading.\nPress Ignore to skip right ahead to system DLL.",
				dll, error);
			switch (ShowMessageBox(FindGameMainWindow(), text.c_str(), MB_ICONWARNING | MB_ABORTRETRYIGNORE)) {
				case IDRETRY: {
					s_ignoreDlls.Add(dll);
					list.Reset();
					return ChainCall<TFn>(slot, functionName, cb);
				}

				case IDIGNORE:
					s_useSystemDll = true;
					list.Reset();
					list.Add(systemDll);
					i = static_cast<size_t>(-1);
					continue;

				case IDABORT:
					TerminateProcess(GetCurrentProcess(), static_cast<UINT>(-1));
			}
		}

		list.Reset();
		const auto text = Format(L"Failed to load any of the possible {}. Aborting.", Slots[slot].DllNameW);
		ShowMessageBox(FindGameMainWindow(), text.c_str(), MB_ICONERROR);
		TerminateProcess(GetCurrentProcess(), static_cast<UINT>(-1));
		ExitProcess(static_cast<UINT>(-1));
	}

	bool IsOwn(const void* p) {
		HMODULE hModule{};
		return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(p), &hModule)
			&& hModule == Loader::g_module;
	}
}

// What an export resolves to when neither a ChainLoad DLL nor System32 has it, so that importers still bind.
extern "C" HRESULT XA_ProxyNotImplemented() {
	return E_NOTIMPL;
}

// Called by the stubs in ProxyExports.asm on their first call. Only the DLL this one stands in for gets real targets;
// it would never be asked for another one's functions.
extern "C" void* XA_ProxyResolve(uint32_t index) {
	using namespace Loader;
	const auto& entry = XivAlexander::EntryPoint::ProxyExports::Exports[index];
	void* target = nullptr;
	if (s_proxySlot && lstrcmpiA(entry.DllName, s_proxySlot->DllName) == 0) {
		const auto slot = SlotIndexOf(entry.DllName);
		PathList candidates;
		CopyChainLoadDlls(slot, candidates);
		candidates.Add(Join(SystemDirectory(), Slots[slot].DllNameW));
		for (size_t i = 0; i < candidates.Count && !target; ++i) {
			const auto hModule = LoadLibraryW(candidates.Items[i].c_str());
			if (!hModule)
				continue;
			PinModule(hModule);
			if (const auto p = reinterpret_cast<void*>(GetProcAddress(hModule, entry.Name)); p && !IsOwn(p))
				target = p;
			FreeLibrary(hModule);
		}
		candidates.Reset();
		if (!target)
			DebugPrint(L"Cannot resolve {}!{}", FromUtf8(entry.DllName, Length(entry.DllName)), FromUtf8(entry.Name, Length(entry.Name)));
	}
	if (!target)
		target = reinterpret_cast<void*>(&XA_ProxyNotImplemented);
	InterlockedExchangePointer(&XA_ProxySlots[index], target);
	return target;
}

void Loader::AttachAsProxyIfApplicable(bool staticLoad) {
	const auto selfName = FileNameOf(g_modulePath);
	for (const auto& slot : Slots) {
		if (IEquals(selfName, slot.DllNameW))
			s_proxySlot = &slot;
	}
	if (!s_proxySlot || !staticLoad)
		return;
	if (!IEquals(FileNameOf(ProcessImagePath(GetCurrentProcess()).c_str()), GameExecutableName))
		return;
	if (IsEnvironmentVariableSet(L"XIVALEXANDER_DISABLE"))
		return;

	// Nothing can be loaded under the loader lock; this resumes at the game's entry point instead.
	Str error;
	if (const auto params = static_cast<InjectEntryPointParameters*>(PatchEntryPoint(GetCurrentProcess(), error)))
		params->LoadedAsProxy = true;
	else
		DebugPrint(L"Cannot patch the entry point: {}", error);
}

void Loader::InitializeAsProxyBeforeEntryPoint() {
	if (!s_proxySlot)
		return;

	// The DLLs this one stands in front of get to initialize before the game does, as they would have in its place.
	PathList chain;
	CopyChainLoadDlls(static_cast<size_t>(s_proxySlot - Slots), chain);
	for (size_t i = 0; i < chain.Count; ++i) {
		if (const auto hModule = LoadLibraryW(chain.Items[i].c_str()))
			PinModule(hModule);
		else
			DebugPrint(L"Cannot load {}: {}", chain.Items[i], LastErrorText(L"LoadLibraryW"));
	}
	chain.Reset();

	if (IsEnvironmentVariableSet(L"XIVALEXANDER_DISABLE"))
		return;

	EnableInjectOnCreateProcess(InjectFlags::Use);
	LoadFeaturesBeforeEntryPoint(true);
}

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
	return ChainCall<PFN_D3D11_CREATE_DEVICE>(0, "D3D11CreateDevice", [&](PFN_D3D11_CREATE_DEVICE pfn, bool discardImmediately) {
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

namespace {
	template<typename TFn, typename... TArgs>
	HRESULT ChainCallFactory(const char* name, void** ppFactory, TArgs... args) {
		return ChainCall<TFn>(1, name, [&](TFn pfn, bool discardImmediately) {
			const auto res = pfn(args..., ppFactory);
			if (res == S_OK && discardImmediately && ppFactory)
				static_cast<IUnknown*>(*ppFactory)->Release();
			return res;
		});
	}
}

HRESULT WINAPI FORWARDER_CreateDXGIFactory(REFIID riid, void** ppFactory) {
	return ChainCallFactory<HRESULT(WINAPI*)(REFIID, void**)>("CreateDXGIFactory", ppFactory, riid);
}

HRESULT WINAPI FORWARDER_CreateDXGIFactory1(REFIID riid, void** ppFactory) {
	return ChainCallFactory<HRESULT(WINAPI*)(REFIID, void**)>("CreateDXGIFactory1", ppFactory, riid);
}

HRESULT WINAPI FORWARDER_CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) {
	return ChainCallFactory<HRESULT(WINAPI*)(UINT, REFIID, void**)>("CreateDXGIFactory2", ppFactory, Flags, riid);
}

HRESULT WINAPI FORWARDER_DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, void** ppvOut, LPUNKNOWN punkOuter) {
	return ChainCall<HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, void**, LPUNKNOWN)>(2, "DirectInput8Create", [&](auto pfn, bool discardImmediately) {
		const auto res = pfn(hinst, dwVersion, riidltf, ppvOut, punkOuter);
		if (res == S_OK && discardImmediately && ppvOut)
			static_cast<IUnknown*>(*ppvOut)->Release();
		return res;
	});
}
