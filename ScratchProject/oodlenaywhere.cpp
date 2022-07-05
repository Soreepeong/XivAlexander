#define _CRT_SECURE_NO_WARNINGS

#include <fstream>
#include <iostream>
#include <span>
#include <type_traits>
#include <vector>

#define IMAGE_NUMBEROF_DIRECTORY_ENTRIES 16
#define IMAGE_DIRECTORY_ENTRY_BASERELOC 5
#define IMAGE_SIZEOF_SHORT_NAME 8

struct IMAGE_DOS_HEADER {
	uint16_t e_magic;
	uint16_t e_cblp;
	uint16_t e_cp;
	uint16_t e_crlc;
	uint16_t e_cparhdr;
	uint16_t e_minalloc;
	uint16_t e_maxalloc;
	uint16_t e_ss;
	uint16_t e_sp;
	uint16_t e_csum;
	uint16_t e_ip;
	uint16_t e_cs;
	uint16_t e_lfarlc;
	uint16_t e_ovno;
	uint16_t e_res[4];
	uint16_t e_oemid;
	uint16_t e_oeminfo;
	uint16_t e_res2[10];
	uint32_t e_lfanew;
};

struct IMAGE_FILE_HEADER {
	uint16_t Machine;
	uint16_t NumberOfSections;
	uint32_t TimeDateStamp;
	uint32_t PointerToSymbolTable;
	uint32_t NumberOfSymbols;
	uint16_t SizeOfOptionalHeader;
	uint16_t Characteristics;
};

struct IMAGE_DATA_DIRECTORY {
	uint32_t VirtualAddress;
	uint32_t Size;
};

struct IMAGE_OPTIONAL_HEADER32 {
	uint16_t Magic;
	uint8_t MajorLinkerVersion;
	uint8_t MinorLinkerVersion;
	uint32_t SizeOfCode;
	uint32_t SizeOfInitializedData;
	uint32_t SizeOfUninitializedData;
	uint32_t AddressOfEntryPoint;
	uint32_t BaseOfCode;
	uint32_t BaseOfData;
	uint32_t ImageBase;
	uint32_t SectionAlignment;
	uint32_t FileAlignment;
	uint16_t MajorOperatingSystemVersion;
	uint16_t MinorOperatingSystemVersion;
	uint16_t MajorImageVersion;
	uint16_t MinorImageVersion;
	uint16_t MajorSubsystemVersion;
	uint16_t MinorSubsystemVersion;
	uint32_t Win32VersionValue;
	uint32_t SizeOfImage;
	uint32_t SizeOfHeaders;
	uint32_t CheckSum;
	uint16_t Subsystem;
	uint16_t DllCharacteristics;
	uint32_t SizeOfStackReserve;
	uint32_t SizeOfStackCommit;
	uint32_t SizeOfHeapReserve;
	uint32_t SizeOfHeapCommit;
	uint32_t LoaderFlags;
	uint32_t NumberOfRvaAndSizes;
	IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
};

struct IMAGE_OPTIONAL_HEADER64 {
	uint16_t Magic;
	uint8_t MajorLinkerVersion;
	uint8_t MinorLinkerVersion;
	uint32_t SizeOfCode;
	uint32_t SizeOfInitializedData;
	uint32_t SizeOfUninitializedData;
	uint32_t AddressOfEntryPoint;
	uint32_t BaseOfCode;
	uint64_t ImageBase;
	uint32_t SectionAlignment;
	uint32_t FileAlignment;
	uint16_t MajorOperatingSystemVersion;
	uint16_t MinorOperatingSystemVersion;
	uint16_t MajorImageVersion;
	uint16_t MinorImageVersion;
	uint16_t MajorSubsystemVersion;
	uint16_t MinorSubsystemVersion;
	uint32_t Win32VersionValue;
	uint32_t SizeOfImage;
	uint32_t SizeOfHeaders;
	uint32_t CheckSum;
	uint16_t Subsystem;
	uint16_t DllCharacteristics;
	uint64_t SizeOfStackReserve;
	uint64_t SizeOfStackCommit;
	uint64_t SizeOfHeapReserve;
	uint64_t SizeOfHeapCommit;
	uint32_t LoaderFlags;
	uint32_t NumberOfRvaAndSizes;
	IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
};

template<typename TOptionalHeader>
struct IMAGE_NT_HEADERS_SIZED {
	uint32_t Signature;
	IMAGE_FILE_HEADER FileHeader;
	TOptionalHeader OptionalHeader;
};

struct IMAGE_SECTION_HEADER {
	char Name[IMAGE_SIZEOF_SHORT_NAME];
	union {
		uint32_t PhysicalAddress;
		uint32_t VirtualSize;
	} Misc;
	uint32_t VirtualAddress;
	uint32_t SizeOfRawData;
	uint32_t PointerToRawData;
	uint32_t PointerToRelocations;
	uint32_t PointerToLinenumbers;
	uint16_t NumberOfRelocations;
	uint16_t NumberOfLinenumbers;
	uint32_t Characteristics;
};

struct IMAGE_BASE_RELOCATION {
	uint32_t VirtualAddress;
	uint32_t SizeOfBlock;
};

#define FIELD_OFFSET(type, field)    ((int32_t)(int64_t)&(((type *)0)->field))
#define IMAGE_FIRST_SECTION( ntheader ) ((IMAGE_SECTION_HEADER*)        \
    ((const char*)(ntheader) +                                            \
     FIELD_OFFSET( IMAGE_NT_HEADERS, OptionalHeader ) +                 \
     ((ntheader))->FileHeader.SizeOfOptionalHeader   \
    ))

#if defined(_WIN64)
#define STDCALL __stdcall
const auto GamePath = LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\ffxiv_dx11.exe)";

extern "C" void* __stdcall VirtualAlloc(void* lpAddress, size_t dwSize, uint32_t flAllocationType, uint32_t flProtect);
void* executable_allocate(size_t size) {
	return VirtualAlloc(nullptr, size, 0x3000 /* MEM_COMMIT | MEM_RESERVE */, 0x40 /* PAGE_EXECUTE_READWRITE */);
}

using IMAGE_NT_HEADERS = IMAGE_NT_HEADERS_SIZED<IMAGE_OPTIONAL_HEADER64>;

#elif defined(_WIN32)
#define STDCALL __stdcall
const auto GamePath = LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\ffxiv.exe)";

extern "C" void* __stdcall VirtualAlloc(void* lpAddress, size_t dwSize, uint32_t flAllocationType, uint32_t flProtect);
void* executable_allocate(size_t size) {
	return VirtualAlloc(nullptr, size, 0x3000 /* MEM_COMMIT | MEM_RESERVE */, 0x40 /* PAGE_EXECUTE_READWRITE */);
}

using IMAGE_NT_HEADERS = IMAGE_NT_HEADERS_SIZED<IMAGE_OPTIONAL_HEADER32>;

#elif defined(__linux__)
#define STDCALL __attribute__((stdcall))
const auto GamePath = R"(ffxiv.exe)";

#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <unistd.h>
#include <sys/mman.h>
void* executable_allocate(size_t size) {
	const auto p = memalign(sysconf(_SC_PAGE_SIZE), size);
	mprotect(p, size, PROT_READ | PROT_WRITE | PROT_EXEC);
	return p;
}

using IMAGE_NT_HEADERS = IMAGE_NT_HEADERS_SIZED<IMAGE_OPTIONAL_HEADER32>;

#endif

using OodleNetwork1_Shared_Size = std::remove_pointer_t<int(STDCALL*)(int htbits)>;
using OodleNetwork1_Shared_SetWindow = std::remove_pointer_t<void(STDCALL*)(void* data, int htbits, void* window, int windowSize)>;
using OodleNetwork1UDP_Train = std::remove_pointer_t<void(STDCALL*)(void* state, void* shared, const void* const* trainingPacketPointers, const int* trainingPacketSizes, int trainingPacketCount)>;
using OodleNetwork1UDP_Decode = std::remove_pointer_t<bool(STDCALL*)(void* state, void* shared, const void* compressed, size_t compressedSize, void* raw, size_t rawSize)>;
using OodleNetwork1UDP_Encode = std::remove_pointer_t<int(STDCALL*)(const void* state, const void* shared, const void* raw, size_t rawSize, void* compressed)>;
using OodleNetwork1UDP_State_Size = std::remove_pointer_t<int(STDCALL*)(void)>;
using Oodle_Malloc = std::remove_pointer_t<void*(STDCALL*)(size_t size, int align)>;
using Oodle_Free = std::remove_pointer_t<void(STDCALL*)(void* p)>;
using Oodle_SetMallocFree = std::remove_pointer_t<void(STDCALL*)(Oodle_Malloc* pfnMalloc, Oodle_Free* pfnFree)>;

void* STDCALL my_malloc(size_t size, int align) {
	const auto pRaw = (char*)malloc(size + align + sizeof(void*) - 1);
	if (!pRaw)
		return nullptr;

	const auto pAligned = (void*)(((size_t)pRaw + align + 7) & (size_t)-align);
	*((void**)pAligned - 1) = pRaw;
	return pAligned;
}

void STDCALL my_free(void* p) {
	free(*((void**)p - 1));
}

const char* lookup_in_text(const char* pBaseAddress, const char* sPattern, const char* sMask, size_t length) {
	std::vector<void*> result;
	const std::string_view mask(sMask, length);
	const std::string_view pattern(sPattern, length);

	const auto& dosh = *(IMAGE_DOS_HEADER*)(&pBaseAddress[0]);
	const auto& nth = *(IMAGE_NT_HEADERS*)(&pBaseAddress[dosh.e_lfanew]);

	const auto pSectionHeaders = IMAGE_FIRST_SECTION(&nth);
	for (size_t i = 0; i < nth.FileHeader.NumberOfSections; ++i) {
		if (strncmp(pSectionHeaders[i].Name, ".text", 8) == 0) {
			std::string_view section(pBaseAddress + pSectionHeaders[i].VirtualAddress, pSectionHeaders[i].Misc.VirtualSize);
			const auto nUpperLimit = section.length() - pattern.length();
			for (size_t i = 0; i < nUpperLimit; ++i) {
				for (size_t j = 0; j < pattern.length(); ++j) {
					if ((section[i + j] & mask[j]) != (pattern[j] & mask[j]))
						goto next_char;
				}
				return section.data() + i;
			next_char:;
			}
		}
	}
	std::cerr << "Could not find signature" << std::endl;
	exit(-1);
	return nullptr;
}

int main() {
	std::cerr << std::hex;
	freopen(NULL, "rb", stdin);
	freopen(NULL, "wb", stdout);

	std::ifstream game(GamePath, std::ios::binary);
	game.seekg(0, std::ios::end);
	std::vector<char> buf((size_t)game.tellg());
	game.seekg(0, std::ios::beg);
	game.read(&buf[0], buf.size());

	const auto& dosh = *(IMAGE_DOS_HEADER*)(&buf[0]);
	const auto& nth = *(IMAGE_NT_HEADERS*)(&buf[dosh.e_lfanew]);

	std::span<char> virt((char*)executable_allocate(nth.OptionalHeader.SizeOfImage), nth.OptionalHeader.SizeOfImage);
	std::cerr << std::hex << "Base: 0x" << (size_t)&virt[0] << std::endl;

	const auto ddoff = dosh.e_lfanew + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) + nth.FileHeader.SizeOfOptionalHeader;
	memcpy(&virt[0], &buf[0], ddoff + sizeof(IMAGE_SECTION_HEADER) * nth.FileHeader.NumberOfSections);
	for (const auto& s : std::span((IMAGE_SECTION_HEADER*)&buf[ddoff], nth.FileHeader.NumberOfSections)) {
		const auto src = std::span(&buf[s.PointerToRawData], s.SizeOfRawData);
		const auto dst = std::span(&virt[s.VirtualAddress], s.Misc.VirtualSize);
		memcpy(&dst[0], &src[0], std::min(src.size(), dst.size()));
	}

	const auto base = nth.OptionalHeader.ImageBase;
	for (size_t i = nth.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress,
		i_ = i + nth.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;
		i < i_; ) {
		const auto& page = *(IMAGE_BASE_RELOCATION*)&virt[i];
		for (const auto relo : std::span((uint16_t*)(&page + 1), (page.SizeOfBlock - sizeof page) / 2)) {
			if ((relo >> 12) == 0)
				void();
			else if ((relo >> 12) == 3)
				*(uint32_t*)&virt[(size_t)page.VirtualAddress + (relo & 0xFFF)] += (uint32_t)((size_t)&virt[0] - base);
			else if ((relo >> 12) == 10)
				*(uint64_t*)&virt[(size_t)page.VirtualAddress + (relo & 0xFFF)] += (uint64_t)((size_t)&virt[0] - base);
			else
				std::abort();
		}

		i += page.SizeOfBlock;
	}

	const auto cpfnOodleSetMallocFree = lookup_in_text(
		&virt[0],
		"\x75\x16\x68\x00\x00\x00\x00\x68\x00\x00\x00\x00\xe8",
		"\xff\xff\xff\x00\x00\x00\x00\xff\x00\x00\x00\x00\xe8",
		13) + 12;
	const auto pfnOodleSetMallocFree = (Oodle_SetMallocFree*)(cpfnOodleSetMallocFree + 5 + *(int*)(cpfnOodleSetMallocFree + 1));

	std::vector<const char*> calls;
	for (auto sig1 = lookup_in_text(
		&virt[0],
		"\x83\x7e\x00\x00\x75\x00\x6a\x13\xe8\x00\x00\x00\x00\x6a\x00\x6a\x00\x50\xe8",
		"\xff\xff\x00\x00\xff\x00\xff\xff\xff\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff",
		19), sig2 = sig1 + 1024; calls.size() < 6 && sig1 < sig2; sig1++) {
		if (*sig1 != (char)0xe8)
			continue;
		const auto pTargetAddress = sig1 + 5 + *(int*)(sig1 + 1);
		if (pTargetAddress < virt.data() || pTargetAddress >= virt.data() + virt.size())
			continue;
		calls.push_back(pTargetAddress);
	}
	if (calls.size() < 6) {
		std::cerr << "Could not find signature" << std::endl;
		return -1;
	}
	const auto pfnOodleNetwork1_Shared_Size = (OodleNetwork1_Shared_Size*)calls[0];
	const auto pfnOodleNetwork1_Shared_SetWindow = (OodleNetwork1_Shared_SetWindow*)calls[2];
	const auto pfnOodleNetwork1UDP_State_Size= (OodleNetwork1UDP_State_Size*)calls[3];
	const auto pfnOodleNetwork1UDP_Train = (OodleNetwork1UDP_Train*)calls[5];

	const auto pfnOodleNetwork1UDP_Decode = (OodleNetwork1UDP_Decode*)lookup_in_text(
		&virt[0],
		"\x8b\x44\x24\x18\x56\x85\xc0\x7e\x00\x8b\x74\x24\x14\x85\xf6\x7e\x00\x3b\xf0",
		"\xff\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff",
		19);

	const auto cpfnOodleNetwork1UDP_Encode = lookup_in_text(
		&virt[0],
		"\x57\xff\x15\x00\x00\x00\x00\xff\x75\x08\x56\xff\x75\x10\xff\x77\x1c\xff\x77\x18\xe8",
		"\xff\xff\xff\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff",
		21) + 20;
	const auto pfnOodleNetwork1UDP_Encode = (OodleNetwork1UDP_Encode*)(cpfnOodleNetwork1UDP_Encode + 5 + *(int*)(cpfnOodleNetwork1UDP_Encode + 1));

	int htbits = 19;
	std::vector<uint8_t> state(pfnOodleNetwork1UDP_State_Size());
	std::vector<uint8_t> shared(pfnOodleNetwork1_Shared_Size(htbits));
	std::vector<uint8_t> window(0x8000);

	pfnOodleSetMallocFree(&my_malloc, &my_free);
	pfnOodleNetwork1_Shared_SetWindow(&shared[0], htbits, &window[0], static_cast<int>(window.size()));
	pfnOodleNetwork1UDP_Train(&state[0], &shared[0], nullptr, nullptr, 0);

	std::vector<uint8_t> src, dst;
	src.resize(256);
	for (int i = 0; i < 256; i++)
		src[i] = i;
	dst.resize(src.size());
	dst.resize(pfnOodleNetwork1UDP_Encode(&state[0], &shared[0], &src[0], src.size(), &dst[0]));
	if (!pfnOodleNetwork1UDP_Decode(&state[0], &shared[0], &dst[0], dst.size(), &src[0], src.size())) {
		std::cerr << "Oodle encode/decode test failure" << std::endl;
		return -1;
	} else {
		std::cerr << "Oodle encode test: 256 -> " << dst.size() << std::endl;
	}
	for (int i = 0; i < 256; i++) {
		if (src[i] != i) {
			std::cerr << "Oodle encode/decode test failure" << std::endl;
			break;
		}
	}

	std::cerr << "Oodle helper running: state=" << state.size() << " shared=" << shared.size() << " window=" << window.size() << std::endl;
	while (true) {
		struct my_header_t {
			uint32_t SourceLength;
			uint32_t TargetLength;
		} hdr{};
		fread(&hdr, sizeof(hdr), 1, stdin);
		if (!hdr.SourceLength)
			return 0;

		// std::cerr << "Request: src=0x" << hdr.SourceLength << " dst=0x" << hdr.TargetLength << std::endl;
		src.resize(hdr.SourceLength);
		fread(&src[0], 1, src.size(), stdin);

		if (hdr.TargetLength == 0xFFFFFFFFU) {
			dst.resize(src.size());
			dst.resize(pfnOodleNetwork1UDP_Encode(&state[0], &shared[0], &src[0], src.size(), &dst[0]));
			// std::cerr << "Encoded: res=0x" << dst.size() << std::endl;
		} else {
			dst.resize(hdr.TargetLength);
			if (!pfnOodleNetwork1UDP_Decode(&state[0], &shared[0], &src[0], src.size(), &dst[0], dst.size())) {
				dst.resize(0);
				dst.resize(hdr.TargetLength);
			}
		}
		uint32_t size = (uint32_t)dst.size();
		fwrite(&size, sizeof(size), 1, stdout);
		fwrite(&dst[0], 1, dst.size(), stdout);
		fflush(stdout);
	}
}