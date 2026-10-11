#include "pch.h"
#include "ResourceOverrideRules.h"

#include <nlohmann/json.hpp>

const srell::u8cregex& XivAlexander::LazyRegex::Get(const std::string& pattern) const {
	auto regex = m_regex.load();
	if (!regex) {
		// Threads racing here each compile one; the first one stored is used by all.
		auto compiled = std::make_shared<const srell::u8cregex>(pattern, srell::regex_constants::icase);
		if (m_regex.compare_exchange_strong(regex, compiled))
			regex = std::move(compiled);
	}
	return *regex;
}

const srell::u8cregex& XivAlexander::PathReplacementRule::Regex() const {
	return m_regex.Get(From);
}

bool XivAlexander::PathReplacementRule::operator==(const PathReplacementRule& r) const {
	return Enabled == r.Enabled && From == r.From && To == r.To && Stop == r.Stop;
}

void XivAlexander::to_json(nlohmann::json& j, const PathReplacementRule& v) {
	j = nlohmann::json::object({
		{"enabled", v.Enabled},
		{"from", v.From},
		{"to", v.To},
		{"stop", v.Stop},
	});
}

void XivAlexander::from_json(const nlohmann::json& j, PathReplacementRule& v) {
	v.Enabled = j.value("enabled", true);
	v.From = j.at("from").get<std::string>();
	v.To = j.at("to").get<std::string>();
	v.Stop = j.value("stop", true);
}

const srell::u8cregex& XivAlexander::LogPathFilter::Regex() const {
	return m_regex.Get(Pattern);
}

bool XivAlexander::LogPathFilter::operator==(const LogPathFilter& r) const {
	return Enabled == r.Enabled && Pattern == r.Pattern && Include == r.Include;
}

void XivAlexander::to_json(nlohmann::json& j, const LogPathFilter& v) {
	j = nlohmann::json::object({
		{"enabled", v.Enabled},
		{"pattern", v.Pattern},
		{"include", v.Include},
	});
}

void XivAlexander::from_json(const nlohmann::json& j, LogPathFilter& v) {
	v.Enabled = j.value("enabled", true);
	v.Pattern = j.at("pattern").get<std::string>();
	v.Include = j.value("include", true);
}

void XivAlexander::to_json(nlohmann::json& j, const ForcedCharacterLanguage& v) {
	j = nlohmann::json::object({
		{"enabled", v.Enabled},
		{"name", v.Name},
		{"language", v.Language},
	});
}

void XivAlexander::from_json(const nlohmann::json& j, ForcedCharacterLanguage& v) {
	v.Enabled = j.value("enabled", true);
	v.Name = j.value("name", std::string());
	v.Language = j.value("language", std::string());
}

std::vector<std::filesystem::path> XivAlexander::ToggledPath::EnabledOf(const std::vector<ToggledPath>& list) {
	std::vector<std::filesystem::path> res;
	for (const auto& entry : list) {
		if (entry.Enabled && !entry.Path.empty())
			res.push_back(entry.Path);
	}
	return res;
}

void XivAlexander::to_json(nlohmann::json& j, const ToggledPath& v) {
	j = nlohmann::json::object({
		{"path", v.Path},
		{"enabled", v.Enabled},
	});
}

void XivAlexander::from_json(const nlohmann::json& j, ToggledPath& v) {
	if (j.is_string()) {
		v.Enabled = true;
		v.Path = j.get<std::filesystem::path>();
		return;
	}
	v.Enabled = j.value("enabled", true);
	v.Path = j.value("path", std::filesystem::path());
}
