#include "pch.h"
#include "Signatures.h"

using XivAlexander::Game::Signatures::MatchCount;
using XivAlexander::Game::Signatures::RegexSignature;
using XivAlexander::Game::Signatures::ScanResult;

namespace byte_regex = xivres::util::byte_regex;

namespace {
	std::span<const uint8_t> Bytes(const char* begin, const char* end) {
		return {reinterpret_cast<const uint8_t*>(begin), static_cast<size_t>(end - begin)};
	}

	byte_regex::resume ToResume(RegexSignature::LookupFrom lookupFrom) {
		return lookupFrom == RegexSignature::FromMatchEnd ? byte_regex::resume::match_end : byte_regex::resume::next_byte;
	}
}

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
	SearchFrom(m_begin + byte_regex::resume_offset(m_current.Match(), Bytes(m_begin, m_end), ToResume(m_lookupFrom)));
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
	if (!byte_regex::search(*m_pattern, Bytes(m_begin, m_end), static_cast<size_t>(from - m_begin), match)) {
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

		from = begin + byte_regex::resume_offset(result.Match(), Bytes(begin, end), ToResume(lookupFrom));
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
	if (!byte_regex::match_at(m_pattern, Bytes(begin, begin + length), match))
		return false;

	result = ScanResult(std::move(match));
	return true;
}

std::pair<ScanResult, MatchCount> RegexSignature::LookupUnique(const void* data, size_t length, LookupFrom lookupFrom) const {
	const auto begin = static_cast<const char*>(data);
	auto found = byte_regex::find_unique(m_pattern, Bytes(begin, begin + length), ToResume(lookupFrom));
	if (!found.Count)
		return {ScanResult{}, MatchCount::None};
	return {ScanResult(std::move(found.First)), found.Count == 1 ? MatchCount::One : MatchCount::Many};
}
