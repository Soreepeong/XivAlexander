#pragma once

#include <cstddef>
#include <cstdint>
#include <minwindef.h>

// The only contract between a loader DLL and XivAlexander64.dll. A loader DLL copied into a game folder may be
// older or newer than the feature DLL in the program folder, so fields are only ever appended, and each side reads
// no further than the Size the other side wrote.
namespace XivAlexander::LoaderInterface {
	// Version 1: the fields up to and including ReleaseFeatures.
	// Version 2: SetCreateProcessArgumentFilter.
	constexpr uint32_t Version = 2;

	// The oldest loader that XivAlexander64.dll still starts with.
	constexpr uint32_t MinimumVersion = 1;

	// Returns the arguments to start applicationPath with instead of the given ones, or null to keep them. The returned
	// text must stay valid until the next call on the same thread.
	using CreateProcessArgumentFilter = const wchar_t*(__stdcall*)(const wchar_t* applicationPath, const wchar_t* arguments);

	enum class LoadMode : uint32_t {
		EntryPoint = 1,
		Proxy = 2,
		Remote = 3,
	};

	struct LoaderInfo {
		uint32_t Size;  // sizeof(LoaderInfo) as the loader knew it
		uint32_t Version;
		LoadMode Mode;
		uint32_t Reserved;

		HMODULE LoaderModule;
		const wchar_t* LoaderPath;
		const wchar_t* ProgramDirectory;  // where XivAlexander64.dll and XivAlexanderLoader64.exe are
		const wchar_t* OriginalCommandLine;  // as the process was started, before anything replaced it

		// Dll::EnableInjectOnCreateProcess of the loader: how it follows processes that this one creates. May be null.
		size_t(__stdcall* EnableInjectOnCreateProcess)(size_t flags);

		// Tells the loader that the features were turned off from inside, so that it may let go of the feature DLL. May be null.
		void(__stdcall* ReleaseFeatures)();

		// Version 2. Has the loader pass the arguments of the processes it follows through the filter; null to stop. May be null.
		void(__stdcall* SetCreateProcessArgumentFilter)(CreateProcessArgumentFilter filter);
	};

	enum InitializeResult : int32_t {
		Initialized = 0,
		Failed = -1,
		Incompatible = -2,
	};

	// int32_t XA_Initialize(const LoaderInfo*), exported by XivAlexander64.dll.
	using InitializeFunction = int32_t(__stdcall*)(const LoaderInfo* pInfo);
	constexpr char InitializeExportName[] = "XA_Initialize";

	[[nodiscard]] constexpr bool HasField(const LoaderInfo& info, size_t offsetEnd) {
		return info.Size >= offsetEnd;
	}
}

// Whether the LoaderInfo at p carries the given field.
#define XA_LOADERINFO_HAS(p, field) (::XivAlexander::LoaderInterface::HasField(*(p), offsetof(::XivAlexander::LoaderInterface::LoaderInfo, field) + sizeof((p)->field)))
