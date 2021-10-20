#pragma once

#include "Utils_Win32_Handle.h"

namespace Sqex {
	class EscapedString {
		static constexpr auto SentinelCharacter = '\x02';
		static const std::string EscapedNewLine;

		mutable std::string m_escaped;
		mutable std::string m_parsed;
		mutable std::vector<std::string> m_components;

	public:
		EscapedString() = default;
		EscapedString(EscapedString&&) = default;
		EscapedString(const EscapedString&) = default;

		EscapedString(std::string escaped) {
			SetEscaped(std::move(escaped));
		}

		EscapedString(std::string parsed, std::vector<std::string> components) {
			SetParsed(std::move(parsed), std::move(components));
		}

		EscapedString& operator=(EscapedString&&) = default;
		EscapedString& operator=(const EscapedString&) = default;

		bool operator==(const EscapedString& r) {
			return Escaped() == r.Escaped();
		}

		bool operator!=(const EscapedString& r) {
			return Escaped() != r.Escaped();
		}

		bool operator<(const EscapedString& r) {
			return Escaped() < r.Escaped();
		}

		bool operator<=(const EscapedString& r) {
			return Escaped() <= r.Escaped();
		}

		bool operator>(const EscapedString& r) {
			return Escaped() > r.Escaped();
		}

		bool operator>=(const EscapedString& r) {
			return Escaped() >= r.Escaped();
		}

		bool Empty() const {
			return m_escaped.empty() && m_parsed.empty();
		}

		[[nodiscard]] const std::string& Parsed() const {
			Parse();
			return m_parsed;
		}

		EscapedString& SetParsed(std::string parsed, std::vector<std::string> components) {
			VerifyComponents(parsed, components);
			m_parsed = std::move(parsed);
			m_components = std::move(components);
			m_escaped.clear();
			return *this;
		}

		EscapedString& SetParsedCompatible(std::string s) {
			Parse();
			VerifyComponents(s, m_components);
			m_parsed = std::move(s);
			m_escaped.clear();
			return *this;
		}

		[[nodiscard]] const std::string& Escaped() const {
			Escape();
			return m_escaped;
		}

		EscapedString& SetEscaped(std::string escaped) {
			m_escaped = std::move(escaped);
			m_parsed.clear();
			m_components.clear();
			return *this;
		}

		[[nodiscard]] const std::vector<std::string>& Components() const {
			Parse();
			return m_components;
		}

	private:
		void Parse() const;
		void Escape() const;

		void VerifyComponents(const std::string& parsed, const std::vector<std::string>& components) {
			const auto cnt = static_cast<size_t>(std::ranges::count(parsed, SentinelCharacter));
			if (cnt != components.size())
				throw std::invalid_argument(std::format("number of sentinel characters({}) != expected number of sentinel characters({})", cnt, components.size()));
		}
	};
}
