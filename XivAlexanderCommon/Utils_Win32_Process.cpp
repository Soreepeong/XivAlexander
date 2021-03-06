#include "pch.h"
#include "Utils_Win32_Process.h"
#include "Utils_CallOnDestruction.h"

Utils::Win32::Process::Process() : Handle() {
}

Utils::Win32::Process::Process(std::nullptr_t)
	: Process() {
}

Utils::Win32::Process::Process(HANDLE hProcess, bool ownership)
	: Handle(hProcess, ownership) {
}

Utils::Win32::Process::Process(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwProcessId)
	: Handle(OpenProcess(dwDesiredAccess, bInheritHandle, dwProcessId)
		, Null
		, "OpenProcess({:x}, {}, {})", dwDesiredAccess, bInheritHandle ? "TRUE" : "FALSE", dwProcessId) {
}

Utils::Win32::Process::~Process() = default;

Utils::Win32::Process::Process(Process && r) noexcept
	: Handle(r.m_object, r.m_bOwnership)
	, m_moduleMemory(std::move(r.m_moduleMemory)) {
	r.Detach();
}

Utils::Win32::Process::Process(const Process & r)
	: Handle(r)
	, m_moduleMemory(r.m_moduleMemory) {
}

Utils::Win32::Process& Utils::Win32::Process::operator=(Process && r) noexcept {
	if (&r == this)
		return *this;

	m_object = r.m_object;
	m_bOwnership = r.m_bOwnership;
	m_moduleMemory = std::move(r.m_moduleMemory);
	r.Detach();
	return *this;
}

Utils::Win32::Process& Utils::Win32::Process::operator=(const Process & r) {
	if (&r == this)
		return *this;

	Handle::operator=(r);
	m_moduleMemory = r.m_moduleMemory;
	return *this;
}

Utils::Win32::Process& Utils::Win32::Process::operator=(std::nullptr_t) {
	Clear();
	return *this;
}

Utils::Win32::Process& Utils::Win32::Process::Current() {
	static Process current{ GetCurrentProcess(), false };
	return current;
}

Utils::Win32::Process& Utils::Win32::Process::Attach(HANDLE r, bool ownership, const std::string& errorMessage) {
	Handle::Attach(r, Null, ownership, errorMessage);
	return *this;
}

HANDLE Utils::Win32::Process::Detach() {
	m_moduleMemory.clear();
	return Handle::Detach();
}

void Utils::Win32::Process::Clear() {
	m_moduleMemory.clear();
	Handle::Clear();
}

HMODULE Utils::Win32::Process::AddressOf(std::filesystem::path path, ModuleNameCompareMode compareMode, bool require) const {
	std::vector<HMODULE> hModules;
	DWORD cbNeeded;
	do {
		hModules.resize(hModules.size() + std::min<size_t>(1024, std::max<size_t>(32768, hModules.size())));
		cbNeeded = static_cast<DWORD>(hModules.size());
		if (!EnumProcessModules(m_object, &hModules[0], static_cast<DWORD>(hModules.size() * sizeof(HMODULE)), &cbNeeded))
			throw Error(std::format("FindModuleAdderss(pid={}, path={})/EnumProcessModules", GetProcessId(m_object), ToUtf8(path)));
	} while (cbNeeded == hModules.size() * sizeof(HMODULE));
	hModules.resize(cbNeeded / sizeof(HMODULE));

	switch (compareMode) {
		case ModuleNameCompareMode::FullPath:
			path = canonical(path);
			break;
		case ModuleNameCompareMode::FileNameWithExtension:
			path = path.filename();
			break;
		case ModuleNameCompareMode::FileNameWithoutExtension:
			path = path.filename().replace_extension();
			break;
	}

	std::wstring sModName;
	sModName.resize(PATHCCH_MAX_CCH);
	for (const auto hModule : hModules) {
		auto remoteModulePath = PathOf(hModule);
		switch (compareMode) {
			case ModuleNameCompareMode::FullPath:
				remoteModulePath = canonical(remoteModulePath);
				break;
			case ModuleNameCompareMode::FileNameWithExtension:
				remoteModulePath = remoteModulePath.filename();
				break;
			case ModuleNameCompareMode::FileNameWithoutExtension:
				remoteModulePath = remoteModulePath.filename().replace_extension();
				break;
		}
		if (remoteModulePath == path)
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
		length = GetModuleFileNameW(hModule, &buf[0], static_cast<DWORD>(buf.size()));
	else if (!hModule) {
		length = static_cast<DWORD>(buf.size());
		if (!QueryFullProcessImageNameW(m_object, 0, &buf[0], &length))
			length = 0;
	} else
		length = GetModuleFileNameExW(m_object, hModule, &buf[0], static_cast<DWORD>(buf.size()));
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

DWORD Utils::Win32::Process::GetId() const {
	return GetProcessId(m_object);
}

void Utils::Win32::Process::Terminate(DWORD dwExitCode = 0, bool errorIfAlreadyTerminated) const {
	if (TerminateProcess(m_object, dwExitCode))
		return;
	const auto err = GetLastError();
	if (!errorIfAlreadyTerminated && Wait(0) != WAIT_TIMEOUT)
		return;
	throw Error(err, "TerminateProcess");
}

int Utils::Win32::Process::CallRemoteFunction(void* rpfn, void* rpParam, const char* pcszDescription) const {
	const auto hLoadLibraryThread = Handle(CreateRemoteThread(m_object, nullptr, 0, static_cast<LPTHREAD_START_ROUTINE>(rpfn), rpParam, 0, nullptr),
		Null,
		"Failed to call remote function {}@{:p}({:p})", pcszDescription, rpfn, rpParam);
	WaitForSingleObject(hLoadLibraryThread, INFINITE);
	DWORD exitCode;
	GetExitCodeThread(hLoadLibraryThread, &exitCode);
	return exitCode;
}

std::pair<void*, void*> Utils::Win32::Process::FindImportedFunction(HMODULE hModule, const std::filesystem::path & dllName, const char* pszFunctionName, uint32_t hintOrOrdinal) const {
	const auto fileName = ToUtf8(dllName.filename());

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
			continue; // invalid; either one of name or ordinal shoould exist
		else if (ordinal != pOrdinals[i])
			continue; // exported by ordinal; ordinal mismatch

		BoundaryCheck(pOrdinals[i], 0, pExportTable->NumberOfFunctions);
		const auto rvaAddress = pFunctions[pOrdinals[i]];

		if (mem.AddressInDataDirectory(rvaAddress, IMAGE_DIRECTORY_ENTRY_EXPORT)) {
			// Forwarder RVA
			const auto forwardedNameRead = mem.ReadAligned<char>(rvaAddress);
			std::string forwardedName(forwardedNameRead.data(), std::min<size_t>(512, forwardedNameRead.size()));
			forwardedName.resize(strlen(forwardedName.data()));
			const auto dot = forwardedName.find('.');
			if (dot == std::string::npos)
				continue; // invalid; format is DLLNAME.FUNCTIONAME
			forwardedName[dot] = '\0';

			const auto* moduleName = &forwardedName[0];
			const auto* functionName = &forwardedName[dot + 1];

			const auto hForwardedToModule = AddressOf(moduleName, ModuleNameCompareMode::FileNameWithoutExtension);
			if (!hForwardedToModule)
				break;  // invalid; operating system should have loaded the module already
			if (hForwardedToModule == hModule)
				break;  // invalid; will result in infinite recursion
			return FindExportedFunction(hForwardedToModule, functionName, 0, require);

		} else {
			// Export RVA
			return reinterpret_cast<char*>(hModule) + rvaAddress;
		}
	}
	if (require)
		throw std::out_of_range("exported function not found");
	return nullptr;
}

HMODULE Utils::Win32::Process::LoadModule(const std::filesystem::path & path) const {
	auto pathBuf = path.wstring();
	pathBuf.resize(pathBuf.size() + 1, L'\0');  // ensure null terminated
	
	const auto rpszDllPath = WithVirtualAlloc(nullptr, MEM_COMMIT | MEM_RESERVE, PAGE_READONLY, std::span(pathBuf));
	CallRemoteFunction(LoadLibraryW, rpszDllPath, "LoadLibraryW");
	OutputDebugStringW(L"LoadLibraryW\n");
	return AddressOf(path);
}

int Utils::Win32::Process::UnloadModule(HMODULE hModule) const {
	return CallRemoteFunction(FreeLibrary, hModule, "FreeLibrary");
}

std::vector<MEMORY_BASIC_INFORMATION> Utils::Win32::Process::GetCommittedImageAllocation(const std::filesystem::path & path) const {
	const auto ntpath = ToNativePath(path);
	const auto ntpathw = ntpath.wstring();

	std::vector<MEMORY_BASIC_INFORMATION> regions;
	for (MEMORY_BASIC_INFORMATION mbi{};
		VirtualQueryEx(m_object, mbi.BaseAddress, &mbi, sizeof mbi);
		mbi.BaseAddress = static_cast<char*>(mbi.BaseAddress) + mbi.RegionSize) {
		if (!(mbi.State & MEM_COMMIT) || mbi.Type != MEM_IMAGE)
			continue;
		if (GetMappedImageNativePath(m_object, mbi.BaseAddress).wstring() == ntpathw)
			regions.emplace_back(mbi);
	}
	return regions;
}

std::vector<MEMORY_BASIC_INFORMATION> Utils::Win32::Process::GetCommittedImageAllocation() const {
	return GetCommittedImageAllocation(PathOf());
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
	SIZE_T read;
	if (ReadProcessMemory(m_object, static_cast<char*>(lpBase) + offset, buf, len, &read))
		return read;

	const auto err = GetLastError();
	if (err == ERROR_PARTIAL_COPY && !readFull)
		return read;

	throw Error(err, "Process::ReadMemory");
}

void Utils::Win32::Process::WriteMemory(void* lpTarget, const void* lpSource, size_t len, bool forceWrite) const {
	CallOnDestructionWithValue<DWORD> protect;
	if (forceWrite)
		protect = WithVirtualProtect(lpTarget, 0, len, PAGE_EXECUTE_READWRITE);
	
	SIZE_T written;
	if (!WriteProcessMemory(m_object, lpTarget, lpSource, len, &written) || written != len)
		throw Error("Process::WriteMemory");

	if ((protect & PAGE_EXECUTE)
		|| (protect & PAGE_EXECUTE_READ)
		|| (protect & PAGE_EXECUTE_READWRITE)
		|| (protect & PAGE_EXECUTE_WRITECOPY))
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

Utils::CallOnDestructionWithValue<DWORD> Utils::Win32::Process::WithVirtualProtect(void* lpBase, size_t offset, size_t length, DWORD newProtect) const {
	const auto previousProtect = VirtualProtect(lpBase, offset, length, newProtect);
	const auto lpAddress = static_cast<char*>(lpBase) + offset;
	return CallOnDestructionWithValue(previousProtect, [this, lpAddress, length, newProtect, previousProtect]() {
		if (DWORD protectBeforeRestoration;
			!VirtualProtectEx(m_object, lpAddress, length, previousProtect, &protectBeforeRestoration)
			|| protectBeforeRestoration != newProtect) {
			DebugPrint(L"Problem restoring memory protect for PID {} at address 0x{:x}(length 0x{:x}): "
				L"original 0x{:x} => newProtect 0x{:x} => protectBeforeRestoration 0x{:x} => \"restored\" 0x{:x}",
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
