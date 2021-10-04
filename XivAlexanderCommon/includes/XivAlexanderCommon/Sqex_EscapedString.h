#pragma once

#include "Utils_Win32_Handle.h"

namespace Sqex {
	class EscapedString {
		static constexpr auto SentinelCharacter = '\x02';
		static const std::string EscapedNewLine;

		std::vector<std::string> m_escapedItems;
		size_t m_escapedLength = 0;
		std::string m_filteredString;

	public:
		EscapedString() = default;

		EscapedString(const std::string& src) {
			*this = src;
		}

		EscapedString& operator=(const std::string& src);

		[[nodiscard]] const std::string& FilteredString() const {
			return m_filteredString;
		}

		[[nodiscard]] const std::vector<std::string>& EscapedItems() const {
			return m_escapedItems;
		}

		void FilteredString(std::string s);

		operator std::string() const;
	};
}
