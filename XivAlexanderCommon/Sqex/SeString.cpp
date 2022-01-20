#include "pch.h"
#include "XivAlexanderCommon/Sqex/SeString.h"

void Sqex::SeString::Parse() const {
	if (!m_parsed.empty() || m_escaped.empty())
		return;

	std::string parsed;
	std::vector<SePayload> payloads;
	parsed.reserve(m_escaped.size());

	std::string_view remaining(m_escaped);
	while (!remaining.empty()) {
		if (remaining[0] == StartOfText) {
			if (remaining.size() < 3)
				throw std::invalid_argument("STX occurred but there are less than 3 remaining bytes");
			remaining = remaining.substr(1);

			const auto payloadTypeLength = SeExpressionUint32::ExpressionLength(remaining[0]);
			if (payloadTypeLength == 0)
				throw std::invalid_argument("payload type length specifier is not a SeExpressionUint32");
			else if (remaining.size() < payloadTypeLength)
				throw std::invalid_argument("payload type length specifier is incomplete");
			const auto payloadType = SeExpressionUint32(remaining);
			remaining = remaining.substr(payloadTypeLength);

			const auto lengthLength = SeExpressionUint32::ExpressionLength(remaining[0]);
			if (lengthLength == 0)
				throw std::invalid_argument("payload data length specifier is not a SeExpressionUint32");
			else if (remaining.size() < lengthLength)
				throw std::invalid_argument("payload data length specifier is incomplete");
			const auto payloadLength = SeExpressionUint32(remaining);
			remaining = remaining.substr(lengthLength);

			if (remaining.size() < payloadLength)
				throw std::invalid_argument("payload is incomplete");
			auto payload = SePayload(payloadType, remaining.substr(0, payloadLength));
			remaining = remaining.substr(payloadLength);

			if (remaining.empty() || remaining[0] != EndOfText)
				throw std::invalid_argument("ETX not found");
			remaining = remaining.substr(1);

			if (m_newlineAsCarriageReturn && payload.Type() == SePayload::PayloadType::NewLine) {
				parsed.push_back('\r');
			} else {
				parsed.push_back(StartOfText);
				payloads.emplace_back(std::move(payload));
			}
		} else {
			parsed.push_back(remaining.front());
			remaining = remaining.substr(1);
		}
	}

	m_parsed = std::move(parsed);
	m_payloads = std::move(payloads);
}

void Sqex::SeString::Escape() const {
	if (!m_escaped.empty() || m_parsed.empty())
		return;

	size_t reserveSize = m_parsed.size();
	for (const auto& payload : m_payloads)
		reserveSize += 1  // STX
		+ SeExpressionUint32::MaxLengthValue  // payload type
		+ SeExpressionUint32::MaxLengthValue  // payload length
		+ payload.Data().size()  // payload
		+ 1;  // ETX

	std::string res;
	res.reserve(reserveSize);
	
	size_t escapeIndex = 0;
	for (const auto chr : m_parsed) {
		if (chr == '\r' && m_newlineAsCarriageReturn) {
			res += "\x02\x10\x01\x03";  // STX, SeExpressionUint32(PayloadType(NewLine)), SeExpressionUint32(0), ETX
		} else if (chr != StartOfText) {
			res += chr;
		} else {
			const auto ptr = res.size();
			const auto& payload = m_payloads[escapeIndex++];
			res += StartOfText;
			SeExpressionUint32(payload.Type()).EncodeAppendTo(res);
			SeExpressionUint32(static_cast<uint32_t>(payload.Data().size())).EncodeAppendTo(res);
			res += payload.Data();
			res += EndOfText;
		}
	}

	m_escaped = std::move(res);
}

size_t Sqex::SeExpressionUint32::Length() const {
	if (m_value < 0xCF)
		return 1;
	return size_t{1}
		+ !!(m_value & 0xFF000000)
		+ !!(m_value & 0x00FF0000)
		+ !!(m_value & 0x0000FF00)
		+ !!(m_value & 0x000000FF);
}

inline size_t Sqex::SeExpressionUint32::EncodeTo(std::span<char> s) const {
	const auto res = span_cast<uint8_t>(s);
	if (m_value < 0xCF) {
		res[0] = m_value + 1;
		return 1;
	} else {
		res[0] = 0xF0;
		size_t offset = 1;
		if (const auto v = (0xFF & (m_value >> 24))) res[0] |= 8, res[offset++] = v;
		if (const auto v = (0xFF & (m_value >> 16))) res[0] |= 4, res[offset++] = v;
		if (const auto v = (0xFF & (m_value >> 8))) res[0] |= 2, res[offset++] = v;
		if (const auto v = (0xFF & (m_value >> 0))) res[0] |= 1, res[offset++] = v;
		res[0] -= 1;
		return offset;
	}
}

size_t Sqex::SeExpressionUint32::ExpressionLength(char firstByte) {
	auto marker = static_cast<uint8_t>(firstByte);
	if (0x00 < marker && marker < 0xD0) {
		return 1;
	} else if (0xF0 <= marker && marker <= 0xFE) {
		marker += 1;
		return size_t{} + 1 + !!(marker & 8) + !!(marker & 4) + !!(marker & 2) + !!(marker & 1);
	} else {
		return 0;
	}
}

uint32_t Sqex::SeExpressionUint32::Decode(std::string_view escaped) {
	const auto data = span_cast<uint8_t>(escaped);
	auto marker = static_cast<uint8_t>(escaped[0]);
	if (0x00 < data[0] && data[0] < 0xD0) {
		return data[0] - 1;
	} else if (0xF0 <= data[0] && data[0] <= 0xFE) {
		const auto marker = data[0] + 1;
		uint32_t res = 0, offset = 0;
		if (marker & 8) res |= static_cast<uint8_t>(escaped[++offset]) << 24;
		if (marker & 4) res |= static_cast<uint8_t>(escaped[++offset]) << 16;
		if (marker & 2) res |= static_cast<uint8_t>(escaped[++offset]) << 8;
		if (marker & 1) res |= static_cast<uint8_t>(escaped[++offset]) << 0;
		return res;
	} else {
		throw std::invalid_argument("Not a SeExpressionUint32");
	}
}
