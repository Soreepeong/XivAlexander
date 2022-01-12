#include "pch.h"
#include "Structure.h"

#include "Utils/Utils.h"
#include "Utils/ZlibWrapper.h"

const uint8_t Sqex::Network::Structure::XivBundle::MagicConstant1[]{
	0x52, 0x52, 0xa0, 0x41,
	0xff, 0x5d, 0x46, 0xe2,
	0x7f, 0x2a, 0x64, 0x4d,
	0x7b, 0x99, 0xc4, 0x75,
};
const uint8_t Sqex::Network::Structure::XivBundle::MagicConstant2[]{
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
};

std::span<const uint8_t> Sqex::Network::Structure::XivBundle::ExtractFrontTrash(const std::span<const uint8_t>& buf) {
	const auto searchLength = std::min(sizeof Magic, buf.size());
	return {
		buf.begin(),
		std::min(
			std::search(buf.begin(), buf.end(), MagicConstant1, MagicConstant1 + searchLength),
			std::search(buf.begin(), buf.end(), MagicConstant2, MagicConstant2 + searchLength)
		)
	};
}

std::string Sqex::Network::Structure::XivBundle::Represent() const {
	const auto st = Utils::EpochToLocalSystemTime(Timestamp);
	return std::format(
		"[{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}] Length={} ConnType={} Count={} Gzip={}",
		st.wYear, st.wMonth, st.wDay,
		st.wHour, st.wMinute, st.wSecond,
		st.wMilliseconds,
		TotalLength, ConnType, MessageCount, GzipCompressed
	);
}

std::vector<std::vector<uint8_t>> Sqex::Network::Structure::XivBundle::SplitMessages(uint16_t expectedMessageCount, const std::span<const uint8_t>& buf) {
	std::vector<std::vector<uint8_t>> result;
	result.reserve(expectedMessageCount);
	for (size_t i = 0; i < buf.size();) {
		const auto& message = *reinterpret_cast<const XivMessage*>(&buf[i]);
		if (i + message.Length > buf.size() || !message.Length)
			throw std::runtime_error("Could not parse game message (sum(message.length for each message) > total message length)");

		const auto sub = buf.subspan(i, static_cast<size_t>(message.Length));
		result.emplace_back(sub.begin(), sub.end());
		i += message.Length;
	}
	return result;
}

std::vector<std::vector<uint8_t>> Sqex::Network::Structure::XivBundle::GetMessages(Utils::ZlibReusableInflater& inflater) const {
	const std::span view(reinterpret_cast<const uint8_t*>(this) + sizeof XivBundleHeader, TotalLength - sizeof XivBundleHeader);

	if (GzipCompressed)
		return SplitMessages(MessageCount, inflater(view));
	else
		return SplitMessages(MessageCount, view);
}

std::string Sqex::Network::Structure::XivMessage::Represent(bool dump) const {
	std::string dumpstr;
	if (Type == MessageType::ClientKeepAlive || Type == MessageType::ServerKeepAlive) {
		const auto st = Utils::EpochToLocalSystemTime(Data.KeepAlive.Epoch * 1000LL);
		dumpstr += std::format(
			"\n\tFFXIVMessage {:04}-{:02}-{:02} {:02}:{:02}:{:02} ID={}",
			st.wYear, st.wMonth, st.wDay,
			st.wHour, st.wMinute, st.wSecond,
			Data.KeepAlive.Id
		);
	} else if (Type == MessageType::Ipc) {
		const auto st = Utils::EpochToLocalSystemTime(Data.Ipc.Epoch * 1000LL);
		dumpstr += std::format(
			"\n\tFFXIVMessage {:04}-{:02}-{:02} {:02}:{:02}:{:02} Type={:04x} SubType={:04x} Unknown1={:04x} SeqId={:04x} Unknown2={:08x}",
			st.wYear, st.wMonth, st.wDay,
			st.wHour, st.wMinute, st.wSecond,
			static_cast<int>(Data.Ipc.Type), Data.Ipc.SubType, Data.Ipc.Unknown1, Data.Ipc.ServerId, Data.Ipc.Unknown2
		);
		if (dump) {
			dumpstr += "\n\t\t";
			const size_t dataLength = reinterpret_cast<const char*>(this) + this->Length - reinterpret_cast<const char*>(Data.Ipc.Data.Raw);
			for (size_t i = 0; i < dataLength; ++i) {
				dumpstr += std::format("{:02x} ", Data.Ipc.Data.Raw[i] & 0xFF);
				if (i % 4 == 3 && i != dataLength - 1)
					dumpstr += " ";
				if (i % 32 == 31 && i != dataLength - 1)
					dumpstr += "\n\t\t";
			}
		}
	}
	return std::format(
		"Length={} Source={:08x} Current={:08x} Type={}{}",
		Length, SourceActor, CurrentActor, static_cast<int>(Type), dumpstr
	);
}
