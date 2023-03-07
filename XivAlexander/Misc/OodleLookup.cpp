#include "pch.h"
#include "OodleLookup.h"
#include "Hooks.h"

bool XivAlexander::Misc::OodleLookup::Search(Utils::OodleNetworkFunctions& oodle) {
	const auto base = reinterpret_cast<char*>(GetModuleHandleW(nullptr));
	const auto& dos = *reinterpret_cast<IMAGE_DOS_HEADER*>(base);
	const auto& nt = *reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos.e_lfanew);

	std::span<char> virt;
	for (const auto& sh : std::span(IMAGE_FIRST_SECTION(&nt), nt.FileHeader.NumberOfSections)) {
		if (sh.Characteristics & IMAGE_SCN_CNT_CODE) {
			virt = {base + sh.VirtualAddress, sh.Misc.VirtualSize};
			break;
		}
	}

#ifdef _WIN64
	const auto InitOodle = Signatures::RegexSignature(R"(\x75.\x48\x8d\x15....\x48\x8d\x0d....\xe8(....)\xc6\x05....\x01.{0,256}\x75.\xb9(....)\xe8(....)\x45\x33\xc0\x33\xd2\x48\x8b\xc8\xe8.....{0,6}\x41\xb9(....)\xba.....{0,6}\x48\x8b\xc8\xe8(....))");
#else
	const auto InitOodle = Signatures::RegexSignature(R"(\x75\x16\x68....\x68....\xe8(....)\xc6\x05....\x01.{0,256}\x75\x27\x6a(.)\xe8(....)\x6a\x00\x6a\x00\x50\xe8....\x83\xc4.\x89\x46.\x68(....)\xff\x76.\x6a.\x50\xe8(....))");
#endif
	if (Signatures::ScanResult sr; InitOodle.Lookup(virt, sr)) {
		sr.ResolveAddressInto(oodle.SetMallocFree, 1);
		sr.GetInto(oodle.HtBits, 2);
		sr.ResolveAddressInto(oodle.SharedSize, 3);
		sr.GetInto(oodle.WindowSize, 4);
		sr.ResolveAddressInto(oodle.SharedSetWindow, 5);
	} else
		return false;

#ifdef _WIN64
	const auto SetUpStatesAndTrain = Signatures::RegexSignature(R"(\x75\x04\x48\x89\x7e.\xe8(....)\x4c..\xe8(....).{0,256}\x01\x75\x0a\x48\x8b\x0f\xe8(....)\xeb\x09\x48\x8b\x4f\x08\xe8(....))");
#else
	const auto SetUpStatesAndTrain = Signatures::RegexSignature(R"(\xe8(....)\x8b\xd8\xe8(....)\x83\x7d\x10\x01.{0,256}\x83\x7d\x10\x01\x6a\x00\x6a\x00\x6a\x00\xff\x77.\x75\x09\xff.\xe8(....)\xeb\x08\xff\x76.\xe8(....))");
#endif
	if (Signatures::ScanResult sr; SetUpStatesAndTrain.Lookup(virt, sr)) {
		sr.ResolveAddressInto(oodle.UdpStateSize, 1);
		sr.ResolveAddressInto(oodle.TcpStateSize, 2);
		sr.ResolveAddressInto(oodle.TcpTrain, 3);
		sr.ResolveAddressInto(oodle.UdpTrain, 4);
	} else
		return false;

#ifdef _WIN64
	const auto DecodeOodle = Signatures::RegexSignature(R"(\x4d\x85\xd2\x74\x0a\x49\x8b\xca\xe8(....)\xeb\x09\x48\x8b\x49\x08\xe8(....))");
	const auto EncodeOodle = Signatures::RegexSignature(R"(\x48\x85\xc0\x74\x0d\x48\x8b\xc8\xe8(....)\x48..\xeb\x0b\x48\x8b\x49\x08\xe8(....))");
	if (Signatures::ScanResult sr1, sr2; DecodeOodle.Lookup(virt, sr1) && EncodeOodle.Lookup(virt, sr2)) {
		sr1.ResolveAddressInto(oodle.TcpDecode, 1);
		sr1.ResolveAddressInto(oodle.UdpDecode, 2);
		sr2.ResolveAddressInto(oodle.TcpEncode, 1);
		sr2.ResolveAddressInto(oodle.UdpEncode, 2);
	} else
		return false;
#else
	const auto TcpCodecOodle = Signatures::RegexSignature("\x85\xc0\x74.\x50\xe8(....)\x57\x8b\xf0\xff\x15");
	const auto UdpCodecOodle = Signatures::RegexSignature("\xff\x71\x04\xe8(....)\x57\x8b\xf0\xff\x15");
	if (Signatures::ScanResult sr1, sr2; TcpCodecOodle.Lookup(virt, sr1) && UdpCodecOodle.Lookup(virt, sr2)) {
			sr1.ResolveAddressInto(oodle.TcpEncode, 1);
			sr2.ResolveAddressInto(oodle.UdpEncode, 1);

		if (TcpCodecOodle.Lookup(virt, sr1, true) && UdpCodecOodle.Lookup(virt, sr2, true)) {
			sr1.ResolveAddressInto(oodle.TcpDecode, 1);
			sr2.ResolveAddressInto(oodle.UdpDecode, 1);
		} else
			return false;
	} else
		return false;
#endif

	oodle.Found = true;
	return true;
}
