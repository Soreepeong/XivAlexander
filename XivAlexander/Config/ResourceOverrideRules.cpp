#include "pch.h"
#include "ResourceOverrideRules.h"

#include <mutex>

#include <nlohmann/json.hpp>

namespace {
	const srell::u8cregex& LazyRegex(std::optional<srell::u8cregex>& regex, const std::string& pattern) {
		static std::mutex s_mtx;
		const auto lock = std::lock_guard(s_mtx);
		if (!regex)
			regex.emplace(pattern, srell::regex_constants::icase);
		return *regex;
	}
}

const srell::u8cregex& XivAlexander::PathReplacementRule::Regex() const {
	return LazyRegex(m_regex, From);
}

bool XivAlexander::PathReplacementRule::operator==(const PathReplacementRule& r) const {
	return From == r.From && To == r.To && Stop == r.Stop;
}

void XivAlexander::to_json(nlohmann::json& j, const PathReplacementRule& v) {
	j = nlohmann::json::object({
		{"from", v.From},
		{"to", v.To},
		{"stop", v.Stop},
	});
}

void XivAlexander::from_json(const nlohmann::json& j, PathReplacementRule& v) {
	v.From = j.at("from").get<std::string>();
	v.To = j.at("to").get<std::string>();
	v.Stop = j.value("stop", true);
}

const srell::u8cregex& XivAlexander::LogPathFilter::Regex() const {
	return LazyRegex(m_regex, Pattern);
}

bool XivAlexander::LogPathFilter::operator==(const LogPathFilter& r) const {
	return Pattern == r.Pattern && Include == r.Include;
}

void XivAlexander::to_json(nlohmann::json& j, const LogPathFilter& v) {
	j = nlohmann::json::object({
		{"pattern", v.Pattern},
		{"include", v.Include},
	});
}

void XivAlexander::from_json(const nlohmann::json& j, LogPathFilter& v) {
	v.Pattern = j.at("pattern").get<std::string>();
	v.Include = j.value("include", true);
}
