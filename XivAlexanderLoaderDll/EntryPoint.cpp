#include "Base.h"

#include <Psapi.h>

#include "resource.h"
#include "x86.h"
#if XA_LOADER64
#include "x64.h"
#endif

// DllMain, and patching a process's entry point so that it loads the loader DLL of its bitness first.

namespace {
#if XA_LOADER64
	namespace NativeArch = XivAlexander::EntryPoint::X64;
#else
	namespace NativeArch = XivAlexander::EntryPoint::X86;
#endif
	static_assert(sizeof(NativeArch::InjectEntryPointParameters) == sizeof(Loader::InjectEntryPointParameters));
	static_assert(offsetof(NativeArch::InjectEntryPointParameters, LoadedAsProxy) == offsetof(Loader::InjectEntryPointParameters, LoadedAsProxy));

	bool Read(HANDLE hProcess, uint64_t address, void* buf, size_t size) {
		SIZE_T read = 0;
		return ReadProcessMemory(hProcess, reinterpret_cast<void*>(static_cast<uintptr_t>(address)), buf, size, &read) && read == size;
	}

	// The main image: the first committed image region mapped from the process's own file.
	bool FindImageBase(HANDLE hProcess, uintptr_t& base, Loader::Str& error) {
		using namespace Loader;
		if (GetProcessId(hProcess) == GetCurrentProcessId()) {
			base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
			return true;
		}
		const auto image = ProcessImagePath(hProcess, PROCESS_NAME_NATIVE);
		if (image.empty()) {
			error = LastErrorText(L"QueryFullProcessImageNameW");
			return false;
		}
		Str mapped;
		MEMORY_BASIC_INFORMATION mbi{};
		for (uintptr_t address = 0; VirtualQueryEx(hProcess, reinterpret_cast<void*>(address), &mbi, sizeof mbi); address = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
			if (!(mbi.State & MEM_COMMIT) || mbi.Type != MEM_IMAGE)
				continue;
			const auto len = K32GetMappedFileNameW(hProcess, mbi.BaseAddress, mapped.Buffer(32768), 32768);
			mapped.Resize(len);
			if (len && IEquals(mapped.c_str(), image.c_str())) {
				base = reinterpret_cast<uintptr_t>(mbi.AllocationBase);
				return true;
			}
		}
		error = Str(L"Cannot find the image of ").Append(image);
		return false;
	}

#if XA_LOADER64
	uint64_t FindKnownDlls32Kernel32Function(const char* name, Loader::Str& error) {
		using namespace Loader;
		using NtOpenSectionFn = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
		using NtMapViewOfSectionFn = NTSTATUS(NTAPI*)(HANDLE, HANDLE, PVOID*, ULONG_PTR, SIZE_T, PLARGE_INTEGER, PSIZE_T, DWORD, ULONG, ULONG);
		using NtUnmapViewOfSectionFn = NTSTATUS(NTAPI*)(HANDLE, PVOID);
		constexpr NTSTATUS StatusImageNotAtBase = 0x40000003;
		constexpr DWORD ViewUnmap = 2;

		const auto hNtdll = GetModuleHandleW(L"ntdll.dll");
		const auto pNtOpenSection = reinterpret_cast<NtOpenSectionFn>(GetProcAddress(hNtdll, "NtOpenSection"));
		const auto pNtMapViewOfSection = reinterpret_cast<NtMapViewOfSectionFn>(GetProcAddress(hNtdll, "NtMapViewOfSection"));
		const auto pNtUnmapViewOfSection = reinterpret_cast<NtUnmapViewOfSectionFn>(GetProcAddress(hNtdll, "NtUnmapViewOfSection"));
		if (!pNtOpenSection || !pNtMapViewOfSection || !pNtUnmapViewOfSection) {
			error = L"ntdll section functions not found";
			return 0;
		}

		wchar_t sectionName[] = L"\\KnownDlls32\\kernel32.dll";
		UNICODE_STRING us{
			.Length = static_cast<USHORT>(sizeof sectionName - sizeof(wchar_t)),
			.MaximumLength = static_cast<USHORT>(sizeof sectionName),
			.Buffer = sectionName,
		};
		OBJECT_ATTRIBUTES oa{};
		InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

		HANDLE hSection{};
		if (const auto status = pNtOpenSection(&hSection, SECTION_MAP_READ | SECTION_QUERY, &oa); !NT_SUCCESS(status)) {
			error = Str(L"NtOpenSection(\\KnownDlls32\\kernel32.dll) failed: ").AppendHex(static_cast<uint32_t>(status));
			return 0;
		}
		void* base{};
		SIZE_T viewSize{};
		const auto status = pNtMapViewOfSection(hSection, GetCurrentProcess(), &base, 0, 0, nullptr, &viewSize, ViewUnmap, 0, PAGE_READONLY);
		CloseHandle(hSection);
		if (!NT_SUCCESS(status) || !base) {
			error = Str(L"NtMapViewOfSection(\\KnownDlls32\\kernel32.dll) failed: ").AppendHex(static_cast<uint32_t>(status));
			return 0;
		}

		uint64_t result = 0;
		const auto view = static_cast<const uint8_t*>(base);
		const auto fits = [viewSize](size_t offset, size_t size) { return offset <= viewSize && viewSize - offset >= size; };
		do {
			if (status == StatusImageNotAtBase) {
				error = L"\\KnownDlls32\\kernel32.dll was relocated";
				break;
			}
			const auto& dos = *reinterpret_cast<const IMAGE_DOS_HEADER*>(view);
			if (!fits(0, sizeof dos) || !fits(static_cast<size_t>(dos.e_lfanew), sizeof(IMAGE_NT_HEADERS32))) {
				error = L"kernel32 (32-bit) image is truncated";
				break;
			}
			const auto& nt = *reinterpret_cast<const IMAGE_NT_HEADERS32*>(view + dos.e_lfanew);
			if (nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
				error = L"kernel32 (32-bit) is not a PE32 image";
				break;
			}
			const auto& dir = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
			if (!fits(dir.VirtualAddress, sizeof(IMAGE_EXPORT_DIRECTORY))) {
				error = L"kernel32 (32-bit) image is truncated";
				break;
			}
			const auto& exports = *reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(view + dir.VirtualAddress);
			const auto nameLength = Length(name);
			for (DWORD i = 0; i < exports.NumberOfNames && !result; ++i) {
				if (!fits(exports.AddressOfNames + i * sizeof(DWORD), sizeof(DWORD)))
					break;
				const auto nameRva = *reinterpret_cast<const DWORD*>(view + exports.AddressOfNames + i * sizeof(DWORD));
				if (!fits(nameRva, nameLength + 1) || memcmp(view + nameRva, name, nameLength + 1) != 0)
					continue;
				if (!fits(exports.AddressOfNameOrdinals + i * sizeof(WORD), sizeof(WORD)))
					break;
				const auto ordinal = *reinterpret_cast<const WORD*>(view + exports.AddressOfNameOrdinals + i * sizeof(WORD));
				if (ordinal >= exports.NumberOfFunctions || !fits(exports.AddressOfFunctions + ordinal * sizeof(DWORD), sizeof(DWORD)))
					break;
				const auto rva = *reinterpret_cast<const DWORD*>(view + exports.AddressOfFunctions + ordinal * sizeof(DWORD));
				if (dir.VirtualAddress <= rva && rva < dir.VirtualAddress + dir.Size) {
					error = Str(L"kernel32 (32-bit) forwards ").Append(FromUtf8(name, nameLength));
					break;
				}
				result = reinterpret_cast<uintptr_t>(view) + rva;
			}
			if (!result && error.empty())
				error = Str(L"kernel32 (32-bit) does not export ").Append(FromUtf8(name, nameLength));
		} while (false);
		pNtUnmapViewOfSection(GetCurrentProcess(), base);
		return result;
	}
#endif

	uint64_t FindKernel32Function(bool target64, const char* name, Loader::Str& error) {
		if (target64 == !!XA_LOADER64) {
			// The system DLLs are mapped at the same base in every process of the same bitness.
			if (const auto pfn = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), name))
				return reinterpret_cast<uintptr_t>(pfn);
			error = Loader::LastErrorText(L"GetProcAddress(kernel32)");
			return 0;
		}
#if XA_LOADER64
		return FindKnownDlls32Kernel32Function(name, error);
#else
		error = L"A 32-bit process cannot resolve functions of a 64-bit process.";
		return 0;
#endif
	}

	bool WriteCode(HANDLE hProcess, uint64_t address, const void* data, size_t size, Loader::Str& error) {
		const auto p = reinterpret_cast<void*>(static_cast<uintptr_t>(address));
		DWORD oldProtect = 0;
		if (!VirtualProtectEx(hProcess, p, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
			error = Loader::LastErrorText(L"VirtualProtectEx");
			return false;
		}
		SIZE_T written = 0;
		const auto ok = WriteProcessMemory(hProcess, p, data, size, &written) && written == size;
		if (!ok)
			error = Loader::LastErrorText(L"WriteProcessMemory");
		VirtualProtectEx(hProcess, p, size, oldProtect, &oldProtect);
		FlushInstructionCache(hProcess, p, size);
		return ok;
	}

	// The templates are those of the target's architecture, which may differ from this process's.
	template<typename TPointer, typename TThunk, typename TTrampoline>
	void* PatchEntryPointWith(HANDLE hProcess, const wchar_t* dllPath, bool target64, Loader::Str& error) {
		using namespace Loader;
		uintptr_t base;
		if (!FindImageBase(hProcess, base, error))
			return nullptr;

		LONG lfanew = 0;
		DWORD signature = 0, rvaEntryPoint = 0;
		if (!Read(hProcess, base + offsetof(IMAGE_DOS_HEADER, e_lfanew), &lfanew, sizeof lfanew)
			|| !Read(hProcess, base + lfanew, &signature, sizeof signature)
			|| signature != IMAGE_NT_SIGNATURE
			// AddressOfEntryPoint is at the same offset in both optional header layouts.
			|| !Read(hProcess, base + lfanew + offsetof(IMAGE_NT_HEADERS32, OptionalHeader.AddressOfEntryPoint), &rvaEntryPoint, sizeof rvaEntryPoint)) {
			error = LastErrorText(L"Cannot read the image headers");
			return nullptr;
		}
		const uint64_t entryPoint = static_cast<uint64_t>(base) + rvaEntryPoint;

		TTrampoline trampoline{};
		if (!Read(hProcess, entryPoint, trampoline.buf_EntryPointBackup, sizeof trampoline.buf_EntryPointBackup)) {
			error = LastErrorText(L"Cannot read the entry point");
			return nullptr;
		}

		const auto pfnLoadLibraryW = FindKernel32Function(target64, "LoadLibraryW", error);
		const auto pfnGetProcAddress = pfnLoadLibraryW ? FindKernel32Function(target64, "GetProcAddress", error) : 0;
		if (!pfnLoadLibraryW || !pfnGetProcAddress)
			return nullptr;

		const auto pathBytes = (Length(dllPath) + 1) * sizeof(wchar_t);
		constexpr auto trampolineLength = (sizeof(TTrampoline) + 7) / 8 * 8;
		const auto totalLength = trampolineLength + pathBytes;

		const auto remote = static_cast<char*>(VirtualAllocEx(hProcess, nullptr, totalLength, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
		if (!remote) {
			error = LastErrorText(L"VirtualAllocEx");
			return nullptr;
		}
		const auto remoteAddress = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(remote));
		constexpr auto maxPointer = static_cast<uint64_t>(static_cast<TPointer>(-1));
		if (remoteAddress + totalLength > maxPointer || entryPoint > maxPointer || pfnLoadLibraryW > maxPointer || pfnGetProcAddress > maxPointer) {
			VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
			error = L"Address does not fit in the target's pointer size";
			return nullptr;
		}
		const auto rbase = static_cast<TPointer>(remoteAddress);

		trampoline.parameters.EntryPoint = static_cast<TPointer>(entryPoint);
		trampoline.parameters.EntryPointOriginalBytes = static_cast<TPointer>(rbase + offsetof(TTrampoline, buf_EntryPointBackup));
		trampoline.parameters.EntryPointOriginalLength = static_cast<TPointer>(sizeof trampoline.buf_EntryPointBackup);
		trampoline.parameters.TrampolineAddress = rbase;
		trampoline.CallLoadLibrary.lpLibFileName.val = static_cast<TPointer>(rbase + trampolineLength);
		trampoline.CallLoadLibrary.fn.ptr = static_cast<TPointer>(pfnLoadLibraryW);
		trampoline.CallGetProcAddress.lpProcName.val = static_cast<TPointer>(rbase + offsetof(TTrampoline, buf_CallGetProcAddress_lpProcName));
		trampoline.CallGetProcAddress.fn.ptr = static_cast<TPointer>(pfnGetProcAddress);
		trampoline.CallInjectEntryPoint.param.val = static_cast<TPointer>(rbase + offsetof(TTrampoline, parameters));

		const auto buffer = static_cast<char*>(Alloc(totalLength));
		memcpy(buffer, &trampoline, sizeof trampoline);
		memcpy(buffer + trampolineLength, dllPath, pathBytes);
		SIZE_T written = 0;
		const auto ok = WriteProcessMemory(hProcess, remote, buffer, totalLength, &written) && written == totalLength;
		Free(buffer);
		if (!ok) {
			error = LastErrorText(L"WriteProcessMemory");
			VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
			return nullptr;
		}
		FlushInstructionCache(hProcess, remote, totalLength);

		TThunk thunk{};
		thunk.CallTrampoline.fn.ptr = rbase;
		if (!WriteCode(hProcess, entryPoint, &thunk, sizeof thunk, error)) {
			VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
			return nullptr;
		}
		return remote + offsetof(TTrampoline, parameters);
	}
}

void* Loader::PatchEntryPoint(HANDLE hProcess, Str& error) {
	using namespace XivAlexander::EntryPoint;
	if (IsProcess64(hProcess)) {
#if XA_LOADER64
		return PatchEntryPointWith<X64::Pointer, X64::EntryPointThunkTemplate, X64::TrampolineTemplate>(hProcess, g_modulePath, true, error);
#else
		error = L"A 64-bit process can only be patched by the 64-bit loader.";
		return nullptr;
#endif
	}
#if XA_LOADER64
	const auto dll32 = Join(ProgramDirectory(), LoaderDll32Name);
	if (!Exists(dll32.c_str())) {
		error = Format(L"{} not found", dll32);
		return nullptr;
	}
	return PatchEntryPointWith<X86::Pointer, X86::EntryPointThunkTemplate, X86::TrampolineTemplate>(hProcess, dll32.c_str(), false, error);
#else
	return PatchEntryPointWith<X86::Pointer, X86::EntryPointThunkTemplate, X86::TrampolineTemplate>(hProcess, g_modulePath, false, error);
#endif
}

void Loader::RestoreEntryPoint(const InjectEntryPointParameters* p) {
	DWORD oldProtect = 0;
	VirtualProtect(p->EntryPoint, p->EntryPointOriginalLength, PAGE_EXECUTE_READWRITE, &oldProtect);
	memcpy(p->EntryPoint, p->EntryPointOriginalBytes, p->EntryPointOriginalLength);
	VirtualProtect(p->EntryPoint, p->EntryPointOriginalLength, oldProtect, &oldProtect);
	FlushInstructionCache(GetCurrentProcess(), p->EntryPoint, p->EntryPointOriginalLength);
}

#if XA_LOADER64
// For XivAlexanderLoader64.exe: patches a process of either bitness; null with the error as text on failure.
extern "C" void* __stdcall XA_PatchEntryPoint(HANDLE hProcess, wchar_t* pszError, DWORD cchError) {
	Loader::Str error;
	const auto result = Loader::PatchEntryPoint(hProcess, error);
	if (!result && pszError && cchError)
		lstrcpynW(pszError, error.c_str(), static_cast<int>(cchError));
	return result;
}
#endif

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD fdwReason, LPVOID lpReserved) {
	using namespace Loader;
	switch (fdwReason) {
		case DLL_PROCESS_ATTACH: {
			g_module = hInstance;
			DisableThreadLibraryCalls(hInstance);

			const auto copy = [](const Str& s) {
				const auto p = static_cast<wchar_t*>(Alloc((s.size() + 1) * sizeof(wchar_t)));
				memcpy(p, s.c_str(), s.size() * sizeof(wchar_t));
				return static_cast<const wchar_t*>(p);
			};
			g_originalCommandLine = copy(GetCommandLineW());
			const auto path = ModulePath(hInstance);
			g_modulePath = copy(path);
			g_moduleDirectory = copy(DirectoryOf(path.c_str()));

#if XA_LOADER64
			// lpReserved is non-null for static loads.
			AttachAsProxyIfApplicable(!!lpReserved);
#endif
			return TRUE;
		}

		case DLL_PROCESS_DETACH:
			// At process exit the other threads are gone already; only an unload needs the hooks taken out.
			if (!lpReserved)
				RemoveFollowHooks();
			return TRUE;
	}
	return TRUE;
}
