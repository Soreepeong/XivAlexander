#include "pch.h"
#include "Sqex_EscapedString.h"

const std::string Sqex::EscapedString::EscapedNewLine = "\x02\x10\x01\x03";

void Sqex::EscapedString::Parse() const {
	if (!m_parsed.empty() || m_escaped.empty())
		return;

	std::string parsed;
	std::vector<std::string> components;
	parsed.reserve(m_escaped.size());

	std::span remaining{ reinterpret_cast<const uint8_t*>(&m_escaped[0]), m_escaped.size() };
	while (!remaining.empty()) {
		if (remaining.front() == SentinelCharacter) {
			if (remaining.size() < 3)
				throw std::invalid_argument("sentinel character occurred but there are less than 3 remaining bytes");

			// 0x00, byte, mark(0x02)
			// 0x01, byte, type
			// 0x02, byte, length specifier
			uint32_t len = remaining[2];
			switch (len) {
			case 0xF0:
			case 0xF1:
				// 1 bytes
				len = 1 + remaining[3] + 1;
				break;

			case 0xF2:
				// 2 bytes
				len = 1 + (remaining[4] | (remaining[3] << 8)) + 2;
				break;

			case 0xFA:
				// 3 bytes
				len = 1 + (remaining[5] | (remaining[4] << 8) | (remaining[3] << 16)) + 3;
				break;

			case 0xFE:
				// 4 bytes
				len = 1 + (remaining[6] | (remaining[5] << 8) | (remaining[4] << 16) | (remaining[3] << 24)) + 4;
				break;

			default:
				if (len >= 0xF0)
					throw std::runtime_error(std::format("Unknown length specifier value {:x}", remaining[-1]));
			}
			len += 3;

			const auto escaped = std::string_view(reinterpret_cast<const char*>(&remaining[0]), len);
			if (escaped == EscapedNewLine)
				parsed.push_back('\r');
			else {
				parsed.push_back(SentinelCharacter);
				components.emplace_back(escaped);
			}
			remaining = remaining.subspan(len);
		} else {
			parsed.push_back(remaining.front());
			remaining = remaining.subspan(1);
		}
	}

	m_parsed = std::move(parsed);
	m_components = std::move(components);
}

void Sqex::EscapedString::Escape() const {
	if (!m_escaped.empty() || m_parsed.empty())
		return;

	size_t reserveSize = m_parsed.size();
	for (const auto& component : m_components)
		reserveSize += component.size();

	std::string res;
	res.reserve(reserveSize);
	
	size_t escapeIndex = 0;
	for (const auto chr : m_parsed) {
		if (chr == SentinelCharacter)
			res += m_components[escapeIndex++];
		else if (chr == '\r')
			res += EscapedNewLine;
		else
			res += chr;
	}

	m_escaped = std::move(res);
}
