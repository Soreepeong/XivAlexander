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
		{"FamilyName", v.FamilyName},
		{"Weight", v.Weight},
		{"Stretch", v.Stretch},
		{"Style", v.Style},
	});
}

void XivAlexander::from_json(const nlohmann::json& j, FontReplacementFamilyFont& v) {
	v = {};
	Read(j, "Name", v.FamilyName); // What earlier versions called it.
	Read(j, "FamilyName", v.FamilyName);
	Read(j, "Weight", v.Weight);
	Read(j, "Stretch", v.Stretch);
	Read(j, "Style", v.Style);
}

void XivAlexander::to_json(nlohmann::json& j, const FontReplacementAdjustments& v) {
	j = nlohmann::json::object({
		{"Scripts", v.Scripts},
		{"NonCjkSize", v.NonCjkSize},
		{"CjkSize", v.CjkSize},
		{"Raise", v.Raise},
		{"NonCjkSpacing", v.NonCjkSpacing},
		{"CjkSpacing", v.CjkSpacing},
		{"StrokeWeight", v.StrokeWeight},
	});
}

void XivAlexander::from_json(const nlohmann::json& j, FontReplacementAdjustments& v) {
	v = {};
	Read(j, "Scripts", v.Scripts);
	Read(j, "NonCjkSize", v.NonCjkSize);
	Read(j, "CjkSize", v.CjkSize);
	Read(j, "Raise", v.Raise);
	Read(j, "NonCjkSpacing", v.NonCjkSpacing);
	Read(j, "CjkSpacing", v.CjkSpacing);
	Read(j, "StrokeWeight", v.StrokeWeight);
}

void XivAlexander::to_json(nlohmann::json& j, const FontReplacementFamilySource& v) {
	j = nlohmann::json::object({{"Enabled", v.Enabled}});
	if (v.IsPreset())
		j["Preset"] = v.Preset;
	else {
		j["Font"] = v.Font;
		j["MonospacedDigits"] = v.MonospacedDigits;
		if (v.Adjustments != FontReplacementAdjustments{})
			j["Adjustments"] = v.Adjustments;
	}
}

void XivAlexander::from_json(const nlohmann::json& j, FontReplacementFamilySource& v) {
	v = {};
	Read(j, "Enabled", v.Enabled);
	Read(j, "Preset", v.Preset);
	Read(j, "Font", v.Font);
	Read(j, "MonospacedDigits", v.MonospacedDigits);
	Read(j, "Adjustments", v.Adjustments);
}

void XivAlexander::to_json(nlohmann::json& j, const FontReplacementFamily& v) {
	j = nlohmann::json::object({
		{"Enabled", v.Enabled},
		{"Sources", v.Sources},
	});
}

void XivAlexander::from_json(const nlohmann::json& j, FontReplacementFamily& v) {
	v = {};
	Read(j, "Enabled", v.Enabled);
	Read(j, "Sources", v.Sources);
}

void XivAlexander::to_json(nlohmann::json& j, const FontReplacementEdgeConfig& v) {
	j = nlohmann::json::object({
		{"Enabled", v.Enabled},
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

	// Saved before it could be turned off: on if set to other than the defaults.
	if (j.contains("Enabled"))
		Read(j, "Enabled", v.Enabled);
	else
		v.Enabled = v != FontReplacementEdgeConfig{};
}

void XivAlexander::to_json(nlohmann::json& j, const FontReplacementAtlasConfig& v) {
	j = nlohmann::json::object({
		{"AxisPageWidth", v.AxisPageWidth},
		{"AxisPageHeight", v.AxisPageHeight},
		{"AxisMaxPages", v.AxisMaxPages},
		{"OtherPageWidth", v.OtherPageWidth},
		{"OtherPageHeight", v.OtherPageHeight},
		{"OtherMaxPages", v.OtherMaxPages},
		{"Format", v.Format == FontReplacementAtlasTextureFormat::Bgra4444 ? "BGRA4444" : "BGRA8888"},
	});
}

void XivAlexander::from_json(const nlohmann::json& j, FontReplacementAtlasConfig& v) {
	v = {};

	// Square pages were one size before.
	Read(j, "AxisPageSize", v.AxisPageWidth);
	Read(j, "AxisPageSize", v.AxisPageHeight);
	Read(j, "OtherPageSize", v.OtherPageWidth);
	Read(j, "OtherPageSize", v.OtherPageHeight);

	Read(j, "AxisPageWidth", v.AxisPageWidth);
	Read(j, "AxisPageHeight", v.AxisPageHeight);
	Read(j, "AxisMaxPages", v.AxisMaxPages);
	Read(j, "OtherPageWidth", v.OtherPageWidth);
	Read(j, "OtherPageHeight", v.OtherPageHeight);
	Read(j, "OtherMaxPages", v.OtherMaxPages);
	if (const auto it = j.find("Format"); it != j.end() && it->is_string() && _stricmp(it->get<std::string>().c_str(), "BGRA4444") == 0)
		v.Format = FontReplacementAtlasTextureFormat::Bgra4444;
}

void XivAlexander::to_json(nlohmann::json& j, const FontReplacementClearTypeConfig& v) {
	j = nlohmann::json::object({
		{"Level", v.Level},
		{"Contrast", v.Contrast},
		{"Gamma", v.Gamma},
	});
}

void XivAlexander::from_json(const nlohmann::json& j, FontReplacementClearTypeConfig& v) {
	v = {};
	Read(j, "Level", v.Level);
	Read(j, "Contrast", v.Contrast);
	Read(j, "Gamma", v.Gamma);
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
		case FontReplacementSubpixelMode::VerticalRgb:
			j = "VerticalRGB";
			break;
		case FontReplacementSubpixelMode::VerticalBgr:
			j = "VerticalBGR";
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
	else if (_stricmp(s.c_str(), "VerticalRGB") == 0)
		v = FontReplacementSubpixelMode::VerticalRgb;
	else if (_stricmp(s.c_str(), "VerticalBGR") == 0)
		v = FontReplacementSubpixelMode::VerticalBgr;
	else
		v = FontReplacementSubpixelMode::Off;
}

void XivAlexander::to_json(nlohmann::json& j, const FontReplacementVerticalSubpixelRenderer& v) {
	switch (v) {
		case FontReplacementVerticalSubpixelRenderer::DirectWrite:
			j = "DirectWrite";
			break;
		case FontReplacementVerticalSubpixelRenderer::FreeType:
		default:
			j = "FreeType";
	}
}

void XivAlexander::from_json(const nlohmann::json& j, FontReplacementVerticalSubpixelRenderer& v) {
	const auto s = j.is_string() ? j.get<std::string>() : std::string();
	v = _stricmp(s.c_str(), "DirectWrite") == 0 ? FontReplacementVerticalSubpixelRenderer::DirectWrite : FontReplacementVerticalSubpixelRenderer::FreeType;
}

void XivAlexander::to_json(nlohmann::json& j, const FontReplacementNamePlateMode& v) {
	switch (v) {
		case FontReplacementNamePlateMode::Game:
			j = "Game";
			break;
		case FontReplacementNamePlateMode::Live:
			j = "Live";
			break;
		case FontReplacementNamePlateMode::BakedAtFullSize:
		default:
			j = "BakedAtFullSize";
	}
}

void XivAlexander::from_json(const nlohmann::json& j, FontReplacementNamePlateMode& v) {
	const auto s = j.is_string() ? j.get<std::string>() : std::string();
	if (_stricmp(s.c_str(), "Game") == 0)
		v = FontReplacementNamePlateMode::Game;
	else if (_stricmp(s.c_str(), "Live") == 0)
		v = FontReplacementNamePlateMode::Live;
	else
		v = FontReplacementNamePlateMode::BakedAtFullSize;
}
