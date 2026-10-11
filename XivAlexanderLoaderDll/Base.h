#pragma once

// XivAlexanderLoader32.dll and XivAlexanderLoader64.dll: Win32 only, no C runtime, no C++ exceptions or RTTI, no
// constructors or destructors at namespace scope (nothing would run them).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <winternl.h>
#include <intrin.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <new>

#if INTPTR_MAX == INT64_MAX
#define XA_LOADER64 1
#else
#define XA_LOADER64 0
#endif

namespace Loader {
	constexpr wchar_t GameExecutableName[] = L"ffxiv_dx11.exe";
	constexpr wchar_t LoaderExeName[] = L"XivAlexanderLoader64.exe";
	constexpr wchar_t LoaderDll32Name[] = L"XivAlexanderLoader32.dll";
	constexpr wchar_t FeatureDllName[] = L"XivAlexander64.dll";

	// Dll::InjectEntryPointParameters in XivAlexander.h; written by the patcher, read by InjectEntryPoint.
	struct InjectEntryPointParameters {
		void* EntryPoint;
		void* EntryPointOriginalBytes;
		size_t EntryPointOriginalLength;
		void* TrampolineAddress;
		bool LoadedAsProxy;
	};

	namespace InjectFlags {
		enum : size_t {
			Use = 1 << 0,
			InjectAll = 1 << 1,
			InjectGameOnly = 1 << 2,
		};
	}

	void* Alloc(size_t size);
	void Free(void* p);
	size_t Length(const wchar_t* s);
	size_t Length(const char* s);

	// A growable wide string, always null-terminated; allocation failure ends the process.
	class Str {
		wchar_t* m_p = nullptr;
		size_t m_len = 0;
		size_t m_cap = 0;

	public:
		Str() = default;
		Str(const wchar_t* s);
		Str(const wchar_t* s, size_t n);
		Str(const Str& r);
		Str(Str&& r) noexcept;
		~Str();
		Str& operator=(const Str& r);
		Str& operator=(Str&& r) noexcept;

		[[nodiscard]] const wchar_t* c_str() const { return m_p ? m_p : L""; }
		[[nodiscard]] size_t size() const { return m_len; }
		[[nodiscard]] bool empty() const { return !m_len; }
		wchar_t operator[](size_t i) const { return m_p[i]; }

		// Writable buffer of at least n characters plus the terminator; Resize afterwards to the length used.
		wchar_t* Buffer(size_t n);
		void Resize(size_t n);
		void Clear() { Resize(0); }

		Str& Append(const wchar_t* s, size_t n);
		Str& Append(const wchar_t* s) { return Append(s, Length(s)); }
		Str& Append(const Str& s) { return Append(s.c_str(), s.size()); }
		Str& Append(wchar_t c) { return Append(&c, 1); }
		Str& AppendDec(uint32_t n);
		Str& AppendHex(uintptr_t n);

		static Str Dec(uint32_t n) { return Str().AppendDec(n); }
	};

	struct Bytes {
		char* Data = nullptr;
		size_t Size = 0;
		Bytes() = default;
		Bytes(const Bytes&) = delete;
		Bytes& operator=(const Bytes&) = delete;
		~Bytes() { Free(Data); }
	};

	inline const wchar_t* CStr(const wchar_t* s) { return s ? s : L"(null)"; }
	inline const wchar_t* CStr(const Str& s) { return s.c_str(); }

	// Replaces each "{}" of fmt by the next argument.
	Str FormatArgs(const wchar_t* fmt, const wchar_t* const* args, size_t count);

	template<typename... TArgs>
	Str Format(const wchar_t* fmt, const TArgs&... args) {
		const wchar_t* list[sizeof...(TArgs) + 1]{CStr(args)...};
		return FormatArgs(fmt, list, sizeof...(TArgs));
	}

	void DebugPrintText(const Str& s);

	template<typename... TArgs>
	void DebugPrint(const wchar_t* fmt, const TArgs&... args) {
		DebugPrintText(Format(fmt, args...));
	}

	// "context: code (system message)"
	Str ErrorText(const wchar_t* context, DWORD code);
	Str LastErrorText(const wchar_t* context);

	[[nodiscard]] bool IEquals(const wchar_t* a, const wchar_t* b);
	[[nodiscard]] const wchar_t* FileNameOf(const wchar_t* path);
	[[nodiscard]] Str DirectoryOf(const wchar_t* path);
	[[nodiscard]] Str Join(const Str& dir, const wchar_t* name);
	[[nodiscard]] bool Exists(const wchar_t* path);
	[[nodiscard]] bool SameFile(const wchar_t* a, const wchar_t* b);
	[[nodiscard]] bool IsAbsolutePath(const wchar_t* path);
	[[nodiscard]] bool GetFullPath(const wchar_t* path, Str& out);
	// Env-expanded, relative to relativeTo, normalized; as Utils::Win32::TranslatePath.
	[[nodiscard]] Str TranslatePath(const wchar_t* path, const wchar_t* relativeTo);
	[[nodiscard]] Str FromUtf8(const char* s, size_t n);
	[[nodiscard]] Str ModulePath(HMODULE hModule);
	[[nodiscard]] Str ProcessImagePath(HANDLE hProcess, DWORD flags = 0);
	[[nodiscard]] Str SystemDirectory();
	[[nodiscard]] Str KnownFolder(const GUID& id);
	[[nodiscard]] bool ReadWholeFile(const wchar_t* path, Bytes& out, size_t maxSize);
	[[nodiscard]] bool IsProcess64(HANDLE hProcess);
	[[nodiscard]] bool IsEnvironmentVariableSet(const wchar_t* name);
	// Command line quoting, as Utils::Win32::ReverseCommandLineToArgv.
	[[nodiscard]] Str QuoteArgument(const wchar_t* arg);
	// The file version resource's FileDescription is that of a loader DLL or of a pre-split XivAlexander64.dll.
	[[nodiscard]] bool IsXivAlexanderDll(const wchar_t* path);
	void PinModule(HMODULE hModule);
	HWND FindGameMainWindow();

	// Strings of Loader.rc.
	const wchar_t* LoadStr(UINT id);
	Str ButtonText(int id);
	int ShowMessageBox(HWND hwnd, const wchar_t* text, UINT type);
	void OpenUrl(const wchar_t* url);

	extern HMODULE g_module;
	extern const wchar_t* g_modulePath;
	extern const wchar_t* g_moduleDirectory;
	extern const wchar_t* g_originalCommandLine;

	void* PatchEntryPoint(HANDLE hProcess, Str& error);
	void RestoreEntryPoint(const InjectEntryPointParameters* p);

	extern bool g_loadedFromEntryPoint;
	void RemoveFollowHooks();

	Str ProgramDirectory();
#if XA_LOADER64
	Str ConfigDirectory();
	struct PathList {
		Str* Items = nullptr;
		size_t Count = 0;
		void Add(Str s);
		void Reset();
	};
	// The ChainLoad entries of this game in config.runtime.json for the slot (D3d11/Dxgi/Dinput8, legacy key).
	void ReadChainLoadDlls(const char* configName, const char* legacyConfigName, PathList& out);

	void LoadFeaturesBeforeEntryPoint(bool asProxy);
	bool ReleaseSelfReference();
	const wchar_t* FilterCreateProcessArguments(const wchar_t* applicationPath, const wchar_t* arguments);

	void AttachAsProxyIfApplicable(bool staticLoad);
	void InitializeAsProxyBeforeEntryPoint();
#endif
}

extern "C" size_t EnableInjectOnCreateProcess(size_t flags);
#if XA_LOADER64
extern "C" size_t EnableXivAlexander(size_t bEnable);
#endif
