#include "pch.h"
#include "Utils/Win32/RelocatedImageCopy.h"

#include "Utils/Win32/Handle.h"
#include "Utils/Win32/Process.h"

Utils::Win32::RelocatedImageCopy::RelocatedImageCopy(const LoadedModule& original)
	: m_original(*original) {
	const auto& currentProcess = Process::Current();
	const auto f = Handle::FromCreateFile(original.PathOf(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
	const auto optionalHeaderFromFile = f.Read<IMAGE_NT_HEADERS>(f.Read<IMAGE_DOS_HEADER>(0).e_lfanew).OptionalHeader;
	const auto allocation = currentProcess.VirtualAlloc(nullptr, optionalHeaderFromFile.SizeOfImage, MEM_RESERVE, PAGE_NOACCESS);
	m_memRelease = [&currentProcess, allocation] { currentProcess.VirtualFree(allocation, 0, MEM_RELEASE); };

	m_mem = {reinterpret_cast<char*>(allocation), optionalHeaderFromFile.SizeOfImage};

	currentProcess.VirtualAlloc(m_mem.data(), optionalHeaderFromFile.SizeOfHeaders, MEM_COMMIT, PAGE_READWRITE);
	f.Read(0, m_mem.subspan(0, optionalHeaderFromFile.SizeOfHeaders));

	const auto& dos = *reinterpret_cast<const IMAGE_DOS_HEADER*>(m_mem.data());
	const auto& nt = *reinterpret_cast<const IMAGE_NT_HEADERS*>(&m_mem[dos.e_lfanew]);
	const auto sectionHeaders = std::span(IMAGE_FIRST_SECTION(&nt), nt.FileHeader.NumberOfSections);
	for (const auto& sh : sectionHeaders) {
		currentProcess.VirtualAlloc(&m_mem[sh.VirtualAddress], sh.Misc.VirtualSize, MEM_COMMIT, PAGE_READWRITE);
		f.Read(sh.PointerToRawData, m_mem.subspan(sh.VirtualAddress, (std::min)(sh.Misc.VirtualSize, sh.SizeOfRawData)));
	}

	if (const auto displacement = reinterpret_cast<size_t>(m_mem.data()) - nt.OptionalHeader.ImageBase) {
		const auto relocDir = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
		*const_cast<size_t*>(static_cast<const size_t*>(static_cast<const void*>(&nt.OptionalHeader.ImageBase))) += displacement;
		for (size_t i = relocDir.VirtualAddress, i_ = i + relocDir.Size; i < i_; ) {
			const auto& page = *reinterpret_cast<IMAGE_BASE_RELOCATION*>(&m_mem[i]);
			i += sizeof page;

			for (size_t j = sizeof page; j < page.SizeOfBlock && i < i_; j += 2, i += 2) {
				const auto entry = *reinterpret_cast<uint16_t*>(&m_mem[i]);
				const auto addr = static_cast<void*>(&m_mem[static_cast<size_t>(page.VirtualAddress) + (entry & 0xFFF)]);
				switch (entry >> 12) {
					case IMAGE_REL_BASED_ABSOLUTE:
						break;
					case IMAGE_REL_BASED_HIGHLOW:
						*static_cast<uint32_t*>(addr) += static_cast<uint32_t>(displacement);
						break;
					case IMAGE_REL_BASED_DIR64:
						*static_cast<uint64_t*>(addr) += static_cast<uint64_t>(displacement);
						break;
					default:
						// Should not happen (probably an error), but _hopefully_ irrelevant for reading code
						break;
				}
			}
		}
	}

	currentProcess.VirtualProtect(m_mem.data(), 0, optionalHeaderFromFile.SizeOfHeaders, PAGE_READONLY);
	for (const auto& sh : sectionHeaders) {
		const auto r = sh.Characteristics & IMAGE_SCN_MEM_READ;
		const auto w = sh.Characteristics & IMAGE_SCN_MEM_WRITE;
		const auto x = sh.Characteristics & IMAGE_SCN_MEM_EXECUTE;

		DWORD pageAccess = PAGE_NOACCESS;
		if (w && x)
			pageAccess = PAGE_EXECUTE_READWRITE;
		else if (w && !x)
			pageAccess = PAGE_READWRITE;
		else if (r && !w && x)
			pageAccess = PAGE_EXECUTE_READ;
		else if (r && !w && !x)
			pageAccess = PAGE_READONLY;
		else if (!r && !w && x)
			pageAccess = PAGE_EXECUTE;
		currentProcess.VirtualProtect(m_mem.data(), sh.VirtualAddress, sh.Misc.VirtualSize, pageAccess);
	}

	m_copy = LoadedModule(reinterpret_cast<HMODULE>(m_mem.data()), false);
}

Utils::Win32::RelocatedImageCopy::~RelocatedImageCopy() = default;
