#include "pch.h"
#include "Sqex_FontCsv_CreateConfig.h"

#include "Sqex_FontCsv.h"

void Sqex::FontCsv::CreateConfig::to_json(nlohmann::json& j, const GameSource& o) {
	j = nlohmann::json::object({
		{"fdtPath", o.fdtPath},
		{"texturePath", o.texturePath},
		{"textureCount" ,o.textureCount},
		});
	if (!o.indexFile.empty())
		j.emplace("indexFile", o.indexFile);
}

void Sqex::FontCsv::CreateConfig::from_json(const nlohmann::json& j, GameSource& o) {
	if (!j.is_object())
		throw std::invalid_argument(std::format("GameSource expected an object, got {}", j.type_name()));

	if (const auto it = j.find("indexFile"); it != j.end())
		o.indexFile = j.is_null() ? std::string() : it->get<std::string>();

	o.fdtPath = j.at("fdtPath").get<std::filesystem::path>();
	o.texturePath = j.at("texturePath").get<std::filesystem::path>();
	o.textureCount = j.at("textureCount").get<size_t>();
	if (o.textureCount < 1)
		throw std::invalid_argument("textureCount must be >0");
}

void Sqex::FontCsv::CreateConfig::to_json(nlohmann::json& j, const DirectWriteSource& o) {
	j = nlohmann::json::object({
		{"familyName", o.familyName},
		{"height", o.height},
		{"weight", o.weight},
		});
	switch (o.renderMode) {
		case DWRITE_RENDERING_MODE_ALIASED: j.emplace("renderMode", "DWRITE_RENDERING_MODE_ALIASED"); break;
		case DWRITE_RENDERING_MODE_GDI_CLASSIC: j.emplace("renderMode", "DWRITE_RENDERING_MODE_GDI_CLASSIC"); break;
		case DWRITE_RENDERING_MODE_GDI_NATURAL: j.emplace("renderMode", "DWRITE_RENDERING_MODE_GDI_NATURAL"); break;
		case DWRITE_RENDERING_MODE_NATURAL: j.emplace("renderMode", "DWRITE_RENDERING_MODE_NATURAL"); break;
		case DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC: j.emplace("renderMode", "DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC"); break;
		case DWRITE_RENDERING_MODE_OUTLINE: j.emplace("renderMode", "DWRITE_RENDERING_MODE_OUTLINE"); break;
	}
	switch (o.style) {
		case DWRITE_FONT_STYLE_NORMAL: j.emplace("style", "DWRITE_FONT_STYLE_NORMAL"); break;
		case DWRITE_FONT_STYLE_OBLIQUE: j.emplace("style", "DWRITE_FONT_STYLE_OBLIQUE"); break;
		case DWRITE_FONT_STYLE_ITALIC: j.emplace("style", "DWRITE_FONT_STYLE_ITALIC"); break;
	}
	switch (o.stretch) {
		case DWRITE_FONT_STRETCH_ULTRA_CONDENSED: j.emplace("stretch", "DWRITE_FONT_STRETCH_ULTRA_CONDENSED"); break;
		case DWRITE_FONT_STRETCH_EXTRA_CONDENSED: j.emplace("stretch", "DWRITE_FONT_STRETCH_EXTRA_CONDENSED"); break;
		case DWRITE_FONT_STRETCH_CONDENSED: j.emplace("stretch", "DWRITE_FONT_STRETCH_CONDENSED"); break;
		case DWRITE_FONT_STRETCH_SEMI_CONDENSED: j.emplace("stretch", "DWRITE_FONT_STRETCH_SEMI_CONDENSED"); break;
		case DWRITE_FONT_STRETCH_NORMAL: j.emplace("stretch", "DWRITE_FONT_STRETCH_NORMAL"); break;
		case DWRITE_FONT_STRETCH_SEMI_EXPANDED: j.emplace("stretch", "DWRITE_FONT_STRETCH_SEMI_EXPANDED"); break;
		case DWRITE_FONT_STRETCH_EXPANDED: j.emplace("stretch", "DWRITE_FONT_STRETCH_EXPANDED"); break;
		case DWRITE_FONT_STRETCH_EXTRA_EXPANDED: j.emplace("stretch", "DWRITE_FONT_STRETCH_EXTRA_EXPANDED"); break;
		case DWRITE_FONT_STRETCH_ULTRA_EXPANDED: j.emplace("stretch", "DWRITE_FONT_STRETCH_ULTRA_EXPANDED"); break;
	}
}

static int ParseFontWeight(const nlohmann::json& j) {
	if (j.is_null() || j.empty())
		return 400;
	else if (j.is_number_integer()) {
		const auto w = j.get<int>();
		if (w < 1 || w> 1000)
			throw std::invalid_argument(std::format("weight must be between 1 and 1000 inclusive, got {}", w));
		return w;
	} else if (j.is_string()) {
		auto s = j.get<std::string>();
		s = Utils::StringTrim(s);
		s = Utils::StringReplaceAll<std::string>(s, "-", "");
		s = Utils::StringReplaceAll<std::string>(s, " ", "");
		s = Utils::StringReplaceAll<std::string>(s, "_", "");
		if (s.empty())
			return 400;

		// https://docs.microsoft.com/en-us/windows/win32/api/dwrite/ne-dwrite-dwrite_font_weight
		CharLowerA(&s[0]);
		if (s == "thin")
			return 100;
		else if (s == "extralight" || s == "ultralight")
			return 200;
		else if (s == "light")
			return 300;
		else if (s == "semilight")
			return 350;
		else if (s == "normal" || s == "medium")
			return 400;
		else if (s == "medium")
			return 500;
		else if (s == "demibold" || s == "bold")
			return 600;
		else if (s == "bold")
			return 700;
		else if (s == "extrabold" || s == "ultrabold")
			return 800;
		else if (s == "black" || s == "heavy")
			return 900;
		else if (s == "extrablack" || s == "ultraheavy")
			return 950;
		else
			throw std::invalid_argument(std::format("unrecognized weight name \"{}\"", j.get<std::string>()));
	} else if (j.is_boolean())
		return j.get<bool>() ? 700 : 400;
	else
		throw std::invalid_argument(std::format("Failed to interpret a value of type \"{}\" as a font weight", j.type_name()));
}
static int ParseFontWeight(const nlohmann::json& parent, const char* key) {
	if (const auto it = parent.find(key); it != parent.end())
		return ParseFontWeight(it.value());
	return 400;
}

void Sqex::FontCsv::CreateConfig::from_json(const nlohmann::json& j, DirectWriteSource& o) {
	if (!j.is_object())
		throw std::invalid_argument(std::format("DirectWriteSource expected an object, got {}", j.type_name()));

	o.familyName = j.at("familyName").get<std::string>();
	o.height = j.at("height").get<double>();
	o.weight = ParseFontWeight(j, "weight");

	if (const auto it = j.find("renderMode"); it != j.end()) {
		if (it.value().is_number_integer())
			o.renderMode = static_cast<DWRITE_RENDERING_MODE>(it->get<int>());
		else {
			const auto s = it->get<std::string>();
			if (s == "DWRITE_RENDERING_MODE_ALIASED")
				o.renderMode = DWRITE_RENDERING_MODE_ALIASED;

			else if (s == "DWRITE_RENDERING_MODE_GDI_CLASSIC")
				o.renderMode = DWRITE_RENDERING_MODE_GDI_CLASSIC;

			else if (s == "DWRITE_RENDERING_MODE_GDI_NATURAL")
				o.renderMode = DWRITE_RENDERING_MODE_GDI_NATURAL;

			else if (s == "DWRITE_RENDERING_MODE_NATURAL")
				o.renderMode = DWRITE_RENDERING_MODE_NATURAL;

			else if (s == "DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC")
				o.renderMode = DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;

			else if (s == "DWRITE_RENDERING_MODE_OUTLINE")
				o.renderMode = DWRITE_RENDERING_MODE_OUTLINE;

			else
				throw std::invalid_argument(std::format("Unexpected value {} for renderMode", s));
		}
	} else
		o.renderMode = DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC;

	if (const auto it = j.find("style"); it != j.end()) {
		if (it.value().is_number_integer())
			o.style = static_cast<DWRITE_FONT_STYLE>(it->get<int>());
		else {
			const auto s = it->get<std::string>();
			if (s == "DWRITE_FONT_STYLE_NORMAL")
				o.style = DWRITE_FONT_STYLE_NORMAL;

			else if (s == "DWRITE_FONT_STYLE_ITALIC")
				o.style = DWRITE_FONT_STYLE_ITALIC;

			else if (s == "DWRITE_FONT_STYLE_OBLIQUE")
				o.style = DWRITE_FONT_STYLE_OBLIQUE;

			else
				throw std::invalid_argument(std::format("Unexpected value {} for style", s));
		}
	} else
		o.style = DWRITE_FONT_STYLE_NORMAL;

	if (const auto it = j.find("stretch"); it != j.end()) {
		if (it.value().is_number_integer())
			o.stretch = static_cast<DWRITE_FONT_STRETCH>(it->get<int>());
		else {
			const auto s = it->get<std::string>();
			if (s == "DWRITE_FONT_STRETCH_ULTRA_CONDENSED")
				o.stretch = DWRITE_FONT_STRETCH_ULTRA_CONDENSED;

			else if (s == "DWRITE_FONT_STRETCH_EXTRA_CONDENSED")
				o.stretch = DWRITE_FONT_STRETCH_EXTRA_CONDENSED;

			else if (s == "DWRITE_FONT_STRETCH_CONDENSED")
				o.stretch = DWRITE_FONT_STRETCH_CONDENSED;

			else if (s == "DWRITE_FONT_STRETCH_SEMI_CONDENSED")
				o.stretch = DWRITE_FONT_STRETCH_SEMI_CONDENSED;

			else if (s == "DWRITE_FONT_STRETCH_NORMAL")
				o.stretch = DWRITE_FONT_STRETCH_NORMAL;

			else if (s == "DWRITE_FONT_STRETCH_MEDIUM")
				o.stretch = DWRITE_FONT_STRETCH_MEDIUM;

			else if (s == "DWRITE_FONT_STRETCH_SEMI_EXPANDED")
				o.stretch = DWRITE_FONT_STRETCH_SEMI_EXPANDED;

			else if (s == "DWRITE_FONT_STRETCH_EXPANDED")
				o.stretch = DWRITE_FONT_STRETCH_EXPANDED;

			else if (s == "DWRITE_FONT_STRETCH_EXTRA_EXPANDED")
				o.stretch = DWRITE_FONT_STRETCH_EXTRA_EXPANDED;

			else if (s == "DWRITE_FONT_STRETCH_ULTRA_EXPANDED")
				o.stretch = DWRITE_FONT_STRETCH_ULTRA_EXPANDED;

			else
				throw std::invalid_argument(std::format("Unexpected value {} for stretch", s));
		}
	} else
		o.stretch = DWRITE_FONT_STRETCH_NORMAL;
}

void Sqex::FontCsv::CreateConfig::to_json(nlohmann::json& j, const GdiSource& o) {
	// https://docs.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-logfontw
	// TODO: 
	j = nlohmann::json::object({
		{"height", o.lfHeight},
		{"width", o.lfWidth},
		{"escapement", o.lfEscapement},
		{"orientation", o.lfOrientation},
		{"weight", o.lfWeight},
		{"italic", !!o.lfItalic},
		{"underline", !!o.lfUnderline},
		{"strikeOut", !!o.lfStrikeOut},
		{"charSet", o.lfCharSet},
		{"outPrecision", o.lfOutPrecision},
		{"lfClipPrecision", o.lfClipPrecision},
		{"lfQuality", o.lfQuality},
		{"lfPitchAndFamily", o.lfPitchAndFamily},
		{"faceName", Utils::ToUtf8(o.lfFaceName)},
		});
}

void Sqex::FontCsv::CreateConfig::from_json(const nlohmann::json& j, GdiSource& o) {
	o.lfHeight = j.at("height").get<LONG>();
	o.lfWidth = j.value<LONG>("width", 0);
	o.lfEscapement = j.value<LONG>("escapement", 0);
	o.lfOrientation = j.value<LONG>("orientation", 0);
	o.lfWeight = j.value<LONG>("weight", 0);
	o.lfItalic = j.value<BYTE>("italic", 0);
	o.lfUnderline = j.value<BYTE>("underline", 0);
	o.lfStrikeOut = j.value<BYTE>("strikeOut", 0);
	o.lfCharSet = j.value<BYTE>("charSet", DEFAULT_CHARSET);
	o.lfOutPrecision = j.value<BYTE>("outPrecision", OUT_DEFAULT_PRECIS);
	o.lfClipPrecision = j.value<BYTE>("lfClipPrecision", CLIP_DEFAULT_PRECIS);
	o.lfQuality = j.value<BYTE>("lfQuality", CLEARTYPE_NATURAL_QUALITY);
	o.lfPitchAndFamily = j.value<BYTE>("lfPitchAndFamily", FF_DONTCARE | DEFAULT_PITCH);
	const auto faceName = Utils::FromUtf8(j.at("faceName").get<std::string>());
	wcsncpy_s(o.lfFaceName, &faceName[0], faceName.size());
}

void Sqex::FontCsv::CreateConfig::to_json(nlohmann::json& j, const SingleRange& o) {
	if (o.from != o.to)
		j = nlohmann::json::array({
			std::format("U+{:04X}", static_cast<uint32_t>(o.from)),
			std::format("U+{:04X}", static_cast<uint32_t>(o.to)),
			});
	else
		j = std::format("U+{:04X}", static_cast<uint32_t>(o.from));
}

static char32_t ParseCodepointNotation(const nlohmann::json& j) {
	if (j.is_number_integer())
		return j.get<uint32_t>();
	else if (j.is_string()) {
		auto s = Utils::StringTrim(j.get<std::string>());
		if (s.starts_with("U+") || s.starts_with("u+")) {
			s[0] = '0';
			s[1] = 'x';
		}
		char* e = nullptr;
		const auto res = std::strtoul(&s[0], &e, 0);
		if (e != &s[0] + s.size())
			throw std::invalid_argument(std::format("Failed to parse {} as a codepoint", j.get<std::string>()));
		return res;
	} else
		throw std::invalid_argument(std::format("Failed to interpret a value of type \"{}\" as a codepoint", j.type_name()));
}

void Sqex::FontCsv::CreateConfig::from_json(const nlohmann::json& j, SingleRange& o) {
	if (j.is_array()) {
		if (j.size() == 1)
			o.from = o.to = ParseCodepointNotation(j.at(0));
		else if (j.size() == 2) {
			o.from = ParseCodepointNotation(j.at(0));
			o.to = ParseCodepointNotation(j.at(1));
			if (o.to < o.from) {
				const auto tmp = o.from;
				o.from = o.to;
				o.to = tmp;
			}
		} else
			throw std::invalid_argument("expected 1 or 2 items for codepoint range array");
	} else
		o.from = o.to = ParseCodepointNotation(j);
}

void Sqex::FontCsv::CreateConfig::to_json(nlohmann::json& j, const RangeSet& o) {
	//j = nlohmann::json::object();
	//for (const auto& [key, value] : o.ranges)
	//	j.emplace(key, value);
	j = o.ranges;
}

void Sqex::FontCsv::CreateConfig::from_json(const nlohmann::json& j, RangeSet& o) {
	//for (const auto& [k, v] : j.items()) {
	//	RangeSet set{};
	//	from_json(v, set);
	//	o.ranges.emplace(k, set);
	//}
	o.ranges = j.get<decltype(o.ranges)>();
}

void Sqex::FontCsv::CreateConfig::to_json(nlohmann::json& j, const SingleTargetComponent& o) {
	j = nlohmann::json::object({
		{"name", o.name},
		{"ranges", o.ranges},
		{"replace", o.replace},
		{"extendRange", o.extendRange},
		});
}

void Sqex::FontCsv::CreateConfig::from_json(const nlohmann::json& j, SingleTargetComponent& o) {
	o.name = j.at("name").get<std::string>();
	o.ranges = j.value("ranges", std::vector<std::string>());
	o.replace = j.value("replace", false);
	o.extendRange = j.value("extendRange", true);
}

void Sqex::FontCsv::CreateConfig::to_json(nlohmann::json& j, const SingleFontTarget& o) {
	j = nlohmann::json::object({
		{"height", o.height},
		{"ascent", nullptr},
		{"descent", nullptr},
		{"maxGlobalOffsetX", o.maxGlobalOffsetX},
		{"minGlobalOffsetX", o.minGlobalOffsetX},
		{"globalOffsetY", o.globalOffsetY},
		{"charactersToKernAcrossFonts", o.charactersToKernAcrossFonts},
		{"alignToBaseline", o.alignToBaseline},
		{"sources", o.sources},
		});
}

void Sqex::FontCsv::CreateConfig::from_json(const nlohmann::json& j, SingleFontTarget& o) {
	o.height = j.at("height").get<double>();
	if (const auto it = j.find("ascent"); it == j.end())
		o.autoAscent = true;
	else if (it->is_number())
		o.ascent = it->get<uint8_t>();
	else if (it->is_string())
		o.ascentFrom = it->get<std::string>();
	else if (it->is_null())
		o.autoAscent = true;
	else
		throw std::invalid_argument("invalid ascent value given");
	if (const auto it = j.find("descent"); it == j.end())
		o.autoDescent = true;
	else if (it->is_number())
		o.descent = it->get<uint8_t>();
	else if (it->is_string())
		o.descentFrom = it->get<std::string>();
	else if (it->is_null())
		o.autoDescent = true;
	else
		throw std::invalid_argument("invalid descent value given");
	o.maxGlobalOffsetX = j.value<uint8_t>("maxGlobalOffsetX", 6);
	o.minGlobalOffsetX = j.value<uint8_t>("minGlobalOffsetX", 0);
	o.globalOffsetY = j.value<uint8_t>("globalOffsetY", 0);
	o.charactersToKernAcrossFonts = ToU32(j.value("charactersToKernAcrossFonts", std::string(" ")));
	o.alignToBaseline = j.value("alignToBaseline", true);
	o.sources = j.at("sources").get<decltype(o.sources)>();
}

void Sqex::FontCsv::CreateConfig::to_json(nlohmann::json& j, const SingleTextureTarget& o) {
	j = o.fontTargets;
}

void Sqex::FontCsv::CreateConfig::from_json(const nlohmann::json& j, SingleTextureTarget& o) {
	o.fontTargets = j.get<decltype(o.fontTargets)>();
}

void Sqex::FontCsv::CreateConfig::to_json(nlohmann::json& j, const FontCreateConfig& o) {
	j = nlohmann::json::object({
		{"glyphGap", o.glyphGap},
		{"textureWidth", o.textureWidth},
		{"textureHeight", o.textureHeight},
		{"sources", nlohmann::json::object()},
		{"ranges", o.ranges},
		{"targets", o.targets},
		});
	auto& sources = j.at("sources");
	for (const auto& [key, value] : o.sources) {
		if (value.isGameSource)
			sources.emplace(key, value.gameSource);
		else if (value.isDirectWriteSource)
			sources.emplace(key, value.directWriteSource);
		else if (value.isGdiSource)
			sources.emplace(key, value.gdiSource);
	}
}

void Sqex::FontCsv::CreateConfig::from_json(const nlohmann::json& j, FontCreateConfig& o) {
	o.glyphGap = j.value<uint16_t>("glyphGap", 1);
	o.textureWidth = j.value<uint16_t>("textureWidth", 1024);
	o.textureHeight = j.value<uint16_t>("textureHeight", 1024);
	for (const auto& [key, value] : j.at("sources").items()) {
		if (key.starts_with("gdi:")) {
			GdiSource source;
			from_json(value, source);
			o.sources.emplace(key, InputFontSource{ .isGdiSource = true, .gdiSource = source });
		} else if (key.starts_with("dwrite:")) {
			DirectWriteSource source;
			from_json(value, source);
			o.sources.emplace(key, InputFontSource{ .isDirectWriteSource = true, .directWriteSource = source });
		} else if (key.starts_with("game:")) {
			GameSource source;
			from_json(value, source);
			o.sources.emplace(key, InputFontSource{ .isGameSource = true, .gameSource = source });
		} else
			throw std::invalid_argument(std::format("target font name \"{}\" has an unsupported prefix", key));
	}
	o.ranges = j.value("ranges", decltype(o.ranges)());
	o.targets = j.at("targets").get<decltype(o.targets)>();
}
