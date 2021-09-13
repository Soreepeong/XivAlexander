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

void Sqex::FontCsv::CreateConfig::to_json(nlohmann::json& j, const DirectWriteSource& o) {
	j = nlohmann::json::object({
		{"fontFile", ToUtf8(o.fontFile.wstring())},
		{"faceIndex", o.faceIndex},
		{"familyName", o.familyName},
		{"height", o.height},
		{"weight", o.weight},
		{"renderMode", o.renderMode},
		{"style", o.style},
		{"stretch", o.stretch},
		});
}

void Sqex::FontCsv::CreateConfig::from_json(const nlohmann::json& j, DirectWriteSource& o) {
	if (!j.is_object())
		throw std::invalid_argument(std::format("DirectWriteSource expected an object, got {}", j.type_name()));

	o.fontFile = j.value("fontFile", std::filesystem::path(""));
	o.faceIndex = j.value<uint32_t>("faceIndex", 0);
	o.familyName = j.value("familyName", "");
	if (o.fontFile.empty() && o.familyName.empty())
		throw std::invalid_argument("at least one of fontFile or familyName must be specified");
	o.height = j.at("height").get<double>();
	o.weight = ParseFontWeight(j, "weight");
	o.renderMode = j.value("renderMode", DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC);
	o.style = j.value("style", DWRITE_FONT_STYLE_NORMAL);
	o.stretch = j.value("stretch", DWRITE_FONT_STRETCH_NORMAL);
}

void Sqex::FontCsv::CreateConfig::to_json(nlohmann::json& j, const FreeTypeSource& o) {
	std::string loadFlagsDescription;
	if (o.loadFlags & FT_LOAD_NO_SCALE) loadFlagsDescription += " | FT_LOAD_NO_SCALE";
	if (o.loadFlags & FT_LOAD_NO_HINTING) loadFlagsDescription += " | FT_LOAD_NO_HINTING";
	if (o.loadFlags & FT_LOAD_RENDER) loadFlagsDescription += " | FT_LOAD_RENDER";
	if (o.loadFlags & FT_LOAD_NO_BITMAP) loadFlagsDescription += " | FT_LOAD_NO_BITMAP";
	if (o.loadFlags & FT_LOAD_VERTICAL_LAYOUT) loadFlagsDescription += " | FT_LOAD_VERTICAL_LAYOUT";
	if (o.loadFlags & FT_LOAD_FORCE_AUTOHINT) loadFlagsDescription += " | FT_LOAD_FORCE_AUTOHINT";
	if (o.loadFlags & FT_LOAD_CROP_BITMAP) loadFlagsDescription += " | FT_LOAD_CROP_BITMAP";
	if (o.loadFlags & FT_LOAD_PEDANTIC) loadFlagsDescription += " | FT_LOAD_PEDANTIC";
	if (o.loadFlags & FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH) loadFlagsDescription += " | FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH";
	if (o.loadFlags & FT_LOAD_NO_RECURSE) loadFlagsDescription += " | FT_LOAD_NO_RECURSE";
	if (o.loadFlags & FT_LOAD_IGNORE_TRANSFORM) loadFlagsDescription += " | FT_LOAD_IGNORE_TRANSFORM";
	if (o.loadFlags & FT_LOAD_MONOCHROME) loadFlagsDescription += " | FT_LOAD_MONOCHROME";
	if (o.loadFlags & FT_LOAD_LINEAR_DESIGN) loadFlagsDescription += " | FT_LOAD_LINEAR_DESIGN";
	if (o.loadFlags & FT_LOAD_NO_AUTOHINT) loadFlagsDescription += " | FT_LOAD_NO_AUTOHINT";
	if (o.loadFlags & FT_LOAD_COLOR) loadFlagsDescription += " | FT_LOAD_COLOR";
	if (o.loadFlags & FT_LOAD_COMPUTE_METRICS) loadFlagsDescription += " | FT_LOAD_COMPUTE_METRICS";
	if (o.loadFlags & FT_LOAD_BITMAP_METRICS_ONLY) loadFlagsDescription += " | FT_LOAD_BITMAP_METRICS_ONLY";
	if (loadFlagsDescription.empty())
		loadFlagsDescription = "FT_LOAD_DEFAULT";
	else
		loadFlagsDescription = loadFlagsDescription.substr(3);
	j = nlohmann::json::object({
		{"fontFile", ToUtf8(o.fontFile.wstring())},
		{"faceIndex", o.faceIndex},
		{"familyName", o.familyName},
		{"height", o.height},
		{"weight", o.weight},
		{"renderMode", loadFlagsDescription},
		{"style", o.style},
		{"stretch", o.stretch},
		});
}

void Sqex::FontCsv::CreateConfig::from_json(const nlohmann::json& j, FreeTypeSource& o) {
	if (!j.is_object())
		throw std::invalid_argument(std::format("FreeTypeSource expected an object, got {}", j.type_name()));

	o.fontFile = FromUtf8(j.value("fontFile", ""));
	o.faceIndex = j.value<uint32_t>("faceIndex", 0);
	o.familyName = j.value("familyName", "");
	if (o.fontFile.empty() && o.familyName.empty())
		throw std::invalid_argument("at least one of fontFile or familyName must be specified");
	o.height = j.at("height").get<double>();
	o.weight = ParseFontWeight(j, "weight");
	o.style = j.value("style", DWRITE_FONT_STYLE_NORMAL);
	o.stretch = j.value("stretch", DWRITE_FONT_STRETCH_NORMAL);
	o.loadFlags = 0;
	for (auto& flag : StringSplit<std::string>(j.value("renderMode", ""), "|")) {
		CharUpperA(&flag[0]);
		flag = StringTrim(flag);
		if (flag == "" || flag == "FT_LOAD_DEFAULT") void(0);
		else if (flag == "FT_LOAD_NO_SCALE") o.loadFlags |= (1L << 0);
		else if (flag == "FT_LOAD_NO_HINTING") o.loadFlags |= (1L << 1);
		else if (flag == "FT_LOAD_RENDER") o.loadFlags |= (1L << 2);
		else if (flag == "FT_LOAD_NO_BITMAP") o.loadFlags |= (1L << 3);
		else if (flag == "FT_LOAD_VERTICAL_LAYOUT") o.loadFlags |= (1L << 4);
		else if (flag == "FT_LOAD_FORCE_AUTOHINT") o.loadFlags |= (1L << 5);
		else if (flag == "FT_LOAD_CROP_BITMAP") o.loadFlags |= (1L << 6);
		else if (flag == "FT_LOAD_PEDANTIC") o.loadFlags |= (1L << 7);
		else if (flag == "FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH") o.loadFlags |= (1L << 9);
		else if (flag == "FT_LOAD_NO_RECURSE") o.loadFlags |= (1L << 10);
		else if (flag == "FT_LOAD_IGNORE_TRANSFORM") o.loadFlags |= (1L << 11);
		else if (flag == "FT_LOAD_MONOCHROME") o.loadFlags |= (1L << 12);
		else if (flag == "FT_LOAD_LINEAR_DESIGN") o.loadFlags |= (1L << 13);
		else if (flag == "FT_LOAD_NO_AUTOHINT") o.loadFlags |= (1L << 15);
		else if (flag == "FT_LOAD_COLOR") o.loadFlags |= (1L << 20);
		else if (flag == "FT_LOAD_COMPUTE_METRICS") o.loadFlags |= (1L << 21);
		else if (flag == "FT_LOAD_BITMAP_METRICS_ONLY") o.loadFlags |= (1L << 22);
		else
			throw std::invalid_argument(std::format("Unrecognized FreeType load flag \"{}\"", flag));
	}
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
	j = o.ranges;
}

void Sqex::FontCsv::CreateConfig::from_json(const nlohmann::json& j, RangeSet& o) {
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
	o.maxGlobalOffsetX = j.value<uint8_t>("maxGlobalOffsetX", 255);
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
		else if (value.isFreeTypeSource)
			sources.emplace(key, value.freeTypeSource);
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

		} else if (key.starts_with("freetype:")) {
			FreeTypeSource source;
			from_json(value, source);
			o.sources.emplace(key, InputFontSource{ .isFreeTypeSource = true, .freeTypeSource = source });

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

void to_json(nlohmann::json& j, const DWRITE_RENDERING_MODE& o) {
	switch (o) {
		case DWRITE_RENDERING_MODE_ALIASED: j = "DWRITE_RENDERING_MODE_ALIASED"; break;
		case DWRITE_RENDERING_MODE_GDI_CLASSIC: j = "DWRITE_RENDERING_MODE_GDI_CLASSIC"; break;
		case DWRITE_RENDERING_MODE_GDI_NATURAL: j = "DWRITE_RENDERING_MODE_GDI_NATURAL"; break;
		case DWRITE_RENDERING_MODE_NATURAL: j = "DWRITE_RENDERING_MODE_NATURAL"; break;
		case DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC: j = "DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC"; break;
		case DWRITE_RENDERING_MODE_OUTLINE: j = "DWRITE_RENDERING_MODE_OUTLINE"; break;
	}
}

void from_json(const nlohmann::json& j, DWRITE_RENDERING_MODE& o) {
	if (j.is_number_integer())
		o = static_cast<DWRITE_RENDERING_MODE>(j.get<int>());
	else {
		auto s = Utils::StringTrim(j.get<std::string>());
		CharUpperA(&s[0]);

		if (s == "DWRITE_RENDERING_MODE_ALIASED")
			o = DWRITE_RENDERING_MODE_ALIASED;

		else if (s == "DWRITE_RENDERING_MODE_GDI_CLASSIC")
			o = DWRITE_RENDERING_MODE_GDI_CLASSIC;

		else if (s == "DWRITE_RENDERING_MODE_GDI_NATURAL")
			o = DWRITE_RENDERING_MODE_GDI_NATURAL;

		else if (s == "DWRITE_RENDERING_MODE_NATURAL")
			o = DWRITE_RENDERING_MODE_NATURAL;

		else if (s == "DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC")
			o = DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;

		else if (s == "DWRITE_RENDERING_MODE_OUTLINE")
			o = DWRITE_RENDERING_MODE_OUTLINE;

		else
			throw std::invalid_argument(std::format("Unexpected value {} for renderMode", s));
	}
}

void to_json(nlohmann::json& j, const DWRITE_FONT_STYLE& o) {
	switch (o) {
		case DWRITE_FONT_STYLE_NORMAL: j = "DWRITE_FONT_STYLE_NORMAL"; break;
		case DWRITE_FONT_STYLE_OBLIQUE: j = "DWRITE_FONT_STYLE_OBLIQUE"; break;
		case DWRITE_FONT_STYLE_ITALIC: j = "DWRITE_FONT_STYLE_ITALIC"; break;
	}
}

void from_json(const nlohmann::json& j, DWRITE_FONT_STYLE& o) {
	if (j.is_number_integer())
		o = static_cast<DWRITE_FONT_STYLE>(j.get<int>());
	else {
		auto s = Utils::StringTrim(j.get<std::string>());
		CharUpperA(&s[0]);

		if (s == "DWRITE_FONT_STYLE_NORMAL")
			o = DWRITE_FONT_STYLE_NORMAL;

		else if (s == "DWRITE_FONT_STYLE_ITALIC")
			o = DWRITE_FONT_STYLE_ITALIC;

		else if (s == "DWRITE_FONT_STYLE_OBLIQUE")
			o = DWRITE_FONT_STYLE_OBLIQUE;

		else
			throw std::invalid_argument(std::format("Unexpected value {} for style", s));
	}
}

void to_json(nlohmann::json& j, const DWRITE_FONT_STRETCH& o) {
	switch (o) {
		case DWRITE_FONT_STRETCH_ULTRA_CONDENSED: j = "DWRITE_FONT_STRETCH_ULTRA_CONDENSED"; break;
		case DWRITE_FONT_STRETCH_EXTRA_CONDENSED: j = "DWRITE_FONT_STRETCH_EXTRA_CONDENSED"; break;
		case DWRITE_FONT_STRETCH_CONDENSED: j = "DWRITE_FONT_STRETCH_CONDENSED"; break;
		case DWRITE_FONT_STRETCH_SEMI_CONDENSED: j = "DWRITE_FONT_STRETCH_SEMI_CONDENSED"; break;
		case DWRITE_FONT_STRETCH_NORMAL: j = "DWRITE_FONT_STRETCH_NORMAL"; break;
		case DWRITE_FONT_STRETCH_SEMI_EXPANDED: j = "DWRITE_FONT_STRETCH_SEMI_EXPANDED"; break;
		case DWRITE_FONT_STRETCH_EXPANDED: j = "DWRITE_FONT_STRETCH_EXPANDED"; break;
		case DWRITE_FONT_STRETCH_EXTRA_EXPANDED: j = "DWRITE_FONT_STRETCH_EXTRA_EXPANDED"; break;
		case DWRITE_FONT_STRETCH_ULTRA_EXPANDED: j = "DWRITE_FONT_STRETCH_ULTRA_EXPANDED"; break;
	}
}

void from_json(const nlohmann::json& j, DWRITE_FONT_STRETCH& o) {
	if (j.is_number_integer())
		o = static_cast<DWRITE_FONT_STRETCH>(j.get<int>());
	else {
		const auto s = j.get<std::string>();
		if (s == "DWRITE_FONT_STRETCH_ULTRA_CONDENSED")
			o = DWRITE_FONT_STRETCH_ULTRA_CONDENSED;

		else if (s == "DWRITE_FONT_STRETCH_EXTRA_CONDENSED")
			o = DWRITE_FONT_STRETCH_EXTRA_CONDENSED;

		else if (s == "DWRITE_FONT_STRETCH_CONDENSED")
			o = DWRITE_FONT_STRETCH_CONDENSED;

		else if (s == "DWRITE_FONT_STRETCH_SEMI_CONDENSED")
			o = DWRITE_FONT_STRETCH_SEMI_CONDENSED;

		else if (s == "DWRITE_FONT_STRETCH_NORMAL")
			o = DWRITE_FONT_STRETCH_NORMAL;

		else if (s == "DWRITE_FONT_STRETCH_MEDIUM")
			o = DWRITE_FONT_STRETCH_MEDIUM;

		else if (s == "DWRITE_FONT_STRETCH_SEMI_EXPANDED")
			o = DWRITE_FONT_STRETCH_SEMI_EXPANDED;

		else if (s == "DWRITE_FONT_STRETCH_EXPANDED")
			o = DWRITE_FONT_STRETCH_EXPANDED;

		else if (s == "DWRITE_FONT_STRETCH_EXTRA_EXPANDED")
			o = DWRITE_FONT_STRETCH_EXTRA_EXPANDED;

		else if (s == "DWRITE_FONT_STRETCH_ULTRA_EXPANDED")
			o = DWRITE_FONT_STRETCH_ULTRA_EXPANDED;

		else
			throw std::invalid_argument(std::format("Unexpected value {} for stretch", s));
	}
}
