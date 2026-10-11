#include "Base.h"

#include <MinHook.h>

#include "resource.h"

// Following the processes this one creates: CreateProcessW/A start children suspended, and those that may lead to the
// game get their entry point patched (a 32-bit process hands a 64-bit child to XivAlexanderLoader64.exe).

namespace {
	using CreateProcessWFn = decltype(&CreateProcessW);
	using CreateProcessAFn = decltype(&CreateProcessA);
	using IsDebuggerPresentFn = decltype(&IsDebuggerPresent);

	SRWLOCK s_lock = SRWLOCK_INIT;
	bool s_minHookInitialized;
	bool s_active;
	volatile LONG s_flags;
	volatile LONG s_callsInHook;
	void* s_pCreateProcessW;
	void* s_pCreateProcessA;
	CreateProcessWFn s_originalCreateProcessW;
	CreateProcessAFn s_originalCreateProcessA;
	// The import slot of kernel32!IsDebuggerPresent in the main module, and what it held.
	void** s_pIsDebuggerPresentSlot;
	void* s_originalIsDebuggerPresent;

	struct CallInHook {
		CallInHook() { InterlockedIncrement(&s_callsInHook); }
		~CallInHook() { InterlockedDecrement(&s_callsInHook); }
	};

	BOOL WINAPI DetourIsDebuggerPresent() {
		return FALSE;
	}

	void** FindImportSlot(HMODULE hModule, const char* dllName, const char* functionName) {
		const auto base = reinterpret_cast<const uint8_t*>(hModule);
		const auto& dos = *reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		const auto& nt = *reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos.e_lfanew);
		const auto& dir = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
		if (!dir.VirtualAddress)
			return nullptr;
		for (auto desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->OriginalFirstThunk; ++desc) {
			if (!desc->Name || lstrcmpiA(reinterpret_cast<const char*>(base + desc->Name), dllName) != 0)
				continue;
			const auto lookup = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
			const auto slots = reinterpret_cast<IMAGE_THUNK_DATA*>(const_cast<uint8_t*>(base) + desc->FirstThunk);
			for (size_t i = 0; lookup[i].u1.AddressOfData; ++i) {
				if (IMAGE_SNAP_BY_ORDINAL(lookup[i].u1.Ordinal))
					continue;
				const auto& byName = *reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + lookup[i].u1.AddressOfData);
				if (lstrcmpA(byName.Name, functionName) == 0)
					return reinterpret_cast<void**>(&slots[i].u1.Function);
			}
		}
		return nullptr;
	}

	void WriteSlot(void** slot, void* value) {
		DWORD oldProtect;
		if (VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &oldProtect)) {
			InterlockedExchangePointer(slot, value);
			VirtualProtect(slot, sizeof *slot, oldProtect, &oldProtect);
		}
	}

	[[nodiscard]] bool IsInjectTarget(HANDLE hProcess) {
		using namespace Loader;
		const auto path = ProcessImagePath(hProcess);
		if (path.empty())
			return false;
		auto dir = DirectoryOf(path.c_str());
		const auto fileName = FileNameOf(path.c_str());
		if (IEquals(fileName, LoaderExeName) && SameFile(dir.c_str(), g_moduleDirectory))
			return false;

		const auto isGame = IEquals(fileName, GameExecutableName);
		if (isGame) {
			for (const auto candidate : {L"d3d11.dll", L"dxgi.dll", L"dinput8.dll"}) {
				if (IsXivAlexanderDll(Join(dir, candidate).c_str()))
					return false;
			}
		}

		const auto flags = static_cast<size_t>(s_flags);
		if (flags & InjectFlags::InjectAll)
			return true;
		if (flags & InjectFlags::InjectGameOnly)
			return isGame;

		// Within 3 parent folders, something that has to do with FFXIV.
		for (int i = 0; i < 3; ++i) {
			if (Exists(Join(Join(dir, L"game"), GameExecutableName).c_str()))
				return true;
			dir = DirectoryOf(dir.c_str());
		}
		return false;
	}

#if !XA_LOADER64
	// A 64-bit game started by a 32-bit launcher; only the 64-bit side can patch it.
	bool HandToLoader64(HANDLE hProcess, Loader::Str& error) {
		using namespace Loader;
		const auto companion = Join(ProgramDirectory(), LoaderExeName);
		if (!Exists(companion.c_str())) {
			error = Format(LoadStr(IDS_ERROR_LOADER_NOT_FOUND), companion);
			return false;
		}

		HANDLE hInherited{};
		if (!DuplicateHandle(GetCurrentProcess(), hProcess, GetCurrentProcess(), &hInherited, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
			error = LastErrorText(L"DuplicateHandle");
			return false;
		}

		auto commandLine = QuoteArgument(companion.c_str());
		commandLine.Append(L" --handle-instead-of-pid --action _internal_inject_hookentrypoint ").AppendDec(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hInherited)));

		SIZE_T attributeListSize = 0;
		InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeListSize);
		const auto attributeList = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(Alloc(attributeListSize));
		STARTUPINFOEXW si{};
		si.StartupInfo.cb = sizeof si;
		PROCESS_INFORMATION pi{};
		auto ok = InitializeProcThreadAttributeList(attributeList, 1, 0, &attributeListSize)
			&& UpdateProcThreadAttribute(attributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &hInherited, sizeof hInherited, nullptr, nullptr);
		if (ok) {
			si.lpAttributeList = attributeList;
			const auto create = s_originalCreateProcessW ? s_originalCreateProcessW : &CreateProcessW;
			ok = create(companion.c_str(), const_cast<wchar_t*>(commandLine.c_str()), nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &si.StartupInfo, &pi);
		}
		if (!ok)
			error = LastErrorText(L"CreateProcess");
		DeleteProcThreadAttributeList(attributeList);
		Free(attributeList);
		CloseHandle(hInherited);
		if (!ok)
			return false;

		WaitForSingleObject(pi.hProcess, INFINITE);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		return true;
	}
#endif

	void PostProcessExecution(DWORD dwCreationFlags, const PROCESS_INFORMATION* pi) {
		using namespace Loader;
		Str error;
		const auto isTarget = IsInjectTarget(pi->hProcess);
		if (isTarget) {
#if !XA_LOADER64
			if (IsProcess64(pi->hProcess))
				HandToLoader64(pi->hProcess, error);
			else
#endif
				void(PatchEntryPoint(pi->hProcess, error));
		}

		if (!error.empty()) {
			const auto selfPath = ProcessImagePath(GetCurrentProcess());
			const auto targetPath = ProcessImagePath(pi->hProcess);
			const auto text = Format(
				L"Error: {}\n\n"
				L"isTarget={}\n"
				L"Self: PID={}, Platform={}, Path={}\n"
				L"Target: PID={}, Platform={}, Path={}\n",
				error, isTarget ? L"true" : L"false",
				Str::Dec(GetCurrentProcessId()), XA_LOADER64 ? L"x64" : L"x86", selfPath,
				Str::Dec(pi->dwProcessId), IsProcess64(pi->hProcess) ? L"x64" : L"x86", targetPath);
			ShowMessageBox(nullptr, text.c_str(), MB_OK | MB_ICONERROR);
		}
		if (!(dwCreationFlags & CREATE_SUSPENDED))
			ResumeThread(pi->hThread);
	}

	// Which file a command line without lpApplicationName runs, and its arguments; as CreateProcessW resolves it.
	DWORD ResolveApplication(LPCWSTR lpApplicationName, LPCWSTR lpCommandLine, Loader::Str& applicationPath, Loader::Str& pathForCommandLine, Loader::Str& arguments) {
		using namespace Loader;
		if (lpCommandLine && *lpCommandLine) {
			// The program name as CommandLineToArgvW reads argv[0]; the arguments start after the first blank outside quotes.
			if (lpCommandLine[0] == L'"') {
				auto end = lpCommandLine + 1;
				while (*end && *end != L'"')
					++end;
				pathForCommandLine = Str(lpCommandLine + 1, static_cast<size_t>(end - lpCommandLine - 1));
			} else {
				auto end = lpCommandLine;
				while (*end && *end != L' ' && *end != L'\t')
					++end;
				pathForCommandLine = Str(lpCommandLine, static_cast<size_t>(end - lpCommandLine));
			}
			auto inQuote = false;
			auto ptr = lpCommandLine;
			for (; *ptr && ((*ptr != L' ' && *ptr != L'\t') || inQuote); ++ptr) {
				if (*ptr == L'"')
					inQuote = !inQuote;
			}
			if (*ptr)
				++ptr;
			arguments = ptr;
		} else if (lpApplicationName) {
			pathForCommandLine = lpApplicationName;
		} else {
			return ERROR_INVALID_PARAMETER;
		}

		if (lpApplicationName) {
			applicationPath = lpApplicationName;
		} else if (lpCommandLine[0] == L'"') {
			applicationPath = pathForCommandLine;
		} else {
			// Unquoted: "C:\Program Files\x.exe" may be C:\Program.exe with arguments; try longer prefixes at each space.
			const auto line = lpCommandLine;
			const auto lineLength = Length(line);
			size_t prefixEnd = 0;
			while (prefixEnd < lineLength && line[prefixEnd] != L' ')
				++prefixEnd;
			while (true) {
				const auto prefix = Str(line, prefixEnd);
				auto candidate = prefix;
				const auto name = FileNameOf(candidate.c_str());
				auto hasExtension = false;
				for (auto p = name + 1; *name && *p; ++p)
					hasExtension |= *p == L'.';
				if (lstrcmpW(name, L"..") == 0)
					hasExtension = false;
				if (!hasExtension)
					candidate.Append(L".exe");
				if (!IsAbsolutePath(candidate.c_str())) {
					Str found;
					const auto len = SearchPathW(nullptr, prefix.c_str(), L".exe", 32768, found.Buffer(32768), nullptr);
					if (len && len < 32768) {
						found.Resize(len);
						candidate = static_cast<Str&&>(found);
					}
				}
				if (Exists(candidate.c_str())) {
					applicationPath = candidate;
					pathForCommandLine = candidate;
					arguments = prefixEnd < lineLength ? Str(line + prefixEnd + 1) : Str();
					break;
				}
				if (prefixEnd >= lineLength)
					return ERROR_FILE_NOT_FOUND;
				do
					++prefixEnd;
				while (prefixEnd < lineLength && line[prefixEnd] != L' ');
			}
		}

		Str full;
		if (!GetFullPath(applicationPath.c_str(), full))
			return GetLastError();
		applicationPath = static_cast<Str&&>(full);
		return ERROR_SUCCESS;
	}

	BOOL WINAPI DetourCreateProcessW(LPCWSTR lpApplicationName, LPWSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation) {
		using namespace Loader;
		const CallInHook inHook;
		const bool noOperation = dwCreationFlags & (DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS);

		Str applicationPath, pathForCommandLine, arguments;
		if (const auto err = ResolveApplication(lpApplicationName, lpCommandLine, applicationPath, pathForCommandLine, arguments)) {
			SetLastError(err);
			return FALSE;
		}

#if XA_LOADER64
		if (const auto replaced = FilterCreateProcessArguments(applicationPath.c_str(), arguments.c_str()))
			arguments = replaced;
#endif

		auto commandLine = QuoteArgument(pathForCommandLine.c_str());
		if (!arguments.empty())
			commandLine.Append(L' ').Append(arguments);

		const auto result = s_originalCreateProcessW(
			lpApplicationName ? applicationPath.c_str() : nullptr,
			lpCommandLine ? const_cast<wchar_t*>(commandLine.c_str()) : nullptr,
			lpProcessAttributes,
			lpThreadAttributes,
			bInheritHandles,
			dwCreationFlags | (noOperation ? 0 : CREATE_SUSPENDED),
			lpEnvironment,
			lpCurrentDirectory,
			lpStartupInfo,
			lpProcessInformation);
		if (!result)
			return FALSE;

		if (!noOperation)
			PostProcessExecution(dwCreationFlags, lpProcessInformation);
		return result;
	}

	BOOL WINAPI DetourCreateProcessA(LPCSTR lpApplicationName, LPSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCSTR lpCurrentDirectory, LPSTARTUPINFOA lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation) {
		using namespace Loader;
		const CallInHook inHook;
		if (!lpStartupInfo) {
			SetLastError(ERROR_INVALID_PARAMETER);
			return FALSE;
		}

		const auto fromAnsi = [](const char* s, Str& out) -> wchar_t* {
			if (!s)
				return nullptr;
			const auto n = static_cast<int>(Length(s));
			const auto len = n ? MultiByteToWideChar(CP_ACP, 0, s, n, nullptr, 0) : 0;
			if (len > 0)
				MultiByteToWideChar(CP_ACP, 0, s, n, out.Buffer(len), len);
			out.Resize(len > 0 ? len : 0);
			return const_cast<wchar_t*>(out.c_str());
		};

		// STARTUPINFOA and STARTUPINFOW differ only in the string pointers; cb may announce a larger structure.
		const auto siw = static_cast<STARTUPINFOW*>(Alloc(lpStartupInfo->cb > sizeof(STARTUPINFOW) ? lpStartupInfo->cb : sizeof(STARTUPINFOW)));
		memcpy(siw, lpStartupInfo, lpStartupInfo->cb);
		Str applicationName, commandLine, currentDirectory, reserved, desktop, title;
		siw->lpReserved = fromAnsi(lpStartupInfo->lpReserved, reserved);
		siw->lpDesktop = fromAnsi(lpStartupInfo->lpDesktop, desktop);
		siw->lpTitle = fromAnsi(lpStartupInfo->lpTitle, title);

		const auto result = DetourCreateProcessW(
			fromAnsi(lpApplicationName, applicationName),
			fromAnsi(lpCommandLine, commandLine),
			lpProcessAttributes,
			lpThreadAttributes,
			bInheritHandles,
			dwCreationFlags,
			lpEnvironment,
			fromAnsi(lpCurrentDirectory, currentDirectory),
			siw,
			lpProcessInformation);
		const auto err = GetLastError();
		Free(siw);
		SetLastError(err);
		return result;
	}

	bool InstallHooks(Loader::Str& error) {
		using namespace Loader;
		if (!s_minHookInitialized) {
			if (const auto res = MH_Initialize(); res != MH_OK && res != MH_ERROR_ALREADY_INITIALIZED) {
				error = Str(L"MH_Initialize: ").AppendDec(static_cast<uint32_t>(res));
				return false;
			}
			s_minHookInitialized = true;
		}

		const auto hKernel32 = GetModuleHandleW(L"kernel32.dll");
		s_pCreateProcessW = reinterpret_cast<void*>(GetProcAddress(hKernel32, "CreateProcessW"));
		s_pCreateProcessA = reinterpret_cast<void*>(GetProcAddress(hKernel32, "CreateProcessA"));
		MH_STATUS res;
		if ((res = MH_CreateHook(s_pCreateProcessW, reinterpret_cast<void*>(&DetourCreateProcessW), reinterpret_cast<void**>(&s_originalCreateProcessW))) != MH_OK) {
			error = Str(L"MH_CreateHook(CreateProcessW): ").AppendDec(static_cast<uint32_t>(res));
			return false;
		}
		if ((res = MH_CreateHook(s_pCreateProcessA, reinterpret_cast<void*>(&DetourCreateProcessA), reinterpret_cast<void**>(&s_originalCreateProcessA))) != MH_OK) {
			MH_RemoveHook(s_pCreateProcessW);
			error = Str(L"MH_CreateHook(CreateProcessA): ").AppendDec(static_cast<uint32_t>(res));
			return false;
		}
		if ((res = MH_EnableHook(s_pCreateProcessW)) != MH_OK || (res = MH_EnableHook(s_pCreateProcessA)) != MH_OK) {
			MH_RemoveHook(s_pCreateProcessA);
			MH_RemoveHook(s_pCreateProcessW);
			error = Str(L"MH_EnableHook: ").AppendDec(static_cast<uint32_t>(res));
			return false;
		}

		// Not every process imports it from kernel32 (e.g. Windows tools use API sets); there is no slot to hook then.
		if ((s_pIsDebuggerPresentSlot = FindImportSlot(GetModuleHandleW(nullptr), "kernel32.dll", "IsDebuggerPresent"))) {
			s_originalIsDebuggerPresent = *s_pIsDebuggerPresentSlot;
			WriteSlot(s_pIsDebuggerPresentSlot, reinterpret_cast<void*>(&DetourIsDebuggerPresent));
		}

		// While the hooks are in place, this DLL stays loaded.
		HMODULE hSelf;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&InstallHooks), &hSelf);
		return true;
	}

	void UninstallHooks() {
		if (s_pIsDebuggerPresentSlot) {
			WriteSlot(s_pIsDebuggerPresentSlot, s_originalIsDebuggerPresent);
			s_pIsDebuggerPresentSlot = nullptr;
		}
		MH_DisableHook(s_pCreateProcessA);
		MH_DisableHook(s_pCreateProcessW);
		// The trampolines go with the hooks; calls still in them finish first.
		while (s_callsInHook)
			Sleep(1);
		MH_RemoveHook(s_pCreateProcessA);
		MH_RemoveHook(s_pCreateProcessW);
		s_originalCreateProcessA = nullptr;
		s_originalCreateProcessW = nullptr;
	}
}

namespace Loader {
	bool g_loadedFromEntryPoint;
}

void Loader::RemoveFollowHooks() {
	AcquireSRWLockExclusive(&s_lock);
	if (s_active) {
		UninstallHooks();
		s_active = false;
	}
	if (s_minHookInitialized) {
		MH_Uninitialize();
		s_minHookInitialized = false;
	}
	ReleaseSRWLockExclusive(&s_lock);
}

namespace {
	bool SetFollowing(size_t flags, Loader::Str& error) {
		using namespace Loader;
		const bool use = flags & InjectFlags::Use;
		auto ok = true;
		auto release = false;
		AcquireSRWLockExclusive(&s_lock);
		if (use && !s_active) {
			ok = s_active = InstallHooks(error);
		} else if (!use && s_active) {
			UninstallHooks();
			s_active = false;
			release = true;
		}
		if (s_active)
			InterlockedExchange(&s_flags, static_cast<LONG>(flags));
		ReleaseSRWLockExclusive(&s_lock);
		// The caller holds a reference of its own, as a remote thread or as code of a module that called in.
		if (release)
			FreeLibrary(g_module);
		return ok;
	}
}

extern "C" size_t EnableInjectOnCreateProcess(size_t flags) {
	Loader::Str error;
	if (SetFollowing(flags, error))
		return 0;
	Loader::DebugPrint(L"EnableInjectOnCreateProcess error: {}", error);
	return static_cast<size_t>(-1);
}

extern "C" size_t DisableAllApps(void*) {
	using namespace Loader;
	EnableInjectOnCreateProcess(0);
#if XA_LOADER64
	if (const auto result = EnableXivAlexander(0))
		return result;
	if (!ReleaseSelfReference())
		return 0;
#endif
	static volatile LONG s_entryPointReferenceReleased;
	if (g_loadedFromEntryPoint && !InterlockedExchange(&s_entryPointReferenceReleased, 1))
		FreeLibrary(g_module);
	return 0;
}

extern "C" void CallFreeLibrary(void*) {
	FreeLibraryAndExitThread(Loader::g_module, 0);
}

// Called by the trampoline at the patched entry point, which it gets back afterwards.
extern "C" void InjectEntryPoint(Loader::InjectEntryPointParameters* pParam) {
	using namespace Loader;
	RestoreEntryPoint(pParam);

#if XA_LOADER64
	if (pParam->LoadedAsProxy) {
		InitializeAsProxyBeforeEntryPoint();
		return;
	}
#endif

	if (Str error; !SetFollowing(InjectFlags::Use, error)) {
		const auto text = Format(LoadStr(IDS_ERROR_INJECT), error, Str::Dec(GetCurrentProcessId()), ProcessImagePath(GetCurrentProcess()), g_originalCommandLine);
		ShowMessageBox(nullptr, text.c_str(), MB_OK | MB_ICONERROR);
		TerminateProcess(GetCurrentProcess(), 1);
	}
	g_loadedFromEntryPoint = true;

#if XA_LOADER64
	if (IEquals(FileNameOf(ProcessImagePath(GetCurrentProcess()).c_str()), GameExecutableName))
		LoadFeaturesBeforeEntryPoint(false);
#endif
}
