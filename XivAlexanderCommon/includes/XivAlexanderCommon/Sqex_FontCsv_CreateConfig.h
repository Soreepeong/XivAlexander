#pragma once

#include <string>
#include <map>
#include <vector>
#include <dwrite_3.h>
#include <nlohmann/json.hpp>

#include "Sqex_Texture.h"

namespace Sqex::FontCsv::CreateConfig {
	struct GameSource {
		std::filesystem::path indexFile;
		std::filesystem::path fdtPath;
		std::filesystem::path texturePath;
	};
	void to_json(nlohmann::json& j, const GameSource& o);
	void from_json(const nlohmann::json& j, GameSource& o);

	struct DirectWriteSource {
		std::filesystem::path fontFile;
		uint32_t faceIndex;
		std::string familyName;
		double height;
		int weight;
		DWRITE_RENDERING_MODE renderMode;
		DWRITE_FONT_STYLE style;
		DWRITE_FONT_STRETCH stretch;
	};
	void to_json(nlohmann::json& j, const DirectWriteSource& o);
	void from_json(const nlohmann::json& j, DirectWriteSource& o);

	struct FreeTypeSource {
		std::filesystem::path fontFile;
		uint32_t faceIndex;
		std::string familyName;
		double height;
		int weight;
		DWRITE_FONT_STYLE style;
		DWRITE_FONT_STRETCH stretch;
		uint32_t loadFlags;
	};
	void to_json(nlohmann::json& j, const FreeTypeSource& o);
	void from_json(const nlohmann::json& j, FreeTypeSource& o);

	struct GdiSource : LOGFONTW {
	};
	void to_json(nlohmann::json& j, const GdiSource& o);
	void from_json(const nlohmann::json& j, GdiSource& o);

	struct InputFontSource {
		bool isGameSource = false;
		bool isDirectWriteSource = false;
		bool isGdiSource = false;
		bool isFreeTypeSource = false;
		GameSource gameSource;
		DirectWriteSource directWriteSource;
		GdiSource gdiSource;
		FreeTypeSource freeTypeSource;
	};

	struct SingleRange {
		char32_t from;
		char32_t to;
	};
	void to_json(nlohmann::json& j, const SingleRange& o);
	void from_json(const nlohmann::json& j, SingleRange& o);

	struct RangeSet {
		std::map<std::string, SingleRange> ranges;
	};
	void to_json(nlohmann::json& j, const RangeSet& o);
	void from_json(const nlohmann::json& j, RangeSet& o);

	struct SingleTargetComponent {
		std::string name;
		std::vector<std::string> ranges;
		bool replace;
		bool extendRange;
	};
	void to_json(nlohmann::json& j, const SingleTargetComponent& o);
	void from_json(const nlohmann::json& j, SingleTargetComponent& o);

	struct SingleFontTarget {
		double height;

		uint8_t ascent;
		std::string ascentFrom;
		bool autoAscent;

		uint8_t lineHeight;
		std::string lineHeightFrom;
		bool autoLineHeight;

		uint8_t maxGlobalOffsetX;
		uint8_t minGlobalOffsetX;
		uint8_t globalOffsetY;

		uint8_t borderThickness;
		uint8_t borderOpacity;

		std::u32string charactersToKernAcrossFonts;
		bool alignToBaseline;
		std::vector<SingleTargetComponent> sources;
	};
	void to_json(nlohmann::json& j, const SingleFontTarget& o);
	void from_json(const nlohmann::json& j, SingleFontTarget& o);

	struct SingleTextureTarget {
		std::map<std::string, SingleFontTarget> fontTargets;
	};
	void to_json(nlohmann::json& j, const SingleTextureTarget& o);
	void from_json(const nlohmann::json& j, SingleTextureTarget& o);

	struct FontCreateConfig {
		uint16_t glyphGap;
		uint16_t textureWidth;
		uint16_t textureHeight;
		Texture::CompressionType textureType;
		std::map<std::string, InputFontSource> sources;
		std::map<std::string, RangeSet> ranges;
		std::map<std::string, SingleTextureTarget> targets;
	};
	void to_json(nlohmann::json& j, const FontCreateConfig& o);
	void from_json(const nlohmann::json& j, FontCreateConfig& o);
}

void to_json(nlohmann::json& j, const DWRITE_RENDERING_MODE& o);
void from_json(const nlohmann::json& j, DWRITE_RENDERING_MODE& o);

void to_json(nlohmann::json& j, const DWRITE_FONT_STYLE& o);
void from_json(const nlohmann::json& j, DWRITE_FONT_STYLE& o);
void to_json(nlohmann::json& j, const DWRITE_FONT_STRETCH& o);
void from_json(const nlohmann::json& j, DWRITE_FONT_STRETCH& o);
