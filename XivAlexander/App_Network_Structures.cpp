#include "pch.h"
#include "App_Network_Structures.h"

#include <XivAlexanderCommon/XaMisc.h>
#include <XivAlexanderCommon/XaZlib.h>

#include "App_Misc_Logger.h"

const uint8_t App::Network::Structures::FFXIVBundle::MagicConstant1[]{
	0x52, 0x52, 0xa0, 0x41,
	0xff, 0x5d, 0x46, 0xe2,
	0x7f, 0x2a, 0x64, 0x4d,
	0x7b, 0x99, 0xc4, 0x75,
};
const uint8_t App::Network::Structures::FFXIVBundle::MagicConstant2[]{
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
};

std::span<const uint8_t> App::Network::Structures::FFXIVBundle::ExtractFrontTrash(const std::span<const uint8_t>& buf) {
	const auto searchLength = std::min(sizeof Magic, buf.size());
	return {
		buf.begin(),
		std::min(
			std::search(buf.begin(), buf.end(), MagicConstant1, MagicConstant1 + searchLength),
			std::search(buf.begin(), buf.end(), MagicConstant2, MagicConstant2 + searchLength)
		)
	};
}

void App::Network::Structures::FFXIVBundle::DebugPrint(LogCategory logCategory, const char* head) const {
	const auto st = Utils::EpochToLocalSystemTime(Timestamp);
	Misc::Logger::Acquire()->Format<LogLevel::Debug>(
		logCategory,
		"[{} / {:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}] Length={} ConnType={} Count={} Gzip={}",
		head,
		st.wYear, st.wMonth, st.wDay,
		st.wHour, st.wMinute, st.wSecond,
		st.wMilliseconds,
		TotalLength, ConnType, MessageCount, GzipCompressed
		);
}

std::vector<std::vector<uint8_t>> App::Network::Structures::FFXIVBundle::SplitMessages(uint16_t expectedMessageCount, const std::span<const uint8_t>& buf) {
	std::vector<std::vector<uint8_t>> result;
	result.reserve(expectedMessageCount);
	for (size_t i = 0; i < buf.size();) {
		const auto& message = *reinterpret_cast<const FFXIVMessage*>(&buf[i]);
		if (i + message.Length > buf.size() || !message.Length)
			throw std::runtime_error("Could not parse game message (sum(message.length for each message) > total message length)");

		const auto sub = buf.subspan(i, static_cast<size_t>(message.Length));
		result.emplace_back(sub.begin(), sub.end());
		i += message.Length;
	}
	return result;
}

std::vector<std::vector<uint8_t>> App::Network::Structures::FFXIVBundle::GetMessages(Utils::ZlibReusableInflater& inflater) const {
	const std::span view(reinterpret_cast<const uint8_t*>(this) + GamePacketHeaderSize, TotalLength - GamePacketHeaderSize);

	if (GzipCompressed)
		return SplitMessages(MessageCount, inflater(view));
	else
		return SplitMessages(MessageCount, view);
}

std::string App::Network::Structures::FFXIVMessage::DebugPrint(const DumpConfig& dumpConfig) const {
	auto res = std::format("Me={:08x} Src={:08x} Len={} Type=", CurrentActor, SourceActor, Length);

	std::span<const uint8_t> dumpData;
	if (Type == SegmentType::ClientKeepAlive || Type == SegmentType::ServerKeepAlive) {
		const auto st = Utils::EpochToLocalSystemTime(Data.KeepAlive.Epoch * 1000LL);
		res += std::format(
			"{}({})\n\t#{} ({:04}-{:02}-{:02} {:02}:{:02}:{:02})",
			Type == SegmentType::ClientKeepAlive ? "ClientKeepAlive" : "ServerKeepAlive", static_cast<uint16_t>(Type),
			Data.KeepAlive.Id,
			st.wYear, st.wMonth, st.wDay,
			st.wHour, st.wMinute, st.wSecond
		);

	} else if (Type == SegmentType::IPC) {
		const auto st = Utils::EpochToLocalSystemTime(Data.IPC.Epoch * 1000LL);

		const char* pszPossibleMessageType = "";
		switch (dumpConfig.Guess) {
		case DumpConfig::Incoming:
			switch (Length) {
			case 0x09c: pszPossibleMessageType = ": ActionEffect01";
				break;
			case 0x29c: pszPossibleMessageType = ": ActionEffect08";
				break;
			case 0x4dc: pszPossibleMessageType = ": ActionEffect16";
				break;
			case 0x71c: pszPossibleMessageType = ": ActionEffect24";
				break;
			case 0x95c: pszPossibleMessageType = ": ActionEffect32";
				break;
			case 0x040: pszPossibleMessageType = ": ActorControlSelf, ActorCast";
				break;
			case 0x038: pszPossibleMessageType = ": ActorControl";
				break;
			case 0x078: pszPossibleMessageType = ": AddStatusEffect";
				break;
			}
			break;

		case DumpConfig::Outgoing:
			switch (Length) {
			case 0x038: pszPossibleMessageType = ": PositionUpdate";
				break;
			case 0x040: pszPossibleMessageType = ": ActionRequest, ActionRequestGroundTargeted, InteractTarget";
				break;
			}
			break;
		}

		res += std::format(
			"IPC({}){}\n\t#{} ({:04x}:{:04x}) u1={:04x} u2={:08x} ({:04}-{:02}-{:02} {:02}:{:02}:{:02})",
			static_cast<uint16_t>(Type), pszPossibleMessageType,
			Data.IPC.ServerId,
			static_cast<int>(Data.IPC.Type), Data.IPC.SubType,
			Data.IPC.Unknown1, Data.IPC.Unknown2,
			st.wYear, st.wMonth, st.wDay,
			st.wHour, st.wMinute, st.wSecond
		);
		dumpData = std::span(Data.IPC.Data.Raw, DataLength() - offsetof(IPCMessageData, Data));

	} else {
		res += std::format("Unknown({})", static_cast<uint16_t>(Type));
		dumpData = std::span(Data.Raw, DataLength());
	}

	if (dumpConfig.Dump && !dumpData.empty()) {
		for (size_t i = 0; i < dumpData.size(); i += dumpConfig.LineBreakUnit) {
			res += std::format("\n\t\t0x{:04x}\t", i);
			const auto lineSpan = dumpData.subspan(i, std::min(dumpConfig.LineBreakUnit, dumpData.size() - i));
			const auto lineSpanForUtf8 = dumpData.subspan(i, std::min(dumpConfig.LineBreakUnit + 4, dumpData.size() - i));
			for (size_t j = 0; j < lineSpan.size(); ++j) {
				res += std::format("{:02x} ", lineSpan[j]);
				if ((j + 1) % dumpConfig.DoubleSpacingUnit == 0)
					res.append(2, ' ');
				else if ((j + 1) % dumpConfig.SpacingUnit == 0)
					res.push_back(' ');
			}
			if (dumpConfig.DumpString) {
				for (size_t j = lineSpan.size(); j < dumpConfig.LineBreakUnit; ++j) {
					res.append(3, ' ');
					if ((j + 1) % dumpConfig.DoubleSpacingUnit == 0)
						res.append(2, ' ');
					else if ((j + 1) % dumpConfig.SpacingUnit == 0)
						res.push_back(' ');
				}
				res.push_back('\t');

				for (size_t j = 0; j < lineSpan.size();) {
					char32_t code = 0;
					std::span<const uint8_t> codeBytes;
					if ((lineSpan[j] & 0x80) == 0) {
						codeBytes = lineSpan.subspan(j, 1);
						code = codeBytes[0];
						j += 1;

					} else if (j + 1 < lineSpanForUtf8.size()
						&& (lineSpanForUtf8[j + 0] & 0xE0) == 0xC0
						&& (lineSpanForUtf8[j + 1] & 0xC0) == 0x80) {
						codeBytes = lineSpanForUtf8.subspan(j, 2);
						code = (codeBytes[0] & 0x1F) << 6
							| (codeBytes[1] & 0x3F);
						j += 2;

					} else if (j + 2 < lineSpanForUtf8.size()
						&& (lineSpanForUtf8[j + 0] & 0xF0) == 0xE0
						&& (lineSpanForUtf8[j + 1] & 0xC0) == 0x80
						&& (lineSpanForUtf8[j + 2] & 0xC0) == 0x80) {
						codeBytes = lineSpanForUtf8.subspan(j, 3);
						code = (codeBytes[0] & 0x0F) << 12
							| (codeBytes[1] & 0x3F) << 6
							| (codeBytes[2] & 0x3F);
						j += 3;

					} else if (j + 3 < lineSpanForUtf8.size()
						&& (lineSpanForUtf8[j + 0] & 0xF8) == 0xF0
						&& (lineSpanForUtf8[j + 1] & 0xC0) == 0x80
						&& (lineSpanForUtf8[j + 2] & 0xC0) == 0x80
						&& (lineSpanForUtf8[j + 3] & 0xC0) == 0x80) {
						codeBytes = lineSpanForUtf8.subspan(j, 4);
						code = (codeBytes[0] & 0x07) << 18
							| (codeBytes[1] & 0x3F) << 12
							| (codeBytes[2] & 0x3F) << 6
							| (codeBytes[3] & 0x3F);
						j += 4;

					} else {
						codeBytes = lineSpan.subspan(j, 1);
						code = U'.';  // invalid indicator
						j += 1;
					}

					switch (code) {
					case 0x0000:
						res.push_back(' ');
						break;

					case U'.': case 0x007F:
					case 0x0001: case 0x0002: case 0x0003: case 0x0004: case 0x0005: case 0x0006: case 0x0007:
					case 0x0008: case 0x0009: case 0x000a: case 0x000b: case 0x000c: case 0x000d: case 0x000e: case 0x000f:
					case 0x0010: case 0x0011: case 0x0012: case 0x0013: case 0x0014: case 0x0015: case 0x0016: case 0x0017:
					case 0x0018: case 0x0019: case 0x001a: case 0x001b: case 0x001c: case 0x001d: case 0x001e: case 0x001f:
					case 0x0080: case 0x0081: case 0x0082: case 0x0083: case 0x0084: case 0x0085: case 0x0086: case 0x0087:
					case 0x0088: case 0x0089: case 0x008a: case 0x008b: case 0x008c: case 0x008d: case 0x008e: case 0x008f:
					case 0x0090: case 0x0091: case 0x0092: case 0x0093: case 0x0094: case 0x0095: case 0x0096: case 0x0097:
					case 0x0098: case 0x0099: case 0x009a: case 0x009b: case 0x009c: case 0x009d: case 0x009e: case 0x009f:
					case 0x00AD: case 0x0600: case 0x0601: case 0x0602: case 0x0603: case 0x0604: case 0x0605: case 0x061C:
					case 0x06DD: case 0x070F: case 0x08E2: case 0x180E: case 0x200B: case 0x200C: case 0x200D: case 0x200E:
					case 0x200F: case 0x202A: case 0x202B: case 0x202C: case 0x202D: case 0x202E: case 0x2060: case 0x2061:
					case 0x2062: case 0x2063: case 0x2064: case 0x2066: case 0x2067: case 0x2068: case 0x2069: case 0x206A:
					case 0x206B: case 0x206C: case 0x206D: case 0x206E: case 0x206F: case 0xFEFF: case 0xFFF9: case 0xFFFA:
					case 0xFFFB:
					case 0x110BD: case 0x110CD: case 0x13430: case 0x13431: case 0x13432: case 0x13433: case 0x13434: case 0x13435:
					case 0x13436: case 0x13437: case 0x13438: case 0x1BCA0: case 0x1BCA1: case 0x1BCA2: case 0x1BCA3: case 0x1D173:
					case 0x1D174: case 0x1D175: case 0x1D176: case 0x1D177: case 0x1D178: case 0x1D179: case 0x1D17A: case 0xE0001:
					case 0xE0020: case 0xE0021: case 0xE0022: case 0xE0023: case 0xE0024: case 0xE0025: case 0xE0026: case 0xE0027:
					case 0xE0028: case 0xE0029: case 0xE002A: case 0xE002B: case 0xE002C: case 0xE002D: case 0xE002E: case 0xE002F:
					case 0xE0030: case 0xE0031: case 0xE0032: case 0xE0033: case 0xE0034: case 0xE0035: case 0xE0036: case 0xE0037:
					case 0xE0038: case 0xE0039: case 0xE003A: case 0xE003B: case 0xE003C: case 0xE003D: case 0xE003E: case 0xE003F:
					case 0xE0040: case 0xE0041: case 0xE0042: case 0xE0043: case 0xE0044: case 0xE0045: case 0xE0046: case 0xE0047:
					case 0xE0048: case 0xE0049: case 0xE004A: case 0xE004B: case 0xE004C: case 0xE004D: case 0xE004E: case 0xE004F:
					case 0xE0050: case 0xE0051: case 0xE0052: case 0xE0053: case 0xE0054: case 0xE0055: case 0xE0056: case 0xE0057:
					case 0xE0058: case 0xE0059: case 0xE005A: case 0xE005B: case 0xE005C: case 0xE005D: case 0xE005E: case 0xE005F:
					case 0xE0060: case 0xE0061: case 0xE0062: case 0xE0063: case 0xE0064: case 0xE0065: case 0xE0066: case 0xE0067:
					case 0xE0068: case 0xE0069: case 0xE006A: case 0xE006B: case 0xE006C: case 0xE006D: case 0xE006E: case 0xE006F:
					case 0xE0070: case 0xE0071: case 0xE0072: case 0xE0073: case 0xE0074: case 0xE0075: case 0xE0076: case 0xE0077:
					case 0xE0078: case 0xE0079: case 0xE007A: case 0xE007B: case 0xE007C: case 0xE007D: case 0xE007E: case 0xE007F:
						res.push_back('.');
						break;

					default:
						res.insert(res.end(), reinterpret_cast<const char*>(&codeBytes[0]), reinterpret_cast<const char*>(&codeBytes[0]) + codeBytes.size());
					}
				}
			}
		}
	}
	while (!res.empty() && std::isspace(res.back()))
		res.pop_back();

	return res;
}
