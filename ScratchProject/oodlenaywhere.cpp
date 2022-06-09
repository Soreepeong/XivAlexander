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
	uint8_t Name[IMAGE_SIZEOF_SHORT_NAME];
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

#if defined(_WIN64)
#define STDCALL __stdcall
const auto GamePath = LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\ffxiv_dx11.exe)";
constexpr size_t off_OodleMalloc = 0x1f21cf8;
constexpr size_t off_OodleFree = 0x1f21d00;
constexpr size_t off_OodleNetwork1_Shared_Size = 0x153edf0;
constexpr size_t off_OodleNetwork1_Shared_SetWindow = 0x153ecc0;
constexpr size_t off_OodleNetwork1UDP_Train = 0x153d920;
constexpr size_t off_OodleNetwork1UDP_Decode = 0x153cdd0;
constexpr size_t off_OodleNetwork1UDP_Encode = 0x153ce20;
constexpr size_t off_OodleNetwork1UDP_State_Size = 0x153d470;

extern "C" void* __stdcall VirtualAlloc(void* lpAddress, size_t dwSize, uint32_t flAllocationType, uint32_t flProtect);
void* executable_allocate(size_t size) {
	return VirtualAlloc(nullptr, size, 0x3000 /* MEM_COMMIT | MEM_RESERVE */, 0x40 /* PAGE_EXECUTE_READWRITE */);
}

using IMAGE_NT_HEADERS = IMAGE_NT_HEADERS_SIZED<IMAGE_OPTIONAL_HEADER64>;

#elif defined(_WIN32)
#define STDCALL __stdcall
const auto GamePath = LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\ffxiv.exe)";
constexpr size_t off_OodleMalloc = 0x1576dc4;
constexpr size_t off_OodleFree = 0x1576dc8;
constexpr size_t off_OodleNetwork1_Shared_Size = 0x1171cc0;
constexpr size_t off_OodleNetwork1_Shared_SetWindow = 0x1171ba0;
constexpr size_t off_OodleNetwork1UDP_Train = 0x1170bb0;
constexpr size_t off_OodleNetwork1UDP_Decode = 0x11701a0;
constexpr size_t off_OodleNetwork1UDP_Encode = 0x11701e0;
constexpr size_t off_OodleNetwork1UDP_State_Size = 0x1170880;

extern "C" void* __stdcall VirtualAlloc(void* lpAddress, size_t dwSize, uint32_t flAllocationType, uint32_t flProtect);
void* executable_allocate(size_t size) {
	return VirtualAlloc(nullptr, size, 0x3000 /* MEM_COMMIT | MEM_RESERVE */, 0x40 /* PAGE_EXECUTE_READWRITE */);
}

using IMAGE_NT_HEADERS = IMAGE_NT_HEADERS_SIZED<IMAGE_OPTIONAL_HEADER32>;

#elif defined(__linux__)
#define STDCALL __attribute__((stdcall))
const auto GamePath = R"(ffxiv.exe)";
constexpr size_t off_OodleMalloc = 0x1576dc4;
constexpr size_t off_OodleFree = 0x1576dc8;
constexpr size_t off_OodleNetwork1_Shared_Size = 0x1171cc0;
constexpr size_t off_OodleNetwork1_Shared_SetWindow = 0x1171ba0;
constexpr size_t off_OodleNetwork1UDP_Train = 0x1170bb0;
constexpr size_t off_OodleNetwork1UDP_Decode = 0x11701a0;
constexpr size_t off_OodleNetwork1UDP_Encode = 0x11701e0;
constexpr size_t off_OodleNetwork1UDP_State_Size = 0x1170880;

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

	int htbits = 19;
	*(void**)&virt[off_OodleMalloc] = (void*)&my_malloc;
	*(void**)&virt[off_OodleFree] = (void*)&my_free;
	std::vector<uint8_t> state(((OodleNetwork1UDP_State_Size*)&virt[off_OodleNetwork1UDP_State_Size])());
	std::vector<uint8_t> shared(((OodleNetwork1_Shared_Size*)&virt[off_OodleNetwork1_Shared_Size])(htbits));
	std::vector<uint8_t> window(0x8000);

	((OodleNetwork1_Shared_SetWindow*)&virt[off_OodleNetwork1_Shared_SetWindow])(&shared[0], htbits, &window[0], static_cast<int>(window.size()));
	((OodleNetwork1UDP_Train*)&virt[off_OodleNetwork1UDP_Train])(&state[0], &shared[0], nullptr, nullptr, 0);

	/*
	std::vector<uint8_t> src, dst;
	while (true) {
		struct my_header_t {
			uint32_t SourceLength;
			uint32_t TargetLength;
		} hdr;
		fread(&hdr, sizeof(hdr), 1, stdin);
		src.resize(hdr.SourceLength);
		fread(&src, 1, src.size(), stdin);
		
		if (hdr.TargetLength == 0xFFFFFFFFU) {
			dst.resize(src.size());
			dst.resize(((OodleNetwork1UDP_Encode*)&virt[off_OodleNetwork1UDP_Encode])(state.data(), shared.data(), src.data(), src.size(), dst.data()));
		} else {
			dst.resize(hdr.TargetLength);
			((OodleNetwork1UDP_Decode*)&virt[off_OodleNetwork1UDP_Decode])(&state[0], &shared[0], &src[0], src.size(), &dst[0], dst.size());
		}
		uint32_t size = (uint32_t)dst.size();
		fwrite(&size, sizeof(size), 1, stdout);
		fwrite(&dst[0], 1, dst.size(), stdout);
		fflush(stdout);
	}
	/*/
	std::vector<uint8_t> src(16000), enc, dec;
	for (size_t i = 0;; i++) {
		for (auto& c : src)
			c = rand();

		enc.clear();
		enc.resize(src.size());
		enc.resize(((OodleNetwork1UDP_Encode*)&virt[off_OodleNetwork1UDP_Encode])(state.data(), shared.data(), src.data(), src.size(), enc.data()));

		dec.clear();
		dec.resize(src.size());
		((OodleNetwork1UDP_Decode*)&virt[off_OodleNetwork1UDP_Decode])(&state[0], &shared[0], &enc[0], enc.size(), &dec[0], dec.size());

		if (memcmp(&src[0], &dec[0], src.size()) != 0)
			std::abort();

		if ((i & 0xFF) == 0xFF)
			std::cout << "Passed 0x" << (i + 1) << " iterations" << std::endl;
	}
	//*/
}