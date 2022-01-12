#pragma once

#include "XivAlexanderCommon/Utils/Win32/Handle.h"

namespace Sqex {
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

		size_t Length() const final;

		size_t EncodeTo(std::span<char> s) const final;

		static size_t ExpressionLength(char firstByte);

		static uint32_t Decode(std::string_view escaped);
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
		void Parse() const;
		void Escape() const;

		void VerifyComponents(const std::string& parsed, const std::vector<SePayload>& components) {
			const auto cnt = static_cast<size_t>(std::ranges::count(parsed, StartOfText));
			if (cnt != components.size())
				throw std::invalid_argument(std::format("number of sentinel characters({}) != expected number of sentinel characters({})", cnt, components.size()));
		}
	};
}
