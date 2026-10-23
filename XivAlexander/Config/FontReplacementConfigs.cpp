#include "pch.h"
#include "FontReplacementConfigs.h"

namespace {
	using XivAlexander::FontReplacementAtlasTextureFormat;
	using XivAlexander::FontReplacementNamePlateMode;
	using XivAlexander::FontReplacementSubpixelMode;
	using XivAlexander::FontReplacementVerticalSubpixelRenderer;

	template<typename T>
	void Read(const nlohmann::json& j, const char* key, T& value) {
		if (const auto it = j.find(key); it != j.end())
			value = it->get<T>();
	}

	// The first name of each is the default, which unknown values are written and read as.
	constexpr std::pair<FontReplacementAtlasTextureFormat, const char*> AtlasTextureFormatNames[]{
		{FontReplacementAtlasTextureFormat::Bgra8888, "BGRA8888"},
		{FontReplacementAtlasTextureFormat::Bgra4444, "BGRA4444"},
	};

	constexpr std::pair<FontReplacementSubpixelMode, const char*> SubpixelModeNames[]{
		{FontReplacementSubpixelMode::Off, "Off"},
		{FontReplacementSubpixelMode::System, "System"},
		{FontReplacementSubpixelMode::Rgb, "RGB"},
		{FontReplacementSubpixelMode::Bgr, "BGR"},
		{FontReplacementSubpixelMode::VerticalRgb, "VerticalRGB"},
		{FontReplacementSubpixelMode::VerticalBgr, "VerticalBGR"},
	};

	constexpr std::pair<FontReplacementVerticalSubpixelRenderer, const char*> VerticalSubpixelRendererNames[]{
		{FontReplacementVerticalSubpixelRenderer::FreeType, "FreeType"},
		{FontReplacementVerticalSubpixelRenderer::DirectWrite, "DirectWrite"},
	};

	constexpr std::pair<FontReplacementNamePlateMode, const char*> NamePlateModeNames[]{
		{FontReplacementNamePlateMode::BakedAtFullSize, "BakedAtFullSize"},
		{FontReplacementNamePlateMode::Game, "Game"},
		{FontReplacementNamePlateMode::Live, "Live"},
	};

	template<typename T, size_t N>
	const char* NameOf(T value, const std::pair<T, const char*> (&names)[N]) {
		for (const auto& [v, name] : names) {
			if (v == value)
				return name;
		}
		return names[0].second;
	}

	template<typename T, size_t N>
	T ParseName(const nlohmann::json& j, const std::pair<T, const char*> (&names)[N]) {
		const auto s = j.is_string() ? j.get<std::string>() : std::string();
		for (const auto& [value, name] : names) {
			if (_stricmp(s.c_str(), name) == 0)
				return value;
		}
		return names[0].first;
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
		{"Format", NameOf(v.Format, AtlasTextureFormatNames)},
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
	if (const auto it = j.find("Format"); it != j.end())
		v.Format = ParseName(*it, AtlasTextureFormatNames);
}

void XivAlexander::to_json(nlohmann::json& j, const FontReplacementClearTypeConfig& v) {
	j = nlohmann::json::object({
		{"Level", v.Level},
		{"Contrast", v.Contrast},
		{"Gamma", v.Gamma},
		{"GrayscaleContrast", v.GrayscaleContrast},
	});
}

void XivAlexander::from_json(const nlohmann::json& j, FontReplacementClearTypeConfig& v) {
	v = {};
	Read(j, "Level", v.Level);
	Read(j, "Contrast", v.Contrast);
	Read(j, "Gamma", v.Gamma);
	Read(j, "GrayscaleContrast", v.GrayscaleContrast);
}

void XivAlexander::to_json(nlohmann::json& j, const FontReplacementSubpixelMode& v) {
	j = NameOf(v, SubpixelModeNames);
}

void XivAlexander::from_json(const nlohmann::json& j, FontReplacementSubpixelMode& v) {
	v = ParseName(j, SubpixelModeNames);
}

void XivAlexander::to_json(nlohmann::json& j, const FontReplacementVerticalSubpixelRenderer& v) {
	j = NameOf(v, VerticalSubpixelRendererNames);
}

void XivAlexander::from_json(const nlohmann::json& j, FontReplacementVerticalSubpixelRenderer& v) {
	v = ParseName(j, VerticalSubpixelRendererNames);
}

void XivAlexander::to_json(nlohmann::json& j, const FontReplacementNamePlateMode& v) {
	j = NameOf(v, NamePlateModeNames);
}

void XivAlexander::from_json(const nlohmann::json& j, FontReplacementNamePlateMode& v) {
	v = ParseName(j, NamePlateModeNames);
}
