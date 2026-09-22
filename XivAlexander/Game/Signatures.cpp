#include "pch.h"
#include "Signatures.h"

using XivAlexander::Game::Signatures::MatchCount;
using XivAlexander::Game::Signatures::RegexSignature;
using XivAlexander::Game::Signatures::ScanResult;

RegexSignature::LookupResult::Iterator::Iterator(const srell::regex& pattern, const char* begin, const char* end, const char* from, LookupFrom lookupFrom)
	: m_pattern(&pattern)
	, m_begin(begin)
	, m_end(end)
	, m_lookupFrom(lookupFrom) {
	if (lookupFrom != FromNextByte && lookupFrom != FromMatchEnd)
		throw std::invalid_argument("Invalid lookupFrom value");

	SearchFrom(from);
}

RegexSignature::LookupResult::Iterator& RegexSignature::LookupResult::Iterator::operator++() {
	const auto first = static_cast<const char*>(m_current.begin(0));
	const auto last = static_cast<const char*>(m_current.end(0));
	SearchFrom(m_lookupFrom == FromMatchEnd && last != first ? last : first + 1);
	return *this;
}

RegexSignature::LookupResult::Iterator RegexSignature::LookupResult::Iterator::operator++(int) {
	auto prev = *this;
	++*this;
	return prev;
}

bool RegexSignature::LookupResult::Iterator::operator==(const Iterator& r) const {
	if (!m_pattern || !r.m_pattern)
		return m_pattern == r.m_pattern;
	return m_current.begin(0) == r.m_current.begin(0);
}

void RegexSignature::LookupResult::Iterator::SearchFrom(const char* from) {
	srell::cmatch match;
	if (from >= m_end || !srell::regex_search(from, m_end, m_begin, match, *m_pattern)) {
		m_pattern = nullptr;
		m_current = {};
		return;
	}

	m_current = ScanResult(std::move(match));
}

MatchCount RegexSignature::LookupResult::Count() const {
	auto it = m_first;
	if (it == std::default_sentinel)
		return MatchCount::None;
	return ++it == std::default_sentinel ? MatchCount::One : MatchCount::Many;
}

RegexSignature::LookupResult RegexSignature::Lookup(const void* data, size_t length, LookupFrom lookupFrom) const {
	const auto begin = static_cast<const char*>(data);
	return LookupResult(LookupResult::Iterator(m_pattern, begin, begin + length, begin, lookupFrom));
}

bool RegexSignature::Lookup(const void* data, size_t length, ScanResult& result, LookupFrom lookupFrom) const {
	const auto begin = static_cast<const char*>(data);
	const auto end = begin + length;
	auto from = begin;
	if (result.ready()) {
		const auto prevBegin = static_cast<const char*>(result.begin(0));
		const auto prevEnd = static_cast<const char*>(result.end(0));

		if (prevBegin < begin || prevBegin >= end || prevEnd < prevBegin || prevEnd > end)
			return false;

		from = lookupFrom == FromMatchEnd && prevEnd != prevBegin ? prevEnd : prevBegin + 1;
	}

	const LookupResult::Iterator it(m_pattern, begin, end, from, lookupFrom);
	if (it == std::default_sentinel)
		return false;

	result = *it;
	return true;
}

bool RegexSignature::MatchAt(const void* data, size_t length, ScanResult& result) const {
	const auto begin = static_cast<const char*>(data);
	srell::cmatch match;
	if (!srell::regex_search(begin, begin + length, match, m_pattern, srell::regex_constants::match_continuous))
		return false;

	result = ScanResult(std::move(match));
	return true;
}

std::pair<ScanResult, MatchCount> RegexSignature::LookupUnique(const void* data, size_t length, LookupFrom lookupFrom) const {
	auto it = Lookup(data, length, lookupFrom).begin();
	if (it == std::default_sentinel)
		return {ScanResult{}, MatchCount::None};

	auto first = *it;
	return {std::move(first), ++it == std::default_sentinel ? MatchCount::One : MatchCount::Many};
}
