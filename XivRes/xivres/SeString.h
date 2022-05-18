#ifndef _XIVRES_SESTRING_H_
#define _XIVRES_SESTRING_H_

#include <cstdint>
#include <format>
#include <string>
#include <span>
#include <utility>
#include <vector>

#include "Internal/SpanCast.h"

namespace XivRes {
	class SeExpression {
	public:
		virtual size_t MaxLength() const {
			return Length();
		}

		virtual size_t Length() const = 0;

		virtual size_t EncodeTo(std::span<char> s) const = 0;

		virtual void EncodeAppendTo(std::string& s) const {
			const auto ptr = s.size();
			const auto length = Length();
			s.resize(ptr + length);
			EncodeTo(std::span(s).subspan(ptr, length));
		}

		virtual std::string Encode() {
			std::string buffer(Length(), '\0');
			EncodeTo(buffer);
			return buffer;
		}
	};

	class SeExpressionUint32 : public SeExpression {
		uint32_t m_value = 0;

	public:
		static constexpr size_t MaxLengthValue = 5;

		SeExpressionUint32(const std::string& s)
			: m_value(Decode(std::string_view(s))) {
		}

		SeExpressionUint32(std::string_view escaped)
			: m_value(Decode(escaped)) {
		}

		SeExpressionUint32(const SeExpressionUint32& r)
			: m_value(r.m_value) {
		}

		SeExpressionUint32(uint32_t value)
			: m_value(value) {
		}

		operator uint32_t() const {
			return m_value;
		}

		SeExpressionUint32& operator =(const SeExpressionUint32& r) {
			m_value = r.m_value;
			return *this;
		}

		SeExpressionUint32& operator =(uint32_t r) {
			m_value = r;
			return *this;
		}

		size_t MaxLength() const final {
			return MaxLengthValue;
		}

		size_t Length() const final {
			if (m_value < 0xCF)
				return 1;
			return size_t{ 1 }
				+ !!(m_value & 0xFF000000)
				+ !!(m_value & 0x00FF0000)
				+ !!(m_value & 0x0000FF00)
				+ !!(m_value & 0x000000FF);
		}

		size_t EncodeTo(std::span<char> s) const final {
			const auto res = Internal::span_cast<uint8_t>(s);
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

		static size_t ExpressionLength(char firstByte) {
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

		static uint32_t Decode(std::string_view escaped) {
			const auto data = Internal::span_cast<uint8_t>(escaped);
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

	};

	class SePayload {
	public:
		enum PayloadType : uint32_t {
			NewLine = 0x0F,
			Unset = UINT32_MAX,
		};

	protected:
		uint32_t m_type;
		std::string m_data;

	public:
		SePayload(uint32_t payloadType = PayloadType::Unset, std::string data = {})
			: m_type(payloadType)
			, m_data(data) {
		}

		SePayload(uint32_t payloadType, std::string_view data)
			: m_type(payloadType)
			, m_data(data) {
		}

		SePayload(const SePayload& r)
			: m_type(r.m_type)
			, m_data(r.m_data) {
		}

		SePayload(SePayload&& r) noexcept
			: m_type(r.m_type)
			, m_data(std::move(r.m_data)) {
			r.m_type = PayloadType::Unset;
		}

		SePayload& operator=(const SePayload& r) {
			m_type = r.m_type;
			m_data = r.m_data;
			return *this;
		}

		SePayload& operator=(SePayload&& r) noexcept {
			m_type = r.m_type;
			m_data = std::move(r.m_data);
			r.m_type = PayloadType::Unset;
			return *this;
		}

		uint32_t Type() const {
			return m_type;
		}

		const std::string& Data() const {
			return m_data;
		}
	};

	class SeString {
		static constexpr auto StartOfText = '\x02';
		static constexpr auto EndOfText = '\x03';

		bool m_newlineAsCarriageReturn = false;
		mutable std::string m_escaped;
		mutable std::string m_parsed;
		mutable std::vector<SePayload> m_payloads;

	public:
		SeString() = default;

		SeString(SeString&&) = default;

		SeString(const SeString&) = default;

		SeString(std::string escaped) {
			SetEscaped(std::move(escaped));
		}

		SeString(std::string parsed, std::vector<SePayload> payloads) {
			SetParsed(std::move(parsed), std::move(payloads));
		}

		SeString& operator=(SeString&&) = default;

		SeString& operator=(const SeString&) = default;

		bool operator==(const SeString& r) {
			return Escaped() == r.Escaped();
		}

		bool operator!=(const SeString& r) {
			return Escaped() != r.Escaped();
		}

		bool operator<(const SeString& r) {
			return Escaped() < r.Escaped();
		}

		bool operator<=(const SeString& r) {
			return Escaped() <= r.Escaped();
		}

		bool operator>(const SeString& r) {
			return Escaped() > r.Escaped();
		}

		bool operator>=(const SeString& r) {
			return Escaped() >= r.Escaped();
		}

		bool NewlineAsCarriageReturn() const {
			return m_newlineAsCarriageReturn;
		}

		void NewlineAsCarriageReturn(bool enable) {
			Escape();
			m_parsed.clear();
			m_payloads.clear();
			m_newlineAsCarriageReturn = enable;
		}

		bool Empty() const {
			return m_escaped.empty() && m_parsed.empty();
		}

		[[nodiscard]] const std::string& Parsed() const {
			Parse();
			return m_parsed;
		}

		SeString& SetParsed(std::string parsed, std::vector<SePayload> payloads) {
			VerifyComponents(parsed, payloads);
			m_parsed = std::move(parsed);
			m_payloads = std::move(payloads);
			m_escaped.clear();
			return *this;
		}

		SeString& SetParsedCompatible(std::string s) {
			Parse();
			VerifyComponents(s, m_payloads);
			m_parsed = std::move(s);
			m_escaped.clear();
			return *this;
		}

		[[nodiscard]] const std::string& Escaped() const {
			Escape();
			return m_escaped;
		}

		SeString& SetEscaped(std::string escaped) {
			m_escaped = std::move(escaped);
			m_parsed.clear();
			m_payloads.clear();
			return *this;
		}

		[[nodiscard]] const std::vector<SePayload>& Payloads() const {
			Parse();
			return m_payloads;
		}

	private:
		void Parse() const {
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

		void Escape() const {
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

		void VerifyComponents(const std::string& parsed, const std::vector<SePayload>& components) {
			const auto cnt = static_cast<size_t>(std::ranges::count(parsed, StartOfText));
			if (cnt != components.size())
				throw std::invalid_argument(std::format("number of sentinel characters({}) != expected number of sentinel characters({})", cnt, components.size()));
		}
	};
}

#endif
