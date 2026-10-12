#include "pch.h"
#include "FontReplacementConfigs.h"

namespace {
	template<typename T>
	void Read(const nlohmann::json& j, const char* key, T& value) {
		if (const auto it = j.find(key); it != j.end())
			value = it->get<T>();
	}
}

void XivAlexander::to_json(nlohmann::json& j, const FontReplacementFamilyFont& v) {
	j = nlohmann::json::object({
		{"Name", v.Name},
		{"Weight", v.Weight},
		{"Stretch", v.Stretch},
		{"Style", v.Style},
	});
}

void XivAlexander::from_json(const nlohmann::json& j, FontReplacementFamilyFont& v) {
	v = {};
	Read(j, "Name", v.Name);
	Read(j, "Weight", v.Weight);
	Read(j, "Stretch", v.Stretch);
	Read(j, "Style", v.Style);
}

void XivAlexander::to_json(nlohmann::json& j, const FontReplacementFamilySource& v) {
	j = nlohmann::json::object({{"Enabled", v.Enabled}});
	if (v.IsPreset())
		j["Preset"] = v.Preset;
	else
		j["Font"] = v.Font;
}

void XivAlexander::from_json(const nlohmann::json& j, FontReplacementFamilySource& v) {
	v = {};
	Read(j, "Enabled", v.Enabled);
	Read(j, "Preset", v.Preset);
	Read(j, "Font", v.Font);
}

void XivAlexander::to_json(nlohmann::json& j, const FontReplacementFamily& v) {
	j = nlohmann::json::object({
		{"Enabled", v.Enabled},
		{"MonospacedDigits", v.MonospacedDigits},
		{"Sources", v.Sources},
	});
}

void XivAlexander::from_json(const nlohmann::json& j, FontReplacementFamily& v) {
	v = {};
	Read(j, "Enabled", v.Enabled);
	Read(j, "MonospacedDigits", v.MonospacedDigits);
	Read(j, "Sources", v.Sources);
}

void XivAlexander::to_json(nlohmann::json& j, const FontReplacementEdgeConfig& v) {
	j = nlohmann::json::object({
		{"Scale", v.Scale},
		{"Min", v.Min},
		{"Max", v.Max},
		{"Softness", v.Softness},
	});
}

void XivAlexander::from_json(const nlohmann::json& j, FontReplacementEdgeConfig& v) {
	v = {};
	Read(j, "Scale", v.Scale);
	Read(j, "Min", v.Min);
	Read(j, "Max", v.Max);
	Read(j, "Softness", v.Softness);
}

void XivAlexander::to_json(nlohmann::json& j, const FontReplacementSubpixelMode& v) {
	switch (v) {
		case FontReplacementSubpixelMode::System:
			j = "System";
			break;
		case FontReplacementSubpixelMode::Rgb:
			j = "RGB";
			break;
		case FontReplacementSubpixelMode::Bgr:
			j = "BGR";
			break;
		case FontReplacementSubpixelMode::Off:
		default:
			j = "Off";
	}
}

void XivAlexander::from_json(const nlohmann::json& j, FontReplacementSubpixelMode& v) {
	const auto s = j.is_string() ? j.get<std::string>() : std::string();
	if (_stricmp(s.c_str(), "System") == 0)
		v = FontReplacementSubpixelMode::System;
	else if (_stricmp(s.c_str(), "RGB") == 0)
		v = FontReplacementSubpixelMode::Rgb;
	else if (_stricmp(s.c_str(), "BGR") == 0)
		v = FontReplacementSubpixelMode::Bgr;
	else
		v = FontReplacementSubpixelMode::Off;
}
