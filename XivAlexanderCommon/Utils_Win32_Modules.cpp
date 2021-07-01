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

int Utils::Win32::Modules::CallRemoteFunction(HANDLE hProcess, void* rpfn, void* rpParam, const char* pcszDescription) {
	const auto hLoadLibraryThread = Handle(CreateRemoteThread(hProcess, nullptr, 0, static_cast<LPTHREAD_START_ROUTINE>(rpfn), rpParam, 0, nullptr),
		Handle::Null,
		"Failed to call remote function {}@{:p}({:p})", pcszDescription, rpfn, rpParam);
	WaitForSingleObject(hLoadLibraryThread, INFINITE);
	DWORD exitCode;
	GetExitCodeThread(hLoadLibraryThread, &exitCode);
	return exitCode;
}

HMODULE Utils::Win32::Modules::InjectDll(HANDLE hProcess, const std::filesystem::path& path) {
	auto buf = path.wstring();
	buf.resize(buf.size() + 1);

	const auto nNumberOfBytes = buf.size() * sizeof buf[0];

	void* rpszDllPath = VirtualAllocEx(hProcess, nullptr, nNumberOfBytes, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	if (!rpszDllPath)
		throw Error(GetLastError(), "VirtualAllocEx(pid {}, nullptr, {}, MEM_COMMIT, PAGE_EXECUTE_READWRITE)", GetProcessId(hProcess), nNumberOfBytes);

	const auto releaseRemoteAllocation = CallOnDestruction([&]() {
		VirtualFreeEx(hProcess, rpszDllPath, 0, MEM_RELEASE);
		});

	if (!WriteProcessMemory(hProcess, rpszDllPath, &buf[0], nNumberOfBytes, nullptr))
		throw Error(GetLastError(), "WriteProcessMemory(pid {}, {:p}, {}, {}, nullptr)", GetProcessId(hProcess), rpszDllPath, ToUtf8(buf), nNumberOfBytes);

	CallRemoteFunction(hProcess, LoadLibraryW, rpszDllPath, "LoadLibraryW");
	OutputDebugStringW(L"LoadLibraryW\n");
	const auto res = FindModuleAddress(hProcess, path);
	if (!res)
		throw std::runtime_error("InjectDll failure");
	return res;
}

HMODULE Utils::Win32::Modules::FindModuleAddress(HANDLE hProcess, const std::filesystem::path& szDllPath) {
	std::vector<HMODULE> hMods;
	DWORD cbNeeded;
	do {
		hMods.resize(hMods.size() + std::min<size_t>(1024, std::max<size_t>(32768, hMods.size())));
		cbNeeded = static_cast<DWORD>(hMods.size());
		if (!EnumProcessModules(hProcess, &hMods[0], static_cast<DWORD>(hMods.size() * sizeof(HMODULE)), &cbNeeded))
			throw Error(std::format("FindModuleAdderss(pid={}, path={})/EnumProcessModules", GetProcessId(hProcess), ToUtf8(szDllPath)));
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

static HANDLE DuplicateHandleNullable(HANDLE src) {
	if (!src)
		return nullptr;
	if (HANDLE dst; DuplicateHandle(GetCurrentProcess(), src, GetCurrentProcess(), &dst, 0, FALSE, DUPLICATE_SAME_ACCESS))
		return dst;
	throw Utils::Win32::Error("DuplicateHandle");
}

Utils::Win32::Modules::InjectedModule::InjectedModule()
	: m_rpModule(nullptr)
	, m_path()
	, m_hProcess(nullptr) {
}

Utils::Win32::Modules::InjectedModule::InjectedModule(HANDLE hProcess, std::filesystem::path path)
	: m_rpModule(InjectDll(hProcess, path))
	, m_path(std::move(path))
	, m_hProcess(DuplicateHandleNullable(hProcess)) {
}

Utils::Win32::Modules::InjectedModule::InjectedModule(const InjectedModule& r)
	: InjectedModule(r.m_hProcess, r.m_path) {
}

Utils::Win32::Modules::InjectedModule::InjectedModule(InjectedModule&& r) noexcept
	: m_rpModule(r.m_rpModule)
	, m_path(std::move(r.m_path))
	, m_hProcess(r.m_hProcess) {
	r.m_hProcess = nullptr;
	r.m_rpModule = nullptr;
}

Utils::Win32::Modules::InjectedModule& Utils::Win32::Modules::InjectedModule::operator=(const InjectedModule& r) {
	if (&r == this)
		return *this;

	Clear();

	m_rpModule = r.m_hProcess ? InjectDll(r.m_hProcess, m_path) : nullptr;
	m_path = r.m_path;
	m_hProcess = DuplicateHandleNullable(r.m_hProcess);
	return *this;
}

Utils::Win32::Modules::InjectedModule& Utils::Win32::Modules::InjectedModule::operator=(InjectedModule&& r) noexcept {
	if (&r == this)
		return *this;

	m_path = r.m_path;
	m_hProcess = r.m_hProcess;
	m_rpModule = r.m_rpModule;
	r.m_path.clear();
	r.m_hProcess = nullptr;
	r.m_rpModule = nullptr;
	return *this;
}

Utils::Win32::Modules::InjectedModule::~InjectedModule() {
	Clear();
}

int Utils::Win32::Modules::InjectedModule::Call(void* rpfn, void* rpParam, const char* pcszDescription) const {
	try {
		return CallRemoteFunction(m_hProcess, rpfn, rpParam, pcszDescription);
	} catch (std::exception&) {
		// process already gone
		if (WaitForSingleObject(m_hProcess, 0) != WAIT_TIMEOUT)
			return -1;
		throw;
	}
}

int Utils::Win32::Modules::InjectedModule::Call(const char* name, void* rpParam, const char* pcszDescription) const {
	auto addr = GetProcAddress(m_rpModule, name);
	if (!addr)
		addr = GetProcAddress(m_rpModule, std::format("{}@8", name).c_str());
	return Call(addr, rpParam, pcszDescription);
}

void Utils::Win32::Modules::InjectedModule::Clear() {
	if (m_hProcess) {
		if (m_rpModule) {
			OutputDebugStringW(L"FreeLibrary\n");

			void* freePtr = FreeLibrary;
			const auto a = GetModuleHandleW(L"XivAlexander.dll");
			if (a) {
				freePtr = GetProcAddress(a, "CallFreeLibrary");
				if (!freePtr)
					freePtr = GetProcAddress(a, "CallFreeLibrary@8");
				if (!freePtr)
					freePtr = FreeLibrary;
			}
			
			try {
				CallRemoteFunction(m_hProcess, freePtr, m_rpModule, "(Call)FreeLibrary");
			} catch (std::exception&) {
				// do nothing
			}
		}
		CloseHandle(m_hProcess);
	}

	m_path.clear();
	m_hProcess = nullptr;
	m_rpModule = nullptr;
}
