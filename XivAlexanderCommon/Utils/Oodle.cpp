#include "pch.h"
#include "Oodle.h"
#include "Signatures.h"
#include "Win32/Handle.h"
#include "Win32/Process.h"

Utils::Oodle::OodleModule::OodleModule() {
	try {
		const auto& currentProcess = Win32::Process::Current();
		const auto f = Win32::Handle::FromCreateFile(currentProcess.PathOf(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
		const auto optionalHeaderFromFile = f.Read<IMAGE_NT_HEADERS>(f.Read<IMAGE_DOS_HEADER>(0).e_lfanew).OptionalHeader;
		const auto allocation = currentProcess.VirtualAlloc(nullptr, optionalHeaderFromFile.SizeOfImage, MEM_RESERVE, PAGE_NOACCESS);
		m_memRelease = [&currentProcess, allocation] { currentProcess.VirtualFree(allocation, 0, MEM_RELEASE); };

		m_mem = { reinterpret_cast<char*>(allocation), optionalHeaderFromFile.SizeOfImage };

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
			*const_cast<size_t*>(&nt.OptionalHeader.ImageBase) += displacement;
			for (size_t i = relocDir.VirtualAddress, i_ = i + relocDir.Size; i < i_; ) {
				const auto& page = *reinterpret_cast<IMAGE_BASE_RELOCATION*>(&m_mem[i]);
				i += sizeof page;

				for (size_t j = sizeof page; j < page.SizeOfBlock && i < i_; j += 2, i += 2) {
					const auto entry = *reinterpret_cast<uint16_t*>(&m_mem[i]);
					const auto addr = static_cast<void*>(&m_mem[static_cast<size_t>(page.VirtualAddress) + (entry & 0xFFF)]);
					switch (entry >> 12) {
					case 0:
						break;
					case 3: {
						*static_cast<uint32_t*>(addr) += static_cast<uint32_t>(displacement);
						break;
					}
					case 10: {
						*static_cast<uint64_t*>(addr) += static_cast<uint64_t>(displacement);
						break;
					}
					default:
						// Should not happen (probably an error), but _hopefully_ irrelevant for oodling purposes
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

		std::span<char> codeSection;
		for (const auto& sh : std::span(IMAGE_FIRST_SECTION(&nt), nt.FileHeader.NumberOfSections)) {
			if (sh.Characteristics & IMAGE_SCN_CNT_CODE) {
				codeSection = m_mem.subspan(sh.VirtualAddress, sh.Misc.VirtualSize);
				break;
			}
		}

#ifdef _WIN64
		const auto InitOodle = Signatures::RegexSignature(R"(\x75.\x48\x8d\x15....\x48\x8d\x0d....\xe8(....)\xc6\x05....\x01.{0,256}\x75.\xb9(....)\xe8(....)\x45\x33\xc0\x33\xd2\x48\x8b\xc8\xe8.....{0,6}\x41\xb9(....)\xba.....{0,6}\x48\x8b\xc8\xe8(....))");
#else
		const auto InitOodle = Signatures::RegexSignature(R"(\x75\x16\x68....\x68....\xe8(....)\xc6\x05....\x01.{0,256}\x75\x27\x6a(.)\xe8(....)\x6a\x00\x6a\x00\x50\xe8....\x83\xc4.\x89\x46.\x68(....)\xff\x76.\x6a.\x50\xe8(....))");
#endif
		if (Signatures::ScanResult sr; InitOodle.Lookup(codeSection, sr)) {
			sr.ResolveAddressInto(SetMallocFree, 1);
			sr.GetInto(HtBits, 2);
			sr.ResolveAddressInto(SharedSize, 3);
			sr.GetInto(WindowSize, 4);
			sr.ResolveAddressInto(SharedSetWindow, 5);
		} else
			return;

#ifdef _WIN64
		const auto SetUpStatesAndTrain = Signatures::RegexSignature(R"(\x75\x04\x48\x89\x7e.\xe8(....)\x4c..\xe8(....).{0,256}\x01\x75\x0a\x48\x8b\x0f\xe8(....)\xeb\x09\x48\x8b\x4f\x08\xe8(....))");
#else
		const auto SetUpStatesAndTrain = Signatures::RegexSignature(R"(\xe8(....)\x8b\xd8\xe8(....)\x83\x7d\x10\x01.{0,256}\x83\x7d\x10\x01\x6a\x00\x6a\x00\x6a\x00\xff\x77.\x75\x09\xff.\xe8(....)\xeb\x08\xff\x76.\xe8(....))");
#endif
		if (Signatures::ScanResult sr; SetUpStatesAndTrain.Lookup(codeSection, sr)) {
			sr.ResolveAddressInto(UdpStateSize, 1);
			sr.ResolveAddressInto(TcpStateSize, 2);
			sr.ResolveAddressInto(TcpTrain, 3);
			sr.ResolveAddressInto(UdpTrain, 4);
		} else
			return;

#ifdef _WIN64
		const auto DecodeOodle = Signatures::RegexSignature(R"(\x4d\x85\xd2\x74\x0a\x49\x8b\xca\xe8(....)\xeb\x09\x48\x8b\x49\x08\xe8(....))");
		const auto EncodeOodle = Signatures::RegexSignature(R"(\x48\x85\xc0\x74\x0d\x48\x8b\xc8\xe8(....)\x48..\xeb\x0b\x48\x8b\x49\x08\xe8(....))");
		if (Signatures::ScanResult sr1, sr2; DecodeOodle.Lookup(codeSection, sr1) && EncodeOodle.Lookup(codeSection, sr2)) {
			sr1.ResolveAddressInto(TcpDecode, 1);
			sr1.ResolveAddressInto(UdpDecode, 2);
			sr2.ResolveAddressInto(TcpEncode, 1);
			sr2.ResolveAddressInto(UdpEncode, 2);
		} else
			return;
#else
		const auto TcpCodecOodle = Signatures::RegexSignature("\x85\xc0\x74.\x50\xe8(....)\x57\x8b\xf0\xff\x15");
		const auto UdpCodecOodle = Signatures::RegexSignature("\xff\x71\x04\xe8(....)\x57\x8b\xf0\xff\x15");
		if (Signatures::ScanResult sr1, sr2; TcpCodecOodle.Lookup(codeSection, sr1) && UdpCodecOodle.Lookup(codeSection, sr2)) {
			sr1.ResolveAddressInto(TcpEncode, 1);
			sr2.ResolveAddressInto(UdpEncode, 1);

			if (TcpCodecOodle.Lookup(codeSection, sr1, true) && UdpCodecOodle.Lookup(codeSection, sr2, true)) {
				sr1.ResolveAddressInto(TcpDecode, 1);
				sr2.ResolveAddressInto(UdpDecode, 1);
			} else
				return;
		} else
			return;
#endif

		SetMallocFree(&_aligned_malloc, &_aligned_free);

		Found = true;
	} catch (...) {
		Found = false;
	}
}

Utils::Oodle::OodleModule::~OodleModule() = default;

Utils::Oodle::Oodler::Oodler(const OodleModule& funcs, bool udp)
	: m_funcs(funcs)
	, m_udp(udp) {

	if (!m_funcs.Found)
		return;
	m_state.resize(udp ? m_funcs.UdpStateSize() : m_funcs.TcpStateSize());
	m_shared.resize(m_funcs.SharedSize(m_funcs.HtBits));
	m_window.resize(m_funcs.WindowSize);
	m_buffer.resize(65536);
	m_funcs.SharedSetWindow(m_shared.data(), m_funcs.HtBits, m_window.data(), static_cast<int>(m_window.size()));
	if (udp)
		m_funcs.UdpTrain(m_state.data(), m_shared.data(), nullptr, nullptr, 0);
	else
		m_funcs.TcpTrain(m_state.data(), m_shared.data(), nullptr, nullptr, 0);

}

Utils::Oodle::Oodler::~Oodler() = default;

std::span<uint8_t> Utils::Oodle::Oodler::Decode(std::span<const uint8_t> source, size_t decodedLength) {
	if (!m_funcs.Found)
		throw std::runtime_error("Oodle not initialized");
	m_buffer.resize(decodedLength);
	if (m_udp) {
		if (!m_funcs.UdpDecode(m_state.data(), m_shared.data(), source.data(), source.size(), m_buffer.data(), decodedLength))
			throw std::runtime_error("OodleNetwork1UDP_Decode error");
	} else {
		if (!m_funcs.TcpDecode(m_state.data(), m_shared.data(), source.data(), source.size(), m_buffer.data(), decodedLength))
			throw std::runtime_error("OodleNetwork1TCP_Decode error");
	}
	return { m_buffer };
}

std::span<uint8_t> Utils::Oodle::Oodler::Encode(std::span<const uint8_t> source) {
	if (!m_funcs.Found)
		throw std::runtime_error("Oodle not initialized");
	if (m_buffer.size() < MaxEncodedSize(source.size()))
		m_buffer.resize(MaxEncodedSize(source.size()));
	size_t size;
	if (m_udp) {
		size = m_funcs.UdpEncode(m_state.data(), m_shared.data(), source.data(), source.size(), m_buffer.data());
		if (!size)
			throw std::runtime_error("OodleNetwork1UDP_Encode error");
	} else {
		size = m_funcs.TcpEncode(m_state.data(), m_shared.data(), source.data(), source.size(), m_buffer.data());
		if (!size)
			throw std::runtime_error("OodleNetwork1TCP_Encode error");
	}
	return std::span(m_buffer).subspan(0, size);
}
