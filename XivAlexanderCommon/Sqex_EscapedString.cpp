#include "pch.h"
#include "Sqex_EscapedString.h"

const std::string Sqex::EscapedString::EscapedNewLine = "\x02\x10\x01\x03";

static const char* consume(const char* ptr) {
	auto ptru8 = reinterpret_cast<const uint8_t*>(ptr);
	if (*ptr != '\x02')
		return ptr + 1;

	uint32_t len;
	ptru8 += 3;
	switch (ptru8[-1]) {
		case 0xF0:
		case 0xF1:
			// 1 bytes
			len = 1 + ptru8[0];
			ptru8 += 1;
			break;
		case 0xF2:
			// 2 bytes
			len = 1 + (ptru8[1] | (ptru8[0] << 8));
			ptru8 += 2;
			break;
		case 0xFA:
			// 3 bytes
			len = 1 + (ptru8[2] | (ptru8[1] << 8) | (ptru8[0] << 16));
			ptru8 += 3;
			break;
		case 0xFE:
			// 4 bytes
			len = 1 + (ptru8[3] | (ptru8[2] << 8) | (ptru8[1] << 16) | (ptru8[0] << 24));
			ptru8 += 4;
			break;
		default:
			if (ptru8[-1] < 0xF0)
				len = ptru8[-1];
			else {
				__debugbreak();
				len = 0;
			}
	}
	return reinterpret_cast<const char*>(ptru8 + len);
}

Sqex::EscapedString& Sqex::EscapedString::operator=(const std::string& src) {
	m_escapedLength = 0;
	m_filteredString.reserve(src.size());

	auto ptr = &src[0];
	while (*ptr) {
		if (*ptr == '\x02') {
			const auto nextPtr = consume(ptr);
			auto escaped = std::string(ptr, nextPtr);
			if (escaped == EscapedNewLine)
				m_filteredString.push_back('\r');
			else {
				m_filteredString.push_back(*ptr);
				m_escapedItems.emplace_back(std::move(escaped));
			}
			ptr = nextPtr;
		} else {
			m_filteredString.push_back(*ptr);
			ptr++;
		}
	}
	return *this;
}

void Sqex::EscapedString::FilteredString(std::string s) {
	if (static_cast<size_t>(std::ranges::count(s, SentinelCharacter)) != m_escapedItems.size())
		throw std::invalid_argument("Count of placeholders differ");
	m_filteredString = std::move(s);
}

Sqex::EscapedString::operator std::string() const {
	std::string res;
	size_t escapeIndex = 0;
	res.reserve(m_escapedLength + m_filteredString.size());
	for (const auto chr : m_filteredString) {
		if (chr == SentinelCharacter)
			res += m_escapedItems[escapeIndex++];
		else if (chr == '\r')
			res += EscapedNewLine;
		else
			res += chr;
	}
	return res;
}
