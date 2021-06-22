#include "pch.h"
#include "Utils_Win32_Modules.h"
#include "Utils_CallOnDestruction.h"
#include "Utils_Win32.h"
#include "Utils_Win32_Closeable.h"

using Handle = Utils::Win32::Closeable::Handle;

std::vector<DWORD> Utils::Win32::Modules::GetProcessList() {
	std::vector<DWORD> res;
	DWORD cb = 0;
	do {
		res.resize(res.size() + 1024);
		EnumProcesses(&res[0], static_cast<DWORD>(sizeof res[0] * res.size()), &cb);
	} while (cb == sizeof res[0] * res.size());
	res.resize(cb / sizeof res[0]);
	return res;
}

void* Utils::Win32::Modules::GetModulePointer(HANDLE hProcess, const std::filesystem::path& path) {
	const auto th32 = Handle(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetProcessId(hProcess)),
		INVALID_HANDLE_VALUE,
		"Failed to list modules of process {:d}.", GetProcessId(hProcess));
	MODULEENTRY32W mod{ sizeof MODULEENTRY32W };
	if (!Module32FirstW(th32, &mod))
		return nullptr;

	auto buf = path.wstring();
	CharLowerW(&buf[0]);
	do {
		auto buf2 = std::wstring(mod.szExePath);
		CharLowerW(&buf2[0]);
		if (buf == buf2)
			return mod.modBaseAddr;
	} while (Module32NextW(th32, &mod));
	return nullptr;
}

int Utils::Win32::Modules::CallRemoteFunction(HANDLE hProcess, void* rpfn, void* rpParam, const char* pcszDescription) {
	const auto hLoadLibraryThread = Handle(CreateRemoteThread(hProcess, nullptr, 0, static_cast<LPTHREAD_START_ROUTINE>(rpfn), rpParam, 0, nullptr),
		Handle::Null,
		"Failed to call remote function {}@{:p}({:p})", pcszDescription, rpfn, rpParam);
	WaitForSingleObject(hLoadLibraryThread, INFINITE);
	DWORD exitCode;
	GetExitCodeThread(hLoadLibraryThread, &exitCode);
	return exitCode;
}

void* Utils::Win32::Modules::InjectDll(HANDLE hProcess, const std::filesystem::path& path) {
	auto buf = path.wstring();
	buf.resize(buf.size() + 1);

	if (const auto ptr = GetModulePointer(hProcess, &buf[0]))
		return ptr;

	const auto nNumberOfBytes = buf.size() * sizeof buf[0];

	void* rpszDllPath = VirtualAllocEx(hProcess, nullptr, nNumberOfBytes, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	if (!rpszDllPath)
		throw Error(GetLastError(), "VirtualAllocEx(pid {:d}, nullptr, {:d}, MEM_COMMIT, PAGE_EXECUTE_READWRITE)", GetProcessId(hProcess), nNumberOfBytes);

	const auto releaseRemoteAllocation = CallOnDestruction([&]() {
		VirtualFreeEx(hProcess, rpszDllPath, 0, MEM_RELEASE);
		});

	if (!WriteProcessMemory(hProcess, rpszDllPath, &buf[0], nNumberOfBytes, nullptr))
		throw Error(GetLastError(), "WriteProcessMemory(pid {:d}, {:p}, {}, {:d}, nullptr)", GetProcessId(hProcess), rpszDllPath, ToUtf8(buf), nNumberOfBytes);

	CallRemoteFunction(hProcess, LoadLibraryW, rpszDllPath, "LoadLibraryW");
	return GetModulePointer(hProcess, path);
}

void* Utils::Win32::Modules::FindModuleAddress(HANDLE hProcess, const std::filesystem::path& szDllPath) {
	std::vector<HMODULE> hMods;
	DWORD cbNeeded;
	do {
		hMods.resize(hMods.size() + std::min<size_t>(1024, std::max<size_t>(32768, hMods.size())));
		cbNeeded = static_cast<DWORD>(hMods.size());
		if (!EnumProcessModules(hProcess, &hMods[0], static_cast<DWORD>(hMods.size() * sizeof(HMODULE)), &cbNeeded))
			throw Error(std::format("FindModuleAdderss(pid={:d}, path={})/EnumProcessModules", GetProcessId(hProcess), ToUtf8(szDllPath)));
	} while (cbNeeded == hMods.size() * sizeof(HMODULE));
	hMods.resize(cbNeeded / sizeof(HMODULE));

	std::wstring sModName;
	sModName.resize(PATHCCH_MAX_CCH);
	for (const auto hMod : hMods) {
		const auto remoteModulePath = PathFromModule(hMod, hProcess);
		if (remoteModulePath == szDllPath)
			return hMod;
	}
	return nullptr;
}

std::filesystem::path Utils::Win32::Modules::PathFromModule(HMODULE hModule, HANDLE hProcess) {
	auto buf = std::wstring(PATHCCH_MAX_CCH, 0);

	if (hProcess == INVALID_HANDLE_VALUE)
		hProcess = GetCurrentProcess();

	DWORD length;
	if (hProcess == GetCurrentProcess())
		length = GetModuleFileNameW(hModule, &buf[0], static_cast<DWORD>(buf.size()));
	else if (!hModule) {
		length = static_cast<DWORD>(buf.size());
		if (!QueryFullProcessImageNameW(hProcess, 0, &buf[0], &length))
			length = 0;
	} else
		length = GetModuleFileNameExW(hProcess, hModule, &buf[0], static_cast<DWORD>(buf.size()));
	if (!length)
		throw Error("Failed to get module name.");
	buf.resize(length);

	return std::move(buf);
}
