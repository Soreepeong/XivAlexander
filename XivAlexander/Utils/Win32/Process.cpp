#include "pch.h"
#include "Utils/Win32/Process.h"

#include <xivres/util.on_dtor.h>

Utils::Win32::Process::Process() noexcept = default;

Utils::Win32::Process::Process(std::nullptr_t) noexcept
	: Process() {}

Utils::Win32::Process::Process(HANDLE hProcess, bool ownership) noexcept
	: Handle(hProcess, ownership) {}

Utils::Win32::Process::Process(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwProcessId)
	: Handle(OpenProcess(dwDesiredAccess, bInheritHandle, dwProcessId)
		, Null
		, "OpenProcess({:x}, {}, {})", dwDesiredAccess, bInheritHandle ? "TRUE" : "FALSE", dwProcessId) {}

Utils::Win32::Process::Process(Process&& r) noexcept
	: Handle(r.m_object, r.m_bOwnership)
	, m_moduleMemory(std::move(r.m_moduleMemory)) {
	r.Detach();
}

Utils::Win32::Process::Process(const Process& r)
	: Handle(r)
	, m_moduleMemory(r.m_moduleMemory) {}

Utils::Win32::Process::~Process() noexcept = default;

Utils::Win32::Process& Utils::Win32::Process::operator=(Process&& r) noexcept {
	if (&r == this)
		return *this;

	Clear();
	m_object = r.m_object;
	m_bOwnership = r.m_bOwnership;
	m_moduleMemory = std::move(r.m_moduleMemory);
	r.Detach();
	return *this;
}

Utils::Win32::Process& Utils::Win32::Process::operator=(const Process& r) {
	if (&r == this)
		return *this;

	Handle::operator=(r);
	m_moduleMemory = r.m_moduleMemory;
	return *this;
}

Utils::Win32::Process& Utils::Win32::Process::operator=(std::nullptr_t) noexcept {
	Clear();
	return *this;
}

Utils::Win32::Process& Utils::Win32::Process::Current() {
	static Process current{GetCurrentProcess(), false};
	return current;
}

Utils::Win32::ProcessBuilder::ProcessBuilder() = default;

Utils::Win32::ProcessBuilder::ProcessBuilder(ProcessBuilder&&) noexcept = default;

Utils::Win32::ProcessBuilder& Utils::Win32::ProcessBuilder::operator=(ProcessBuilder&&) noexcept = default;

Utils::Win32::ProcessBuilder::~ProcessBuilder() = default;

std::pair<Utils::Win32::Process, Utils::Win32::Thread> Utils::Win32::ProcessBuilder::Run() {
	const auto MaxLengthOfProcThreadAttributeList = 2UL;
	STARTUPINFOEXW siex{};
	siex.StartupInfo.cb = sizeof siex;

	std::vector<HANDLE> handles;  // this needs to be here, as ProcThreadAttribute only points here instead of copying the contents
	handles.reserve(m_inheritedHandles.size());
	std::ranges::transform(m_inheritedHandles, std::back_inserter(handles), [](const auto& v) { return static_cast<HANDLE>(v); });

	std::vector<char> attributeListBuf;
	if (SIZE_T size = 0; !InitializeProcThreadAttributeList(nullptr, MaxLengthOfProcThreadAttributeList, 0, &size)) {
		const auto err = GetLastError();
		if (err != ERROR_INSUFFICIENT_BUFFER)
			throw Error(err, "InitializeProcThreadAttributeList.1");
		attributeListBuf.resize(size);
		siex.lpAttributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(&attributeListBuf[0]);
		if (!InitializeProcThreadAttributeList(siex.lpAttributeList, MaxLengthOfProcThreadAttributeList, 0, &size))
			throw Error("InitializeProcThreadAttributeList.2");
	}
	const auto cleanAttributeList = xivres::util::on_dtor([&siex]() {
		if (siex.lpAttributeList)
			DeleteProcThreadAttributeList(siex.lpAttributeList);
	});

	if (!handles.empty() && !UpdateProcThreadAttribute(siex.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &handles[0], handles.size() * sizeof handles[0], nullptr, nullptr))
		throw Error("UpdateProcThreadAttribute(PROC_THREAD_ATTRIBUTE_HANDLE_LIST)");

	if (m_parentProcess) {
		auto hParentProcess = static_cast<HANDLE>(m_parentProcess);
		if (!UpdateProcThreadAttribute(siex.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS, &hParentProcess, sizeof hParentProcess, nullptr, nullptr))
			throw Error("UpdateProcThreadAttribute(PROC_THREAD_ATTRIBUTE_PARENT_PROCESS)");
	}

	std::wstring args;
	if (m_bPrependPathToArgument) {
		args = ReverseCommandLineToArgv(m_path.wstring());
		if (!m_args.empty()) {
			args += L" ";
			args += m_args;
		}
	} else
		args = m_args;

	std::wstring environString;
	if (m_environInitialized) {
		for (const auto& item : m_environ) {
			environString.append(item.first);
			environString.push_back(L'=');
			environString.append(item.second);
			environString.push_back(0);
		}
		environString.push_back(0);
	}

	PROCESS_INFORMATION pi{};
	if (!CreateProcessW(m_path.c_str(), args.data(),
		nullptr, nullptr,
		handles.empty() ? FALSE : TRUE,
		CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
		environString.empty() ? nullptr : environString.data(),
		m_dir.empty() ? nullptr : m_dir.c_str(),
		&siex.StartupInfo,
		&pi))
		throw Error("CreateProcess");

	assert(pi.hThread);
	assert(pi.hProcess);

	return {
		Process(pi.hProcess, true),
		Thread(pi.hThread, true)
	};
}

Utils::Win32::ProcessBuilder& Utils::Win32::ProcessBuilder::WithParent(HWND hWnd) {
	if (!hWnd) {
		m_parentProcess.Clear();
		return *this;
	}

	DWORD pid;
	if (!GetWindowThreadProcessId(hWnd, &pid))
		throw Error("GetWindowThreadProcessId({})", reinterpret_cast<size_t>(static_cast<void*>(hWnd)));
	return WithParent({PROCESS_CREATE_PROCESS, FALSE, pid});
}

Utils::Win32::ProcessBuilder& Utils::Win32::ProcessBuilder::WithParent(Process h) {
	m_parentProcess = std::move(h);
	return *this;
}

Utils::Win32::ProcessBuilder& Utils::Win32::ProcessBuilder::WithPath(std::filesystem::path path) {
	m_path = std::move(path);
	return *this;
}

Utils::Win32::ProcessBuilder& Utils::Win32::ProcessBuilder::WithWorkingDirectory(std::filesystem::path dir) {
	m_dir = std::move(dir);
	return *this;
}

Utils::Win32::ProcessBuilder& Utils::Win32::ProcessBuilder::WithArgument(bool prependPathToArgument, const std::string& s) {
	return WithArgument(prependPathToArgument, xivres::util::unicode::convert<std::wstring>(s));
}

Utils::Win32::ProcessBuilder& Utils::Win32::ProcessBuilder::WithArgument(bool prependPathToArgument, std::wstring args) {
	m_bPrependPathToArgument = prependPathToArgument;
	m_args = std::move(args);
	return *this;
}

Utils::Win32::ProcessBuilder& Utils::Win32::ProcessBuilder::WithAppendArgument(const std::string& s) {
	return WithAppendArgument(xivres::util::unicode::convert<std::wstring>(s));
}

Utils::Win32::ProcessBuilder& Utils::Win32::ProcessBuilder::WithAppendArgument(const std::wstring& s) {
	if (!m_args.empty())
		m_args += L" ";
	m_args += ReverseCommandLineToArgv(s);
	return *this;
}

Utils::Win32::ProcessBuilder& Utils::Win32::ProcessBuilder::WithAppendArgument(std::initializer_list<std::string> list) {
	for (const auto& s : list)
		WithAppendArgument(s);
	return *this;
}

Utils::Win32::ProcessBuilder& Utils::Win32::ProcessBuilder::WithAppendArgument(std::initializer_list<std::wstring> list) {
	for (const auto& s : list)
		WithAppendArgument(s);
	return *this;
}

Utils::Win32::ProcessBuilder& Utils::Win32::ProcessBuilder::WithEnviron(std::wstring_view key, std::wstring value) {
	InitializeEnviron();
	m_environ.emplace(key, std::move(value));
	return *this;
}

Utils::Win32::ProcessBuilder& Utils::Win32::ProcessBuilder::WithoutEnviron(const std::wstring& key) {
	InitializeEnviron();
	m_environ.erase(key);
	return *this;
}

Utils::Win32::Handle Utils::Win32::ProcessBuilder::Inherit(HANDLE hSource) {
	auto h = Handle::DuplicateFrom<Handle>(hSource, true);
	auto ret = Handle(h, false);
	m_inheritedHandles.emplace_back(std::move(h));
	return ret;
}

void Utils::Win32::ProcessBuilder::InitializeEnviron() {
	if (m_environInitialized)
		return;
	auto ptr = GetEnvironmentStringsW();
	const auto ptrFree = xivres::util::on_dtor([ptr]() { FreeEnvironmentStringsW(ptr); });
	while (*ptr) {
		const auto part = std::wstring_view(ptr, wcslen(ptr));
		const auto eqPos = part.find(L'=', 1);
		if (eqPos != std::wstring_view::npos)
			m_environ.emplace(part.substr(0, eqPos), part.substr(eqPos + 1));
		else
			m_environ.emplace(part, std::wstring());
		ptr += part.size() + 1;
	}
	m_environInitialized = true;
}

namespace {
	xivres::util::on_dtor WithRunAsInvoker() {
		static constexpr auto NeverElevateEnvKey = L"__COMPAT_LAYER";
		static constexpr auto NeverElevateEnvVal = L"RunAsInvoker";

		std::wstring env;
		env.resize(32768);
		env.resize(GetEnvironmentVariableW(NeverElevateEnvKey, env.data(), static_cast<DWORD>(env.size())));
		const auto envNone = env.empty() && GetLastError() == ERROR_ENVVAR_NOT_FOUND;
		if (!envNone && env.empty())
			throw Utils::Win32::Error("GetEnvironmentVariableW");
		if (!SetEnvironmentVariableW(NeverElevateEnvKey, NeverElevateEnvVal))
			throw Utils::Win32::Error("SetEnvironmentVariableW");
		return {
			[env = std::move(env), envNone]() {
				SetEnvironmentVariableW(NeverElevateEnvKey, envNone ? nullptr : env.data());
			}
		};
	}
}

Utils::Win32::Process Utils::Win32::RunProgram(RunProgramParams params) {
	xivres::util::on_dtor::multi cleanup;

	if (params.path.empty())
		params.path = Process::Current().PathOf();
	else if (!exists(params.path)) {
		std::wstring buf;
		buf.resize(PATHCCH_MAX_CCH);
		buf.resize(SearchPathW(nullptr, params.path.c_str(), L".exe", static_cast<DWORD>(buf.size()), buf.data(), nullptr));
		if (buf.empty())
			throw Error("SearchPath");
		params.path = buf;
	}

	switch (params.elevateMode) {
		case RunProgramParams::Normal:
		case RunProgramParams::Force:
		case RunProgramParams::NeverUnlessAlreadyElevated: {
			if (params.elevateMode == RunProgramParams::NeverUnlessAlreadyElevated)
				cleanup += WithRunAsInvoker();

			SHELLEXECUTEINFOW sei{};
			sei.cbSize = sizeof sei;
			sei.fMask = SEE_MASK_NOCLOSEPROCESS;
			sei.lpVerb = params.elevateMode == RunProgramParams::Force ? L"runas" : L"open";
			sei.lpFile = params.path.c_str();
			sei.lpParameters = params.args.c_str();
			sei.lpDirectory = params.dir.empty() ? nullptr : params.dir.c_str();
			sei.nShow = SW_SHOW;
			if (!ShellExecuteExW(&sei)) {
				const auto err = GetLastError();
				if (err == ERROR_CANCELLED && !params.throwOnCancel)
					return {};

				throw Error(err, "ShellExecuteExW");
			}
			if (!sei.hProcess)  // should not happen, unless registry is messed up.
				throw std::runtime_error("Failed to execute a new program.");
			if (params.wait)
				WaitForSingleObject(sei.hProcess, INFINITE);
			return {sei.hProcess, true};
		}

		case RunProgramParams::NeverUnlessShellIsElevated:
		case RunProgramParams::CancelIfRequired:
		case RunProgramParams::NoElevationIfDenied: {
			if (params.elevateMode == RunProgramParams::NeverUnlessShellIsElevated) {
				if (!IsUserAnAdmin()) {
					params.elevateMode = RunProgramParams::NeverUnlessAlreadyElevated;
					return RunProgram(params);
				}
				cleanup += WithRunAsInvoker();
			}

			try {
				const auto [process, thread] = ProcessBuilder()
					.WithPath(params.path)
					.WithParent(params.elevateMode == RunProgramParams::NeverUnlessShellIsElevated ? GetShellWindow() : nullptr)
					.WithWorkingDirectory(params.dir)
					.WithArgument(true, params.args)
					.Run();
				if (params.wait)
					process.Wait();
				return process;
			} catch (const Error& e) {
				if (e.Code() == ERROR_ELEVATION_REQUIRED) {
					if (params.elevateMode == RunProgramParams::CancelIfRequired && !params.throwOnCancel)
						return {};
					else if (params.elevateMode == RunProgramParams::NoElevationIfDenied) {
						params.elevateMode = RunProgramParams::Normal;
						params.throwOnCancel = false;
						if (auto res = RunProgram(params))
							return res;

						params.elevateMode = RunProgramParams::NeverUnlessAlreadyElevated;
						return RunProgram(params);
					}
				}
				throw;
			}
		}
	}
	throw std::out_of_range("invalid elevateMode");
}

Utils::Win32::Process& Utils::Win32::Process::Attach(HANDLE r, bool ownership, const std::string& errorMessage) {
	Handle::Attach(r, Null, ownership, errorMessage);
	return *this;
}

HANDLE Utils::Win32::Process::Detach() noexcept {
	m_moduleMemory.clear();
	return Handle::Detach();
}

void Utils::Win32::Process::Clear() noexcept {
	m_moduleMemory.clear();
	Handle::Clear();
}

std::vector<HMODULE> Utils::Win32::Process::EnumModules() const {
	// Plain EnumProcessModules from x64 lists only the exe, the 64-bit ntdll and the WOW64 DLLs of a 32-bit process.
	const auto filter = IsSameBitnessAsCurrent() ? LIST_MODULES_DEFAULT : LIST_MODULES_32BIT;

	std::vector<HMODULE> hModules(256);
	while (true) {
		DWORD cbNeeded = 0;
		if (!EnumProcessModulesEx(m_object, hModules.data(), static_cast<DWORD>(hModules.size() * sizeof(HMODULE)), &cbNeeded, filter))
			throw Error(std::format("EnumProcessModulesEx(pid={})", GetProcessId(m_object)));
		if (cbNeeded <= hModules.size() * sizeof(HMODULE)) {
			hModules.resize(cbNeeded / sizeof(HMODULE));
			return hModules;
		}
		hModules.resize(cbNeeded / sizeof(HMODULE) + 64);
	}
}

HMODULE Utils::Win32::Process::AddressOf(std::filesystem::path path, ModuleNameCompareMode compareMode, bool require) const {
	const auto normalize = [compareMode](std::filesystem::path p) {
		switch (compareMode) {
			case ModuleNameCompareMode::FullPath: {
				std::error_code ec;
				if (auto c = canonical(p, ec); !ec)
					return c;
				return p;
			}
			case ModuleNameCompareMode::FileNameWithExtension:
				return p.filename();
			case ModuleNameCompareMode::FileNameWithoutExtension:
				return p.filename().replace_extension();
		}
		return p;
	};
	path = normalize(std::move(path));

	for (const auto hModule : EnumModules()) {
		std::filesystem::path remoteModulePath;
		try {
			remoteModulePath = normalize(PathOf(hModule));
		} catch (const Error&) {
			continue;  // unloaded meanwhile
		}
		if (_wcsicmp(remoteModulePath.c_str(), path.c_str()) == 0)
			return hModule;
	}
	if (require)
		throw std::out_of_range("module not found");
	return nullptr;
}

std::filesystem::path Utils::Win32::Process::PathOf(HMODULE hModule) const {
	auto buf = std::wstring(PATHCCH_MAX_CCH, 0);

	DWORD length;
	if (IsCurrentProcessPseudoHandle())
		length = GetModuleFileNameW(hModule, buf.data(), static_cast<DWORD>(buf.size()));
	else if (!hModule) {
		length = static_cast<DWORD>(buf.size());
		if (!QueryFullProcessImageNameW(m_object, 0, buf.data(), &length))
			length = 0;
	} else
		length = GetModuleFileNameExW(m_object, hModule, buf.data(), static_cast<DWORD>(buf.size()));
	if (!length)
		throw Error("Failed to get module name.");
	buf.resize(length);

	return std::move(buf);
}

bool Utils::Win32::Process::IsCurrentProcessPseudoHandle() const {
	return m_object == GetCurrentProcess();
}

bool Utils::Win32::Process::IsProcess64Bits() const {
	BOOL res;
	if (IsWow64Process(m_object, &res) && res) {
		return false;
	}
	SYSTEM_INFO si;
	GetNativeSystemInfo(&si);
	return si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64;
}

bool Utils::Win32::Process::IsSameBitnessAsCurrent() const {
	return IsProcess64Bits() == (INTPTR_MAX == INT64_MAX);
}

DWORD Utils::Win32::Process::GetId() const {
	return GetProcessId(m_object);
}

void Utils::Win32::Process::Terminate(DWORD dwExitCode = 0, bool errorIfAlreadyTerminated) const {
	if (TerminateProcess(m_object, dwExitCode))
		return;
	const auto err = GetLastError();
	if (errorIfAlreadyTerminated && Wait(0) != WAIT_TIMEOUT)
		throw Error(err, "TerminateProcess");
	return;
}

DWORD Utils::Win32::Process::WaitAndGetExitCode() const {
	Wait();

	DWORD ex;
	if (!GetExitCodeProcess(m_object, &ex))
		throw Error("GetExitCodeProcess");
	return ex;
}

int Utils::Win32::Process::CallRemoteFunction(void* rpfn, void* rpParam, const char* pcszDescription) const {
	if (!IsProcess64Bits() && (reinterpret_cast<uintptr_t>(rpfn) > UINT32_MAX || reinterpret_cast<uintptr_t>(rpParam) > UINT32_MAX))
		throw std::out_of_range(std::format("{}: address out of range for a 32-bit process", pcszDescription));

	const auto hLoadLibraryThread = Handle(CreateRemoteThread(m_object, nullptr, 0, static_cast<LPTHREAD_START_ROUTINE>(rpfn), rpParam, 0, nullptr),
		Null,
		"Failed to call remote function {}@{:p}({:p})", pcszDescription, rpfn, rpParam);
	WaitForSingleObject(hLoadLibraryThread, INFINITE);
	DWORD exitCode;
	GetExitCodeThread(hLoadLibraryThread, &exitCode);
	return exitCode;
}

std::pair<void*, void*> Utils::Win32::Process::FindImportedFunction(HMODULE hModule, const std::filesystem::path& dllName, const char* pszFunctionName, uint32_t hintOrOrdinal) const {
	const auto fileName = xivres::util::unicode::convert<std::string>(dllName.filename().wstring());

	auto& mem = GetModuleMemoryBlockManager(hModule);
	for (const auto& importTableItem : mem.ReadDataDirectory<IMAGE_IMPORT_DESCRIPTOR>(IMAGE_DIRECTORY_ENTRY_IMPORT)) {
		if (!importTableItem.OriginalFirstThunk)
			break;

		if (!importTableItem.Name)
			continue;

		const auto pModuleName = mem.ReadAligned<char>(importTableItem.Name);
		if (_strcmpi(pModuleName.data(), fileName.c_str()))
			continue;

		if (!importTableItem.OriginalFirstThunk)
			continue;

		const auto pImportLookupTable = mem.ReadAligned<size_t>(importTableItem.OriginalFirstThunk);
		const auto pImportAddressTable = mem.ReadAligned<size_t>(importTableItem.FirstThunk);

		for (size_t j = 0;
			j < pImportAddressTable.size() && j < pImportLookupTable.size() && pImportLookupTable[j];
			++j) {
			if (IMAGE_SNAP_BY_ORDINAL(pImportLookupTable[j])) {
				if (!hintOrOrdinal || IMAGE_ORDINAL(pImportLookupTable[j]) != hintOrOrdinal)
					continue;
			} else {
				const auto pName = mem.ReadAligned<IMAGE_IMPORT_BY_NAME>(pImportLookupTable[j]);
				const auto remaining = pName.size() * sizeof pName[0] + reinterpret_cast<const char*>(&pName[0]) - &pName[0].Name[0];
				if ((!hintOrOrdinal || pName[0].Hint != hintOrOrdinal)
					&& (!pszFunctionName || 0 != strncmp(pszFunctionName, pName[0].Name, remaining)))
					continue;
			}

			return std::make_pair<void*, void*>(
				reinterpret_cast<char*>(hModule) + importTableItem.FirstThunk + j * sizeof size_t,
				reinterpret_cast<void*>(pImportAddressTable[j])
			);
		}
	}

	return std::make_pair(nullptr, nullptr);
}

void* Utils::Win32::Process::FindExportedFunction(HMODULE hModule, const char* pszFunctionName, USHORT ordinal, bool require) const {
	auto& mem = GetModuleMemoryBlockManager(hModule);
	const auto dataDirectoryBuffer = mem.ReadDataDirectory<IMAGE_EXPORT_DIRECTORY>(IMAGE_DIRECTORY_ENTRY_EXPORT);
	const auto pExportTable = dataDirectoryBuffer.data();
	const auto pNames = mem.ReadAligned<DWORD>(pExportTable->AddressOfNames, pExportTable->NumberOfNames);
	const auto pOrdinals = mem.ReadAligned<WORD>(pExportTable->AddressOfNameOrdinals, pExportTable->NumberOfNames);
	const auto pFunctions = mem.ReadAligned<DWORD>(pExportTable->AddressOfFunctions, pExportTable->NumberOfFunctions);

	for (DWORD i = 0; i < pExportTable->NumberOfNames; ++i) {
		if (!pNames.empty() && pNames[i]) {
			const auto pName = mem.ReadAligned<char>(pNames[i]);
			if (strncmp(pName.data(), pszFunctionName, pName.size()) != 0)
				continue;
		} else if (!ordinal)
			continue;  // invalid; either one of name or ordinal shoould exist
		else if (ordinal != pOrdinals[i])
			continue;  // exported by ordinal; ordinal mismatch

		BoundaryCheck(pOrdinals[i], 0, pExportTable->NumberOfFunctions);
		const auto rvaAddress = pFunctions[pOrdinals[i]];

		if (mem.AddressInDataDirectory(rvaAddress, IMAGE_DIRECTORY_ENTRY_EXPORT)) {
			const auto forwardedNameRead = mem.ReadAligned<char>(rvaAddress);
			std::string forwardedName(forwardedNameRead.data(), std::min<size_t>(512, forwardedNameRead.size()));
			forwardedName.resize(strlen(forwardedName.data()));
			const auto dot = forwardedName.find('.');
			if (dot == std::string::npos)
				continue;  // invalid; format is DLLNAME.FUNCTIONAME
			forwardedName[dot] = '\0';

			const auto* moduleName = forwardedName.data();
			const auto* functionName = &forwardedName[dot + 1];

			const auto hForwardedToModule = AddressOf(moduleName, ModuleNameCompareMode::FileNameWithoutExtension);
			if (!hForwardedToModule)
				break;  // invalid; operating system should have loaded the module already
			if (hForwardedToModule == hModule)
				break;  // invalid; will result in infinite recursion
			return FindExportedFunction(hForwardedToModule, functionName, 0, require);
		} else {
			return reinterpret_cast<char*>(hModule) + rvaAddress;
		}
	}
	if (require)
		throw std::out_of_range("exported function not found");
	return nullptr;
}

HMODULE Utils::Win32::Process::LoadModule(const std::filesystem::path& path) const {
	auto pathBuf = path.wstring();
	pathBuf.resize(pathBuf.size() + 1, L'\0');  // ensure null terminated

	const auto rpszDllPath = WithVirtualAlloc(nullptr, MEM_COMMIT | MEM_RESERVE, PAGE_READONLY, std::span(pathBuf));
	CallRemoteFunction(FindKernel32Function("LoadLibraryW"), rpszDllPath, "LoadLibraryW");
	return AddressOf(path);
}

int Utils::Win32::Process::UnloadModule(HMODULE hModule) const {
	return CallRemoteFunction(FindKernel32Function("FreeLibrary"), hModule, "FreeLibrary");
}

namespace {
#if INTPTR_MAX == INT64_MAX
	// The 32-bit kernel32 has one base per boot; mapping its KnownDlls32 section here reveals it (as STATUS_IMAGE_MACHINE_TYPE_MISMATCH).
	void* FindKnownDlls32Kernel32Function(const char* pszFunctionName) {
		using NtOpenSectionFn = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
		using NtMapViewOfSectionFn = NTSTATUS(NTAPI*)(HANDLE, HANDLE, PVOID*, ULONG_PTR, SIZE_T, PLARGE_INTEGER, PSIZE_T, DWORD, ULONG, ULONG);
		using NtUnmapViewOfSectionFn = NTSTATUS(NTAPI*)(HANDLE, PVOID);
		constexpr NTSTATUS StatusImageNotAtBase = 0x40000003;
		constexpr DWORD ViewUnmap = 2;

		const auto hNtdll = GetModuleHandleW(L"ntdll.dll");
		const auto pNtOpenSection = reinterpret_cast<NtOpenSectionFn>(GetProcAddress(hNtdll, "NtOpenSection"));
		const auto pNtMapViewOfSection = reinterpret_cast<NtMapViewOfSectionFn>(GetProcAddress(hNtdll, "NtMapViewOfSection"));
		const auto pNtUnmapViewOfSection = reinterpret_cast<NtUnmapViewOfSectionFn>(GetProcAddress(hNtdll, "NtUnmapViewOfSection"));
		if (!pNtOpenSection || !pNtMapViewOfSection || !pNtUnmapViewOfSection)
			throw std::runtime_error("ntdll section functions not found");

		std::wstring name = L"\\KnownDlls32\\kernel32.dll";
		UNICODE_STRING us{
			.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t)),
			.MaximumLength = static_cast<USHORT>(name.size() * sizeof(wchar_t)),
			.Buffer = name.data(),
		};
		OBJECT_ATTRIBUTES oa{};
		InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

		HANDLE hSection{};
		if (const auto status = pNtOpenSection(&hSection, SECTION_MAP_READ | SECTION_QUERY, &oa); !NT_SUCCESS(status))
			throw std::runtime_error(std::format("NtOpenSection(\\KnownDlls32\\kernel32.dll) failed: 0x{:08X}", static_cast<uint32_t>(status)));
		const auto closeSection = Utils::Win32::Handle(hSection, true);

		void* base{};
		SIZE_T viewSize{};
		const auto status = pNtMapViewOfSection(hSection, GetCurrentProcess(), &base, 0, 0, nullptr, &viewSize, ViewUnmap, 0, PAGE_READONLY);
		if (!NT_SUCCESS(status) || !base)
			throw std::runtime_error(std::format("NtMapViewOfSection(\\KnownDlls32\\kernel32.dll) failed: 0x{:08X}", static_cast<uint32_t>(status)));
		const auto unmap = xivres::util::on_dtor([&] { pNtUnmapViewOfSection(GetCurrentProcess(), base); });
		if (status == StatusImageNotAtBase)
			throw std::runtime_error("\\KnownDlls32\\kernel32.dll was relocated");

		const auto view = std::span(static_cast<const uint8_t*>(base), viewSize);
		const auto at = [&view]<typename T>(size_t offset, std::type_identity<T>) -> const T& {
			if (offset > view.size() || view.size() - offset < sizeof(T))
				throw std::out_of_range("kernel32 (32-bit) image is truncated");
			return *reinterpret_cast<const T*>(&view[offset]);
		};

		const auto& dos = at(0, std::type_identity<IMAGE_DOS_HEADER>{});
		const auto& nt = at(dos.e_lfanew, std::type_identity<IMAGE_NT_HEADERS32>{});
		if (nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
			throw std::runtime_error("kernel32 (32-bit) is not a PE32 image");
		const auto& dir = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
		const auto& exports = at(dir.VirtualAddress, std::type_identity<IMAGE_EXPORT_DIRECTORY>{});
		for (DWORD i = 0; i < exports.NumberOfNames; ++i) {
			const auto nameRva = at(exports.AddressOfNames + i * sizeof(DWORD), std::type_identity<DWORD>{});
			const auto maxLength = view.size() > nameRva ? view.size() - nameRva : 0;
			if (strnlen(reinterpret_cast<const char*>(&view[nameRva]), maxLength) == maxLength
				|| strcmp(reinterpret_cast<const char*>(&view[nameRva]), pszFunctionName) != 0)
				continue;

			const auto ordinal = at(exports.AddressOfNameOrdinals + i * sizeof(WORD), std::type_identity<WORD>{});
			if (ordinal >= exports.NumberOfFunctions)
				break;
			const auto rva = at(exports.AddressOfFunctions + ordinal * sizeof(DWORD), std::type_identity<DWORD>{});
			if (dir.VirtualAddress <= rva && rva < dir.VirtualAddress + dir.Size)
				throw std::runtime_error(std::format("kernel32 (32-bit) forwards {}", pszFunctionName));
			return static_cast<char*>(base) + rva;
		}
		throw std::out_of_range(std::format("kernel32 (32-bit) does not export {}", pszFunctionName));
	}
#endif
}

void* Utils::Win32::Process::FindKernel32Function(const char* pszFunctionName) const {
	if (IsSameBitnessAsCurrent()) {
		// The system DLLs are mapped at the same base in every process of the same bitness.
		if (const auto pfn = ::GetProcAddress(GetModuleHandleW(L"kernel32.dll"), pszFunctionName))
			return reinterpret_cast<void*>(pfn);
		throw Error(std::format("GetProcAddress(kernel32, {})", pszFunctionName));
	}

#if INTPTR_MAX == INT64_MAX
	try {
		for (const auto hModule : EnumModules()) {
			if (_wcsicmp(PathOf(hModule).filename().c_str(), L"kernel32.dll") == 0)
				return FindExportedFunction(hModule, pszFunctionName);
		}
	} catch (const Error&) {
		// A process that has not run yet may not be enumerable at all.
	}
	return FindKnownDlls32Kernel32Function(pszFunctionName);
#else
	throw std::runtime_error("A 32-bit process cannot resolve functions of a 64-bit process.");
#endif
}

Utils::Win32::ModuleMemoryBlocks& Utils::Win32::Process::GetModuleMemoryBlockManager(HMODULE hModule) const {
	auto it = m_moduleMemory.find(hModule);
	if (it == m_moduleMemory.end()) {
		std::lock_guard lock(m_moduleMemoryMutex);

		it = m_moduleMemory.find(hModule);
		if (it == m_moduleMemory.end()) {
			it = m_moduleMemory.emplace(hModule, std::make_shared<ModuleMemoryBlocks>(*this, hModule)).first;
		}
	}
	return *it->second;
}

size_t Utils::Win32::Process::ReadMemory(void* lpBase, size_t offset, void* buf, size_t len, bool readFull) const {
	SIZE_T read = 0;
	if (ReadProcessMemory(m_object, static_cast<char*>(lpBase) + offset, buf, len, &read))
		return read;

	const auto err = GetLastError();
	if (err == ERROR_PARTIAL_COPY && !readFull)
		return read;

	throw Error(err, "Process::ReadMemory");
}

void Utils::Win32::Process::WriteMemory(void* lpTarget, const void* lpSource, size_t len, bool forceWrite) const {
	xivres::util::wrap_value_with_context<DWORD> protect;
	if (forceWrite)
		protect = WithVirtualProtect(lpTarget, 0, len, PAGE_EXECUTE_READWRITE);

	SIZE_T written;
	if (!WriteProcessMemory(m_object, lpTarget, lpSource, len, &written) || written != len)
		throw Error("Process::WriteMemory");

	// Without forceWrite the previous protection is unknown, and the target may still be code.
	if (!forceWrite || (protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
		FlushInstructionsCache(lpTarget, len);
}

void* Utils::Win32::Process::VirtualAlloc(void* lpBase, size_t size, DWORD flAllocType, DWORD flProtect, const void* lpSourceData, size_t sourceDataSize) const {
	auto flProtectInitial = flProtect;
	if (lpSourceData) {
		if (!sourceDataSize)
			lpSourceData = nullptr;
		else {
			if (sourceDataSize > size)
				throw std::invalid_argument("sourceDataSize cannot be greater than size");

			if (flProtect != PAGE_EXECUTE_READWRITE && flProtect != PAGE_READWRITE)
				flProtectInitial = PAGE_READWRITE;

			if (!(flAllocType & MEM_COMMIT))
				throw std::invalid_argument("lpSourceData cannot be set if MEM_COMMIT is not specified");
		}
	}

	void* lpAddress = VirtualAllocEx(m_object, lpBase, size, flAllocType, flProtectInitial);
	if (!lpAddress)
		throw Error("VirtualAllocEx");

	try {
		if (lpSourceData)
			WriteMemory(lpAddress, lpSourceData, sourceDataSize);

		if (flProtect != flProtectInitial)
			VirtualProtect(lpAddress, 0, size, flProtect);
	} catch (...) {
		VirtualFree(lpAddress, 0, MEM_RELEASE);
		throw;
	}

	return lpAddress;
}

void Utils::Win32::Process::VirtualFree(void* lpBase, size_t size, DWORD dwFreeType) const {
	if (!VirtualFreeEx(m_object, lpBase, size, dwFreeType))
		throw Error("VirtualFree");
}

DWORD Utils::Win32::Process::VirtualProtect(void* lpBase, size_t offset, size_t length, DWORD newProtect) const {
	DWORD old;
	if (!VirtualProtectEx(m_object, static_cast<char*>(lpBase) + offset, length, newProtect, &old))
		throw Error("VirtualProtectEx");
	return old;
}

xivres::util::wrap_value_with_context<DWORD> Utils::Win32::Process::WithVirtualProtect(void* lpBase, size_t offset, size_t length, DWORD newProtect) const {
	const auto previousProtect = VirtualProtect(lpBase, offset, length, newProtect);
	const auto lpAddress = static_cast<char*>(lpBase) + offset;
	return xivres::util::wrap_value_with_context(previousProtect, [this, lpAddress, length, newProtect, previousProtect]() {
		if (DWORD protectBeforeRestoration;
			!VirtualProtectEx(m_object, lpAddress, length, previousProtect, &protectBeforeRestoration)
			|| protectBeforeRestoration != newProtect) {
			DebugPrint(L"Problem restoring memory protect for PID {} at address 0x{:X}(length 0x{:X}): "
				L"original 0x{:X} => newProtect 0x{:X} => protectBeforeRestoration 0x{:X} => \"restored\" 0x{:X}",
				GetId(), reinterpret_cast<size_t>(lpAddress), length,
				previousProtect, newProtect, protectBeforeRestoration, previousProtect
			);
		}
	});
}

void Utils::Win32::Process::FlushInstructionsCache(void* lpBase, size_t size) const {
	if (!FlushInstructionCache(m_object, lpBase, size))
		throw Error("FlushInstructionsCache");
}

Utils::Win32::ModuleMemoryBlocks::ModuleMemoryBlocks(Process process, HMODULE hModule)
	: CurrentProcess(std::move(process))
	, CurrentModule(hModule)
	, DosHeader(CurrentProcess.ReadMemory<IMAGE_DOS_HEADER>(CurrentModule,
		0))
	, FileHeader(CurrentProcess.ReadMemory<IMAGE_FILE_HEADER>(CurrentModule,
		DosHeader.e_lfanew + offsetof(IMAGE_NT_HEADERS32, FileHeader)))
	, OptionalHeaderMagic(CurrentProcess.ReadMemory<WORD>(CurrentModule,
		DosHeader.e_lfanew + offsetof(IMAGE_NT_HEADERS32, OptionalHeader)))
	, SectionHeaders(CurrentProcess.ReadMemory<IMAGE_SECTION_HEADER>(CurrentModule,
		DosHeader.e_lfanew + offsetof(IMAGE_NT_HEADERS32, OptionalHeader) + FileHeader.SizeOfOptionalHeader
		, std::min<size_t>(64, FileHeader.NumberOfSections))) {
	if (const auto optionalHeaderLength = std::min<size_t>(FileHeader.SizeOfOptionalHeader,
		OptionalHeaderMagic == IMAGE_NT_OPTIONAL_HDR32_MAGIC
		? sizeof IMAGE_OPTIONAL_HEADER32
		: (OptionalHeaderMagic == IMAGE_NT_OPTIONAL_HDR64_MAGIC ? sizeof IMAGE_OPTIONAL_HEADER64 : 0)))
		CurrentProcess.ReadMemory<char>(CurrentModule,
			DosHeader.e_lfanew + offsetof(IMAGE_NT_HEADERS32, OptionalHeader),
			std::span(OptionalHeaderRaw, optionalHeaderLength)
		);
}

Utils::Win32::ModuleMemoryBlocks::~ModuleMemoryBlocks() = default;

bool Utils::Win32::ModuleMemoryBlocks::AddressInDataDirectory(size_t rva, int directoryIndex) {
	const auto& dir = OptionalHeaderMagic == IMAGE_NT_OPTIONAL_HDR32_MAGIC
		? OptionalHeader32.DataDirectory[directoryIndex]
		: OptionalHeader64.DataDirectory[directoryIndex];
	return dir.VirtualAddress <= rva && rva < dir.VirtualAddress + dir.Size;
}

std::span<uint8_t> Utils::Win32::ModuleMemoryBlocks::Read(size_t rva, size_t maxCount) {
	if (!rva)  // treat as empty
		return {};

	const auto& dirs = OptionalHeaderMagic == IMAGE_NT_OPTIONAL_HDR32_MAGIC
		? OptionalHeader32.DataDirectory
		: OptionalHeader64.DataDirectory;
	for (const auto& dir : dirs) {
		if (dir.VirtualAddress > rva || rva >= dir.VirtualAddress + dir.Size)
			continue;
		auto it = m_readMemoryBlocks.find(dir.VirtualAddress);
		if (it == m_readMemoryBlocks.end())
			it = m_readMemoryBlocks.emplace(dir.VirtualAddress, CurrentProcess.ReadMemory<char>(CurrentModule, dir.VirtualAddress, dir.Size)).first;
		return {
			reinterpret_cast<uint8_t*>(&it->second[rva - dir.VirtualAddress]),
			std::min<size_t>(maxCount, (dir.VirtualAddress + dir.Size - rva) / sizeof uint8_t),
		};
	}
	for (const auto& sectionHeader : SectionHeaders) {
		if (sectionHeader.VirtualAddress > rva || rva >= sectionHeader.VirtualAddress + sectionHeader.SizeOfRawData)
			continue;
		auto it = m_readMemoryBlocks.find(sectionHeader.VirtualAddress);
		if (it == m_readMemoryBlocks.end())
			it = m_readMemoryBlocks.emplace(sectionHeader.VirtualAddress, CurrentProcess.ReadMemory<char>(CurrentModule, sectionHeader.VirtualAddress, sectionHeader.SizeOfRawData)).first;
		return {
			reinterpret_cast<uint8_t*>(&it->second[rva - sectionHeader.VirtualAddress]),
			std::min<size_t>(maxCount, (sectionHeader.VirtualAddress + sectionHeader.SizeOfRawData - rva) / sizeof uint8_t),
		};
	}
	throw std::out_of_range("out of range address");
}
