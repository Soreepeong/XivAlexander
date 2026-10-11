#include "Base.h"

#include "../XivAlexander/LoaderInterface.h"
#include "resource.h"

// How XivAlexanderLoader64.dll finds, loads and talks to XivAlexander64.dll.

namespace LoaderInterface = XivAlexander::LoaderInterface;

namespace {
	// Guards the state below; never held while calling into XivAlexander64.dll or freeing it.
	SRWLOCK s_lock = SRWLOCK_INIT;

	// Serializes loading XivAlexander64.dll.
	SRWLOCK s_loadLock = SRWLOCK_INIT;

	// The reference this loader holds on XivAlexander64.dll, and how it got loaded.
	HMODULE s_feature;
	LoaderInterface::LoadMode s_mode;

	// Held while a feature DLL loaded into a running process may call back into this one.
	HMODULE s_selfReference;

	LoaderInterface::CreateProcessArgumentFilter volatile s_argumentFilter;

	LoaderInterface::LoaderInfo s_info;
	wchar_t* s_programDirectory;

	struct Lock {
		SRWLOCK* L;
		explicit Lock(SRWLOCK* l) : L(l) { AcquireSRWLockExclusive(L); }
		~Lock() { ReleaseSRWLockExclusive(L); }
	};

	HMODULE AddReference(HMODULE hModule) {
		HMODULE h{};
		return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(hModule), &h) ? h : nullptr;
	}

	size_t __stdcall CallbackEnableInjectOnCreateProcess(size_t flags) {
		return EnableInjectOnCreateProcess(flags);
	}

	void __stdcall CallbackSetCreateProcessArgumentFilter(LoaderInterface::CreateProcessArgumentFilter filter) {
		InterlockedExchangePointer(reinterpret_cast<void* volatile*>(&s_argumentFilter), reinterpret_cast<void*>(filter));
	}

	void __stdcall CallbackReleaseFeatures() {
		HMODULE released{};
		{
			const Lock lock(&s_lock);
			// Features started before the entry point keep their DLL loaded, as their hooks may still be referenced.
			if (s_mode == LoaderInterface::LoadMode::Remote) {
				CallbackSetCreateProcessArgumentFilter(nullptr);
				released = s_feature;
				s_feature = nullptr;
			}
		}
		if (released)
			FreeLibrary(released);
	}

	void Adopt(HMODULE hModule, LoaderInterface::LoadMode mode) {
		HMODULE previous;
		{
			const Lock lock(&s_lock);
			previous = s_feature;
			s_feature = hModule;
			s_mode = mode;
		}
		if (previous)
			FreeLibrary(previous);
	}

	const LoaderInterface::LoaderInfo& MakeLoaderInfo(LoaderInterface::LoadMode mode) {
		using namespace Loader;
		if (!s_programDirectory) {
			const auto dir = ProgramDirectory();
			s_programDirectory = static_cast<wchar_t*>(Alloc((dir.size() + 1) * sizeof(wchar_t)));
			memcpy(s_programDirectory, dir.c_str(), dir.size() * sizeof(wchar_t));
		}
		s_info = {
			.Size = static_cast<uint32_t>(sizeof s_info),
			.Version = LoaderInterface::Version,
			.Mode = mode,
			.LoaderModule = g_module,
			.LoaderPath = g_modulePath,
			.ProgramDirectory = s_programDirectory,
			.OriginalCommandLine = g_originalCommandLine,
			.EnableInjectOnCreateProcess = &CallbackEnableInjectOnCreateProcess,
			.ReleaseFeatures = &CallbackReleaseFeatures,
			.SetCreateProcessArgumentFilter = &CallbackSetCreateProcessArgumentFilter,
		};
		return s_info;
	}

	// False with the reason if the DLL cannot be loaded; otherwise result is a LoaderInterface::InitializeResult.
	bool LoadAndInitialize(const Loader::Str& path, LoaderInterface::LoadMode mode, int32_t& result, Loader::Str& error) {
		using namespace Loader;
		const auto hModule = LoadLibraryW(path.c_str());
		if (!hModule) {
			error = LastErrorText(L"LoadLibraryW");
			return false;
		}

		if (const auto initialize = reinterpret_cast<LoaderInterface::InitializeFunction>(GetProcAddress(hModule, LoaderInterface::InitializeExportName))) {
			result = initialize(&MakeLoaderInfo(mode));
			// A failed start before the entry point may have left hooks behind; unloading the DLL under them is worse.
			if (result == LoaderInterface::Initialized || (result == LoaderInterface::Failed && mode != LoaderInterface::LoadMode::Remote)) {
				Adopt(hModule, mode);
			} else {
				if (result == LoaderInterface::Incompatible)
					DebugPrint(L"{} does not support loader interface version {}; XivAlexander is not loaded.", path, Str::Dec(LoaderInterface::Version));
				FreeLibrary(hModule);
			}
			return true;
		}

		// An XivAlexander64.dll from before the loader DLLs.
		if (mode == LoaderInterface::LoadMode::Remote) {
			const auto enable = reinterpret_cast<size_t(*)(size_t)>(GetProcAddress(hModule, "EnableXivAlexander"));
			if (!enable) {
				error = LastErrorText(L"GetProcAddress(EnableXivAlexander)");
				FreeLibrary(hModule);
				return false;
			}
			if (enable(1) != 0) {
				FreeLibrary(hModule);
				result = LoaderInterface::Failed;
				return true;
			}
		} else {
			const auto injectEntryPoint = reinterpret_cast<void(*)(void*)>(GetProcAddress(hModule, "XA_InjectEntryPoint"));
			if (!injectEntryPoint) {
				error = LastErrorText(L"GetProcAddress(XA_InjectEntryPoint)");
				FreeLibrary(hModule);
				return false;
			}
			// It follows child processes itself, and starts as if its own entry point patch had run.
			EnableInjectOnCreateProcess(0);
			PinModule(hModule);
			const auto params = PatchEntryPoint(GetCurrentProcess(), error);
			if (!params) {
				FreeLibrary(hModule);
				return false;
			}
			injectEntryPoint(params);
		}
		Adopt(hModule, mode);
		result = LoaderInterface::Initialized;
		return true;
	}
}

const wchar_t* Loader::FilterCreateProcessArguments(const wchar_t* applicationPath, const wchar_t* arguments) {
	const auto filter = s_argumentFilter;
	return filter ? filter(applicationPath, arguments) : nullptr;
}

void Loader::LoadFeaturesBeforeEntryPoint(bool asProxy) {
	const Lock loadLock(&s_loadLock);
	const auto dir = ProgramDirectory();
	const auto path = dir.empty() ? Str() : Join(dir, FeatureDllName);
	Str error;
	if (!path.empty()) {
		int32_t result;
		if (LoadAndInitialize(path, asProxy ? LoaderInterface::LoadMode::Proxy : LoaderInterface::LoadMode::EntryPoint, result, error))
			return;
	}

	const auto pathText = path.empty() ? Str(LoadStr(IDS_ERROR_LOAD_INSTALLED_NOPATH)) : Format(LoadStr(IDS_ERROR_LOAD_INSTALLED_PATH), path);
	const auto text = Format(LoadStr(IDS_ERROR_LOAD_INSTALLED), pathText, error, ButtonText(IDABORT), ButtonText(IDRETRY), ButtonText(IDIGNORE));
	while (true) {
		switch (ShowMessageBox(nullptr, text.c_str(), MB_ICONWARNING | MB_ABORTRETRYIGNORE)) {
			case IDRETRY:
				OpenUrl(LoadStr(IDS_URL_HELP));
				break;

			case IDIGNORE:
				return;

			case IDABORT:
				TerminateProcess(GetCurrentProcess(), static_cast<UINT>(-1));
		}
	}
}

extern "C" size_t EnableXivAlexander(size_t bEnable) {
	using namespace Loader;
	using EnableFn = size_t(*)(size_t);

	// The feature DLL is called without s_lock held, as its own threads may call back while it shuts down.
	HMODULE feature{};
	auto mode = LoaderInterface::LoadMode::Remote;
	{
		const Lock lock(&s_lock);
		if (s_feature) {
			feature = AddReference(s_feature);
			mode = s_mode;
		}
	}

	if (bEnable) {
		if (feature) {
			const auto enable = reinterpret_cast<EnableFn>(GetProcAddress(feature, "EnableXivAlexander"));
			const auto result = enable ? enable(1) : static_cast<size_t>(-1);
			FreeLibrary(feature);
			return result;
		}

		const Lock loadLock(&s_loadLock);
		int32_t result;
		const auto dir = ProgramDirectory();
		if (Str error; dir.empty() || !LoadAndInitialize(Join(dir, FeatureDllName), LoaderInterface::LoadMode::Remote, result, error)) {
			DebugPrint(L"EnableXivAlexander(1) error: {}", error);
			return static_cast<size_t>(-1);
		}
		if (result != LoaderInterface::Initialized)
			return static_cast<size_t>(static_cast<intptr_t>(result));
		const Lock lock(&s_lock);
		if (!s_selfReference)
			s_selfReference = AddReference(g_module);
		return 0;
	}

	// Another copy of the loader, such as one in the game folder, may have loaded it.
	if (!feature) {
		const auto path = Join(ProgramDirectory(), FeatureDllName);
		if (!GetModuleHandleExW(0, path.c_str(), &feature))
			return 0;
	}

	const auto disable = reinterpret_cast<EnableFn>(GetProcAddress(feature, "EnableXivAlexander"));
	if (!disable) {
		FreeLibrary(feature);
		return static_cast<size_t>(-1);
	}
	if (const auto result = disable(0)) {
		FreeLibrary(feature);
		return result;
	}
	FreeLibrary(feature);

	if (mode == LoaderInterface::LoadMode::Remote) {
		HMODULE releasedFeature, releasedSelf;
		{
			const Lock lock(&s_lock);
			CallbackSetCreateProcessArgumentFilter(nullptr);
			releasedFeature = s_feature;
			s_feature = nullptr;
			// The caller, a remote thread through Utils::Win32::InjectedModule, holds a reference of its own.
			releasedSelf = s_selfReference;
			s_selfReference = nullptr;
		}
		if (releasedFeature)
			FreeLibrary(releasedFeature);
		if (releasedSelf)
			FreeLibrary(releasedSelf);
	}
	return 0;
}

bool Loader::ReleaseSelfReference() {
	HMODULE released;
	bool noFeature;
	{
		const Lock lock(&s_lock);
		released = s_selfReference;
		s_selfReference = nullptr;
		noFeature = !s_feature;
	}
	if (released)
		FreeLibrary(released);
	// Features started before the entry point stay, and so does this DLL that they call back into.
	return noFeature;
}
