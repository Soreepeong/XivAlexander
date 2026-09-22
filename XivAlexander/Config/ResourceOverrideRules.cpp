#include "pch.h"
#include "ResourceOverrideRules.h"

#include <nlohmann/json.hpp>

const srell::u8cregex& XivAlexander::PathReplacementRule::Regex() const {
	if (!m_regex)
		m_regex.emplace(From, srell::regex_constants::icase);
	return *m_regex;
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
	if (!m_regex)
		m_regex.emplace(Pattern, srell::regex_constants::icase);
	return *m_regex;
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
