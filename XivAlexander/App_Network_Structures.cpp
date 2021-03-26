#include "pch.h"
#include "App_Network_Structures.h"

const std::vector<uint8_t> App::Network::Structures::FFXIVBundle::MagicConstant1 {
	0x52, 0x52, 0xa0, 0x41,
	0xff, 0x5d, 0x46, 0xe2,
	0x7f, 0x2a, 0x64, 0x4d, 
	0x7b, 0x99, 0xc4, 0x75,
};
const std::vector<uint8_t> App::Network::Structures::FFXIVBundle::MagicConstant2 {
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
};

void App::Network::Structures::FFXIVBundle::DebugPrint(LogCategory logCategory, const char* head) const {
	const auto st = Utils::EpochToLocalSystemTime(Timestamp);
	Misc::Logger::GetLogger().Format<LogLevel::Debug>(
		logCategory,
		"[%s / %04d-%02d-%02d %02d:%02d:%02d.%03d] Length=%d ConnType=%d Count=%d Gzip=%dn",
		head,
		st.wYear, st.wMonth, st.wDay,
		st.wHour, st.wMinute, st.wSecond,
		st.wMilliseconds,
		TotalLength, ConnType, MessageCount, GzipCompressed
	);
}

void App::Network::Structures::FFXIVMessage::DebugPrint(LogCategory logCategory, const char* head, bool dump) const {
	std::string dumpstr;
	if (Type == SegmentType::ClientKeepAlive || Type == SegmentType::ServerKeepAlive) {
		const auto st = Utils::EpochToLocalSystemTime(Data.KeepAlive.Epoch * 1000ULL);
		dumpstr += Utils::FormatString(
			"\n\tFFXIVMessage %04d-%02d-%02d %02d:%02d:%02d ID=%d",
			st.wYear, st.wMonth, st.wDay,
			st.wHour, st.wMinute, st.wSecond,
			Data.KeepAlive.Id
		);
	} else if (Type == SegmentType::IPC) {
		const auto st = Utils::EpochToLocalSystemTime(Data.IPC.Epoch * 1000ULL);
		dumpstr += Utils::FormatString(
			"\n\tFFXIVMessage %04d-%02d-%02d %02d:%02d:%02d Type=%04x SubType=%04x Unknown1=%04x SeqId=%04x Unknown2=%08x",
			st.wYear, st.wMonth, st.wDay,
			st.wHour, st.wMinute, st.wSecond,
			Data.IPC.Type, Data.IPC.SubType, Data.IPC.Unknown1, Data.IPC.ServerId, Data.IPC.Unknown2
			);
		if (dump) {
			dumpstr += "\n\t\t";
			const size_t dataLength = reinterpret_cast<const char*>(this) + this->Length - reinterpret_cast<const char*>(Data.IPC.Data.Raw);
			for (size_t i = 0; i < dataLength; ++i) {
				dumpstr += Utils::FormatString("%02x ", Data.IPC.Data.Raw[i] & 0xFF);
				if (i % 4 == 3 && i != dataLength - 1)
					dumpstr += " ";
				if (i % 32 == 31 && i != dataLength - 1)
					dumpstr += "\n\t\t";
			}
		}
	}
	Misc::Logger::GetLogger().Format<LogLevel::Debug>(
		logCategory,
		"[%s] Length=%d Source=%08x Current=%08x Type=%d%s",
		head, Length, SourceActor, CurrentActor, static_cast<int>(Type), dumpstr.c_str()
		);
}
