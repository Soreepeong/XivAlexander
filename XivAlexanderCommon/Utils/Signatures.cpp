#include "pch.h"
#include "Signatures.h"

bool Utils::Signatures::RegexSignature::Lookup(const void* data, size_t length, ScanResult& result, LookupFrom lookupFrom) const {
	auto base = static_cast<const char*>(data);
	if (result.ready()) {
		const auto prevBegin = static_cast<const char*>(result.begin(0));
		const auto prevEnd = static_cast<const char*>(result.end(0));

		if (prevBegin < base || static_cast<size_t>(prevBegin - base) >= length ||
			prevEnd < prevBegin || static_cast<size_t>(prevEnd - base) > length)
			return false;

		switch (lookupFrom) {
			case FromMatchEnd:
				length = static_cast<size_t>(base + length - prevEnd);
				base = prevEnd;
				break;
			case FromNextByte:
				length -= static_cast<size_t>(prevBegin - base) + 1;
				base = prevBegin + 1;
				break;
			default:
				throw std::invalid_argument("Invalid lookupFrom value");
		}
	}

	srell::cmatch match;
	if (!srell::regex_search(base, base + length, match, m_pattern))
		return false;

	result = ScanResult(std::move(match));
	return true;
}
