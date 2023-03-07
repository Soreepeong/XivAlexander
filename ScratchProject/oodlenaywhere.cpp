#include "pch.h"

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

using OodleNetwork1_Shared_Size = std::remove_pointer_t<int(__stdcall*)(int htbits)>;
using OodleNetwork1_Shared_SetWindow = std::remove_pointer_t<void(__stdcall*)(void* data, int htbits, void* window, int windowSize)>;
using OodleNetwork1_Proto_Train = std::remove_pointer_t<void(__stdcall*)(void* state, void* shared, const void* const* trainingPacketPointers, const int* trainingPacketSizes, int trainingPacketCount)>;
using OodleNetwork1_Proto_State_Size = std::remove_pointer_t<int(__stdcall*)(void)>;
using OodleNetwork1_UDP_Decode = std::remove_pointer_t<bool(__stdcall*)(const void* state, void* shared, const void* compressed, size_t compressedSize, void* raw, size_t rawSize)>;
using OodleNetwork1_UDP_Encode = std::remove_pointer_t<int(__stdcall*)(const void* state, const void* shared, const void* raw, size_t rawSize, void* compressed)>;
using OodleNetwork1_TCP_Decode = std::remove_pointer_t<bool(__stdcall*)(void* state, void* shared, const void* compressed, size_t compressedSize, void* raw, size_t rawSize)>;
using OodleNetwork1_TCP_Encode = std::remove_pointer_t<int(__stdcall*)(void* state, const void* shared, const void* raw, size_t rawSize, void* compressed)>;
using Oodle_Malloc = std::remove_pointer_t<void* (__stdcall*)(size_t size, int align)>;
using Oodle_Free = std::remove_pointer_t<void(__stdcall*)(void* p)>;
using Oodle_SetMallocFree = std::remove_pointer_t<void(__stdcall*)(Oodle_Malloc* pfnMalloc, Oodle_Free* pfnFree)>;

class ScanResult {
	srell::cmatch m_match;

public:
	ScanResult() = default;
	ScanResult(const ScanResult&) = default;
	ScanResult(ScanResult&&) noexcept = default;
	ScanResult& operator=(const ScanResult&) = default;
	ScanResult& operator=(ScanResult&&) noexcept = default;

	ScanResult(srell::cmatch match)
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
	const srell::regex m_pattern;

public:
	template<size_t Length>
	RegexSignature(const char(&data)[Length])
		: m_pattern{ data, data + Length - 1, srell::regex_constants::dotall } {
	}

	bool Lookup(const void* data, size_t length, ScanResult& result, bool next = false) const {
		srell::cmatch match;

		if (next) {
			const auto end = static_cast<const char*>(data) + length;
			const auto prevEnd = result.end(0);
			if (prevEnd >= end)
				return false;
			data = prevEnd;
		}

		if (!srell::regex_search(static_cast<const char*>(data), static_cast<const char*>(data) + length, match, m_pattern))
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
		const auto InitOodle = RegexSignature("\\x75.\\x48\\x8d\\x15....\\x48\\x8d\\x0d....\\xe8(....)\\xc6\\x05....\\x01.{0,256}\\x75.\\xb9(....)\\xe8(....)\\x45\\x33\\xc0\\x33\\xd2\\x48\\x8b\\xc8\\xe8.....{0,6}\\x41\\xb9(....)\\xba.....{0,6}\\x48\\x8b\\xc8\\xe8(....)");
#else
		const auto InitOodle = RegexSignature("\\x75\\x16\\x68....\\x68....\\xe8(....)\\xc6\\x05....\\x01.{0,256}\\x75\\x27\\x6a(.)\\xe8(....)\\x6a\\x00\\x6a\\x00\\x50\\xe8....\\x83\\xc4.\\x89\\x46.\\x68(....)\\xff\\x76.\\x6a.\\x50\\xe8(....)");
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
		const auto SetUpStatesAndTrain = RegexSignature("\\x75\\x04\\x48\\x89\\x7e.\\xe8(....)\\x4c..\\xe8(....).{0,256}\\x01\\x75\\x0a\\x48\\x8b\\x0f\\xe8(....)\\xeb\\x09\\x48\\x8b\\x4f\\x08\\xe8(....)");
#else
		const auto SetUpStatesAndTrain = RegexSignature("\\xe8(....)\\x8b\\xd8\\xe8(....)\\x83\\x7d\\x10\\x01.{0,256}\\x83\\x7d\\x10\\x01\\x6a\\x00\\x6a\\x00\\x6a\\x00\\xff\\x77.\\x75\\x09\\xff.\\xe8(....)\\xeb\\x08\\xff\\x76.\\xe8(....)");
#endif
		if (ScanResult sr; SetUpStatesAndTrain.Lookup(virt, sr)) {
			UdpStateSize = sr.ResolveAddress<OodleNetwork1_Proto_State_Size>(1);
			TcpStateSize = sr.ResolveAddress<OodleNetwork1_Proto_State_Size>(2);
			TcpTrain = sr.ResolveAddress<OodleNetwork1_Proto_Train>(3);
			UdpTrain = sr.ResolveAddress<OodleNetwork1_Proto_Train>(4);
		} else
			return false;

#ifdef _WIN64
		const auto DecodeOodle = RegexSignature("\\x4d\\x85\\xd2\\x74\\x0a\\x49\\x8b\\xca\\xe8(....)\\xeb\\x09\\x48\\x8b\\x49\\x08\\xe8(....)");
		const auto EncodeOodle = RegexSignature("\\x48\\x85\\xc0\\x74\\x0d\\x48\\x8b\\xc8\\xe8(....)\\x48..\\xeb\\x0b\\x48\\x8b\\x49\\x08\\xe8(....)");
		if (ScanResult sr1, sr2; DecodeOodle.Lookup(virt, sr1) && EncodeOodle.Lookup(virt, sr2)) {
			TcpDecode = sr1.ResolveAddress<OodleNetwork1_TCP_Decode>(1);
			UdpDecode = sr1.ResolveAddress<OodleNetwork1_UDP_Decode>(2);
			TcpEncode = sr2.ResolveAddress<OodleNetwork1_TCP_Encode>(1);
			UdpEncode = sr2.ResolveAddress<OodleNetwork1_UDP_Encode>(2);
		} else
			return false;
#else
		const auto TcpCodecOodle = RegexSignature("\\x85\\xc0\\x74.\\x50\\xe8(....)\\x57\\x8b\\xf0\\xff\\x15");
		const auto UdpCodecOodle = RegexSignature("\\xff\\x71\\x04\\xe8(....)\\x57\\x8b\\xf0\\xff\\x15");
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

		m_oodleXiv.SetMallocFree(&my_malloc, &my_free);
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

		m_oodleXiv.SetMallocFree(&my_malloc, &my_free);
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
#ifdef _WIN64
	const auto GamePath = R"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\ffxiv_dx11.exe)";
#else
	const auto GamePath = R"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\ffxiv.exe)";
#endif

	std::ifstream game(GamePath, std::ios::binary);
	game.seekg(0, std::ios::end);
	std::vector<char> buf((size_t)game.tellg());
	game.seekg(0, std::ios::beg);
	game.read(&buf[0], buf.size());

	const auto& dosh = *(IMAGE_DOS_HEADER*)(&buf[0]);
	const auto& nth = *(IMAGE_NT_HEADERS*)(&buf[dosh.e_lfanew]);

	std::span<char> virt((char*)VirtualAlloc(nullptr, nth.OptionalHeader.SizeOfImage, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE), nth.OptionalHeader.SizeOfImage);

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
	oodleXiv.Lookup(virt);


	std::vector<uint8_t> src, dst;
	src.resize(256);
	for (auto i = 0; i < 256; i++)
		src[i] = i >> 4;
	dst.resize(Oodle::CompressedBufferSizeNeeded(src.size()));

	Oodle oodle;
	oodle.SetupTcp(oodleXiv);
	dst.resize(oodle.Encode(src.data(), src.size(), dst.data(), dst.size()));

	oodle.SetupTcp(oodleXiv);
	if (!oodle.Decode(dst.data(), dst.size(), src.data(), src.size())) {
		std::cerr << "Oodle encode/decode test failure" << std::endl;
		return -1;
	} else {
		std::cerr << "Oodle encode test: 256 -> " << dst.size() << std::endl;
	}
	for (int i = 0; i < 256; i++) {
		if (src[i] != i >> 4) {
			std::cerr << "Oodle encode/decode test failure" << std::endl;
			break;
		}
	}
	return 0;

	// Decode/Encode/WithTLS
	//   x64
	//     "\\x4d\\x85\\xd2\\x74\\x0a\\x49\\x8b\\xca\\xe8(....)\\xeb\\x09\\x48\\x8b\\x49\\x08\\xe8(....)"
	//                                ^ TcpDecode                      ^ UdpDecode
	//     "\\x48\\x85\\xc0\\x74\\x0d\\x48\\x8b\\xc8\\xe8(....)\\x48..\\xeb\\x0b\\x48\\x8b\\x49\\x08\\xe8(....)"
	//                                ^ TcpEncode                               ^ UdpEncode
	//   x86
	//     "\\x85\\xc0\\x74.\\x50\\xe8(....)\\x57\\x8b\\xf0\\xff\\x15(....)"
	//                            ^ TcpEncode (1st)         ^ TcpDecode (2nd)
	//     "\\xff\\x71\\x04\\xe8(....)\\x57\\x8b\\xf0\\xff\\x15(....)"
	//                       ^ UdpEncode (1st)         ^ UdpDecode (2nd)
	// 
	// 

	/*

	const auto cpfnOodleSetMallocFree = lookup_in_text(
		&virt[0],
		"\\x75\\x16\\x68\\x00\\x00\\x00\\x00\\x68\\x00\\x00\\x00\\x00\\xe8",
		"\\xff\\xff\\xff\\x00\\x00\\x00\\x00\\xff\\x00\\x00\\x00\\x00\\xe8",
		13) + 12;
	const auto pfnOodleSetMallocFree = (Oodle_SetMallocFree*)(cpfnOodleSetMallocFree + 5 + *(int*)(cpfnOodleSetMallocFree + 1));

	std::vector<const char*> calls;
	for (auto sig1 = lookup_in_text(
		&virt[0],
		"\\x83\\x7e\\x00\\x00\\x75\\x00\\x6a\\x13\\xe8\\x00\\x00\\x00\\x00\\x6a\\x00\\x6a\\x00\\x50\\xe8",
		"\\xff\\xff\\x00\\x00\\xff\\x00\\xff\\xff\\xff\\x00\\x00\\x00\\x00\\xff\\xff\\xff\\xff\\xff\\xff",
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
		"\\x8b\\x44\\x24\\x18\\x56\\x85\\xc0\\x7e\\x00\\x8b\\x74\\x24\\x14\\x85\\xf6\\x7e\\x00\\x3b\\xf0",
		"\\xff\\xff\\xff\\xff\\xff\\xff\\xff\\xff\\x00\\xff\\xff\\xff\\xff\\xff\\xff\\xff\\x00\\xff\\xff",
		19);

	const auto cpfnOodleNetwork1UDP_Encode = lookup_in_text(
		&virt[0],
		"\\x57\\xff\\x15\\x00\\x00\\x00\\x00\\xff\\x75\\x08\\x56\\xff\\x75\\x10\\xff\\x77\\x1c\\xff\\x77\\x18\\xe8",
		"\\xff\\xff\\xff\\x00\\x00\\x00\\x00\\xff\\xff\\xff\\xff\\xff\\xff\\xff\\xff\\xff\\xff\\xff\\xff\\xff\\xff",
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
	//*/
}