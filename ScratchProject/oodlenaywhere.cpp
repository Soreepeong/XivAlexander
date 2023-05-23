#define _CRT_SECURE_NO_WARNINGS

#include <fstream>
#include <iostream>
#include <span>
#include <regex>
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

void* __stdcall my_malloc(size_t size, int align) {
	const auto pRaw = (char*)malloc(size + align + sizeof(void*) - 1);
	if (!pRaw)
		return nullptr;

	const auto pAligned = (void*)(((size_t)pRaw + align + 7) & (size_t)-align);
	*((void**)pAligned - 1) = pRaw;
	return pAligned;
}

void __stdcall my_free(void* p) {
	free(*((void**)p - 1));
}

using OodleNetwork1_Shared_Size = std::remove_pointer_t<int(STDCALL*)(int htbits)>;
using OodleNetwork1_Shared_SetWindow = std::remove_pointer_t<void(STDCALL*)(void* data, int htbits, void* window, int windowSize)>;
using OodleNetwork1_Proto_Train = std::remove_pointer_t<void(STDCALL*)(void* state, void* shared, const void* const* trainingPacketPointers, const int* trainingPacketSizes, int trainingPacketCount)>;
using OodleNetwork1_Proto_State_Size = std::remove_pointer_t<int(STDCALL*)(void)>;
using OodleNetwork1_UDP_Decode = std::remove_pointer_t<bool(STDCALL*)(const void* state, void* shared, const void* compressed, size_t compressedSize, void* raw, size_t rawSize)>;
using OodleNetwork1_UDP_Encode = std::remove_pointer_t<int(STDCALL*)(const void* state, const void* shared, const void* raw, size_t rawSize, void* compressed)>;
using OodleNetwork1_TCP_Decode = std::remove_pointer_t<bool(STDCALL*)(void* state, void* shared, const void* compressed, size_t compressedSize, void* raw, size_t rawSize)>;
using OodleNetwork1_TCP_Encode = std::remove_pointer_t<int(STDCALL*)(void* state, const void* shared, const void* raw, size_t rawSize, void* compressed)>;
using Oodle_Malloc = std::remove_pointer_t<void* (STDCALL*)(size_t size, int align)>;
using Oodle_Free = std::remove_pointer_t<void(STDCALL*)(void* p)>;
using Oodle_SetMallocFree = std::remove_pointer_t<void(STDCALL*)(Oodle_Malloc* pfnMalloc, Oodle_Free* pfnFree)>;

class ScanResult {
	std::cmatch m_match;

public:
	ScanResult() = default;
	ScanResult(const ScanResult&) = default;
	ScanResult(ScanResult&&) noexcept = default;
	ScanResult& operator=(const ScanResult&) = default;
	ScanResult& operator=(ScanResult&&) noexcept = default;

	ScanResult(std::cmatch match)
		: m_match(std::move(match)) {
	}

	template<typename T>
	T& Get(size_t matchIndex) const {
		return *reinterpret_cast<T*>(const_cast<void*>(static_cast<const void*>(m_match[matchIndex].first)));
	}

	template<typename T>
	T* ResolveAddress(size_t matchIndex) const {
		return reinterpret_cast<T*>(m_match[matchIndex].first + 4 + Get<int32_t>(matchIndex));
	}

	template<typename T = void>
	T* begin(size_t matchIndex) {
		return reinterpret_cast<T*>(const_cast<void*>(static_cast<const void*>(&*m_match[matchIndex].first)));
	}

	template<typename T = void>
	void* end(size_t matchIndex) {
		return reinterpret_cast<T*>(const_cast<void*>(static_cast<const void*>(&*m_match[matchIndex].second)));
	}
};

class RegexSignature {
	const std::regex m_pattern;

public:
	template<size_t Length>
	RegexSignature(const char(&data)[Length])
		: m_pattern{ data, data + Length - 1 } {
	}

	bool Lookup(const void* data, size_t length, ScanResult& result, bool next = false) const {
		std::cmatch match;

		if (next) {
			const auto end = static_cast<const char*>(data) + length;
			const auto prevEnd = result.end(0);
			if (prevEnd >= end)
				return false;
			data = prevEnd;
		}

		if (!std::regex_search(static_cast<const char*>(data), static_cast<const char*>(data) + length, match, m_pattern))
			return false;

		result = ScanResult(std::move(match));
		return true;
	}

	template<typename T>
	bool Lookup(std::span<T> data, ScanResult& result, bool next = false) const {
		return Lookup(data.data(), data.size_bytes(), result, next);
	}
};

struct OodleXiv {
	int HtBits;
	int Window;
	OodleNetwork1_Shared_Size* SharedSize;
	Oodle_SetMallocFree* SetMallocFree;
	OodleNetwork1_Shared_SetWindow* SharedSetWindow;
	OodleNetwork1_Proto_State_Size* UdpStateSize;
	OodleNetwork1_Proto_State_Size* TcpStateSize;
	OodleNetwork1_Proto_Train* TcpTrain;
	OodleNetwork1_Proto_Train* UdpTrain;
	OodleNetwork1_TCP_Decode* TcpDecode;
	OodleNetwork1_UDP_Decode* UdpDecode;
	OodleNetwork1_TCP_Encode* TcpEncode;
	OodleNetwork1_UDP_Encode* UdpEncode;

	bool Lookup(std::span<char> virt) {
#ifdef _WIN64
		const auto InitOodle = RegexSignature(R"(\x75[\s\S]\x48\x8d\x15[\s\S][\s\S][\s\S][\s\S]\x48\x8d\x0d[\s\S][\s\S][\s\S][\s\S]\xe8([\s\S][\s\S][\s\S][\s\S])\xc6\x05[\s\S][\s\S][\s\S][\s\S]\x01[\s\S]{0,256}\x75[\s\S]\xb9([\s\S][\s\S][\s\S][\s\S])\xe8([\s\S][\s\S][\s\S][\s\S])\x45\x33\xc0\x33\xd2\x48\x8b\xc8\xe8[\s\S][\s\S][\s\S][\s\S][\s\S]{0,6}\x41\xb9([\s\S][\s\S][\s\S][\s\S])\xba[\s\S][\s\S][\s\S][\s\S][\s\S]{0,6}\x48\x8b\xc8\xe8([\s\S][\s\S][\s\S][\s\S]))");
#else
		const auto InitOodle = RegexSignature(R"(\x75\x16\x68[\s\S][\s\S][\s\S][\s\S]\x68[\s\S][\s\S][\s\S][\s\S]\xe8([\s\S][\s\S][\s\S][\s\S])\xc6\x05[\s\S][\s\S][\s\S][\s\S]\x01[\s\S]{0,256}\x75\x27\x6a([\s\S])\xe8([\s\S][\s\S][\s\S][\s\S])\x6a\x00\x6a\x00\x50\xe8[\s\S][\s\S][\s\S][\s\S]\x83\xc4[\s\S]\x89\x46[\s\S]\x68([\s\S][\s\S][\s\S][\s\S])\xff\x76[\s\S]\x6a[\s\S]\x50\xe8([\s\S][\s\S][\s\S][\s\S]))");
#endif
		if (ScanResult sr; InitOodle.Lookup(virt, sr)) {
			SetMallocFree = sr.ResolveAddress<Oodle_SetMallocFree>(1);
			HtBits = sr.Get<uint8_t>(2);
			SharedSize = sr.ResolveAddress<OodleNetwork1_Shared_Size>(3);
			Window = sr.Get<uint32_t>(4);
			SharedSetWindow = sr.ResolveAddress<OodleNetwork1_Shared_SetWindow>(5);
		} else
			return false;

#ifdef _WIN64
		const auto SetUpStatesAndTrain = RegexSignature(R"(\x75\x04\x48\x89\x7e[\s\S]\xe8([\s\S][\s\S][\s\S][\s\S])\x4c[\s\S][\s\S]\xe8([\s\S][\s\S][\s\S][\s\S])[\s\S]{0,256}\x01\x75\x0a\x48\x8b\x0f\xe8([\s\S][\s\S][\s\S][\s\S])\xeb\x09\x48\x8b\x4f\x08\xe8([\s\S][\s\S][\s\S][\s\S]))");
#else
		const auto SetUpStatesAndTrain = RegexSignature(R"(\xe8([\s\S][\s\S][\s\S][\s\S])\x8b\xd8\xe8([\s\S][\s\S][\s\S][\s\S])\x83\x7d\x10\x01[\s\S]{0,256}\x83\x7d\x10\x01\x6a\x00\x6a\x00\x6a\x00\xff\x77[\s\S]\x75\x09\xff[\s\S]\xe8([\s\S][\s\S][\s\S][\s\S])\xeb\x08\xff\x76[\s\S]\xe8([\s\S][\s\S][\s\S][\s\S]))");
#endif
		if (ScanResult sr; SetUpStatesAndTrain.Lookup(virt, sr)) {
			UdpStateSize = sr.ResolveAddress<OodleNetwork1_Proto_State_Size>(1);
			TcpStateSize = sr.ResolveAddress<OodleNetwork1_Proto_State_Size>(2);
			TcpTrain = sr.ResolveAddress<OodleNetwork1_Proto_Train>(3);
			UdpTrain = sr.ResolveAddress<OodleNetwork1_Proto_Train>(4);
		} else
			return false;

#ifdef _WIN64
		const auto DecodeOodle = RegexSignature(R"(\x4d\x85\xd2\x74\x0a\x49\x8b\xca\xe8([\s\S][\s\S][\s\S][\s\S])\xeb\x09\x48\x8b\x49\x08\xe8([\s\S][\s\S][\s\S][\s\S]))");
		const auto EncodeOodle = RegexSignature(R"(\x48\x85\xc0\x74\x0d\x48\x8b\xc8\xe8([\s\S][\s\S][\s\S][\s\S])\x48[\s\S][\s\S]\xeb\x0b\x48\x8b\x49\x08\xe8([\s\S][\s\S][\s\S][\s\S]))");
		if (ScanResult sr1, sr2; DecodeOodle.Lookup(virt, sr1) && EncodeOodle.Lookup(virt, sr2)) {
			TcpDecode = sr1.ResolveAddress<OodleNetwork1_TCP_Decode>(1);
			UdpDecode = sr1.ResolveAddress<OodleNetwork1_UDP_Decode>(2);
			TcpEncode = sr2.ResolveAddress<OodleNetwork1_TCP_Encode>(1);
			UdpEncode = sr2.ResolveAddress<OodleNetwork1_UDP_Encode>(2);
		} else
			return false;
#else
		const auto TcpCodecOodle = RegexSignature(R"(\x85\xc0\x74[\s\S]\x50\xe8([\s\S][\s\S][\s\S][\s\S])\x57\x8b\xf0\xff\x15)");
		const auto UdpCodecOodle = RegexSignature(R"(\xff\x71\x04\xe8([\s\S][\s\S][\s\S][\s\S])\x57\x8b\xf0\xff\x15)");
		if (ScanResult sr1, sr2; TcpCodecOodle.Lookup(virt, sr1) && UdpCodecOodle.Lookup(virt, sr2)) {
			TcpEncode = sr1.ResolveAddress<OodleNetwork1_TCP_Encode>(1);
			UdpEncode = sr2.ResolveAddress<OodleNetwork1_UDP_Encode>(1);
			// NOTE: compressed buffer size must be (8 + input.size)

			if (TcpCodecOodle.Lookup(virt, sr1, true) && UdpCodecOodle.Lookup(virt, sr2, true)) {
				TcpDecode = sr1.ResolveAddress<OodleNetwork1_TCP_Decode>(1);
				UdpDecode = sr2.ResolveAddress<OodleNetwork1_UDP_Decode>(1);
			} else
				return false;
		} else
			return false;
#endif

		return true;
	}
};

class Oodle {
	OodleXiv m_oodleXiv;

	std::vector<uint8_t> m_shared;
	std::vector<uint8_t> m_state;
	std::vector<uint8_t> m_window;

	bool m_isTcp{};

public:
	Oodle() = default;
	Oodle(Oodle&&) noexcept = default;
	Oodle(const Oodle&) = delete;
	Oodle& operator=(Oodle&&) noexcept = default;
	Oodle& operator=(const Oodle&) = delete;

	void SetupUdp(const OodleXiv& oodleXiv) {
		m_oodleXiv = oodleXiv;
		m_isTcp = false;

		m_shared.clear();
		m_state.clear();
		m_window.clear();
		m_shared.resize(m_oodleXiv.SharedSize(m_oodleXiv.HtBits));
		m_state.resize(m_oodleXiv.UdpStateSize());
		m_window.resize(m_oodleXiv.Window);

		m_oodleXiv.SharedSetWindow(&m_shared[0], m_oodleXiv.HtBits, &m_window[0], m_oodleXiv.Window);
		m_oodleXiv.UdpTrain(&m_state[0], &m_shared[0], nullptr, nullptr, 0);
	}

	void SetupTcp(const OodleXiv& oodleXiv) {
		m_oodleXiv = oodleXiv;
		m_isTcp = true;

		m_shared.clear();
		m_state.clear();
		m_window.clear();
		m_shared.resize(m_oodleXiv.SharedSize(m_oodleXiv.HtBits));
		m_state.resize(m_oodleXiv.TcpStateSize());
		m_window.resize(m_oodleXiv.Window);

		m_oodleXiv.SharedSetWindow(&m_shared[0], m_oodleXiv.HtBits, &m_window[0], m_oodleXiv.Window);
		m_oodleXiv.TcpTrain(&m_state[0], &m_shared[0], nullptr, nullptr, 0);
	}

	size_t Encode(const void* source, size_t sourceLength, void* target, size_t targetLength) {
		if (targetLength < CompressedBufferSizeNeeded(sourceLength))
			return (std::numeric_limits<size_t>::max)();

		if (m_isTcp)
			return m_oodleXiv.TcpEncode(m_state.data(), m_shared.data(), source, sourceLength, target);
		else
			return m_oodleXiv.UdpEncode(m_state.data(), m_shared.data(), source, sourceLength, target);
	}

	size_t Decode(const void* source, size_t sourceLength, void* target, size_t targetLength) {
		if (m_isTcp)
			return m_oodleXiv.TcpDecode(m_state.data(), m_shared.data(), source, sourceLength, target, targetLength);
		else
			return m_oodleXiv.UdpDecode(m_state.data(), m_shared.data(), source, sourceLength, target, targetLength);
	}

	static size_t CompressedBufferSizeNeeded(size_t n) {
		return n + 8;
	}
};

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

	OodleXiv oodleXiv{};
	if (!oodleXiv.Lookup(virt)) {
		std::cerr << "Failed to look for signatures.\n";
		return -1;
	}

	oodleXiv.SetMallocFree(&my_malloc, &my_free);

	std::vector<uint8_t> src, dst;

	Oodle oodleTcp, oodleUdp;
	oodleTcp.SetupTcp(oodleXiv);
	oodleUdp.SetupUdp(oodleXiv);

	std::cerr << "Oodle helper running" << std::endl;
	while (true) {
		struct my_header_t {
			uint32_t SourceLength;
			uint32_t TargetLength;
			bool IsTcp;
			char Padding[3];
		} hdr{};
		static_assert(sizeof(my_header_t) == 12);

		fread(&hdr, sizeof(hdr), 1, stdin);
		if (!hdr.SourceLength)
			return 0;

		// std::cerr << "Request: src=0x" << hdr.SourceLength << " dst=0x" << hdr.TargetLength << std::endl;
		src.resize(hdr.SourceLength);
		fread(&src[0], 1, src.size(), stdin);

		if (hdr.TargetLength == 0xFFFFFFFFU) {
			dst.resize(Oodle::CompressedBufferSizeNeeded(src.size()));
			if (hdr.IsTcp)
				dst.resize(oodleTcp.Encode(src.data(), src.size(), dst.data(), dst.size()));
			else
				dst.resize(oodleUdp.Encode(src.data(), src.size(), dst.data(), dst.size()));
			// std::cerr << "Encoded: res=0x" << dst.size() << std::endl;
		} else {
			dst.resize(hdr.TargetLength);
			bool ok;
			if (hdr.IsTcp)
				ok = oodleTcp.Decode(src.data(), src.size(), dst.data(), dst.size());
			else
				ok = oodleUdp.Decode(src.data(), src.size(), dst.data(), dst.size());
			if (!ok) {
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