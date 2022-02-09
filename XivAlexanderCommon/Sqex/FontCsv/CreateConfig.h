#pragma once

#include <dwrite_3.h>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "XivAlexanderCommon/Sqex/Texture.h"

namespace Sqex::FontCsv::CreateConfig {
	struct GameIndexFile {
		std::vector<std::filesystem::path> pathList;
		GameReleaseRegion autoDetectRegion = GameReleaseRegion::Unspecified;
		std::string autoDetectIndexExpac;
		std::string autoDetectIndexFile;
		std::vector<std::filesystem::path> fallbackPathList;
		std::vector<std::pair<std::string, std::string>> fallbackPrompt;
	};
	void to_json(nlohmann::json& j, const GameIndexFile& o);
	void from_json(const nlohmann::json& j, GameIndexFile& o);
	
	struct FontRequirement {
		std::string name;
		std::string homepage;
		std::vector<std::pair<std::string, std::string>> installInstructions;
	};
	void to_json(nlohmann::json& j, const FontRequirement& o);
	void from_json(const nlohmann::json& j, FontRequirement& o);

	struct GameSource {
		std::filesystem::path indexFile;
		std::string gameIndexFileName;
		std::filesystem::path fdtPath;
		std::filesystem::path texturePath;
		int advanceWidthDelta{};
		std::optional<int> leftSideBearing{};
	};
	void to_json(nlohmann::json& j, const GameSource& o);
	void from_json(const nlohmann::json& j, GameSource& o);

	struct DirectWriteSource {
		std::filesystem::path fontFile;
		uint32_t faceIndex{};
		std::string familyName;
		double height{};
		int weight{};
		bool measureUsingFreeType{};
		DWRITE_RENDERING_MODE renderMode = DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC;
		DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL;
		DWRITE_FONT_STRETCH stretch = DWRITE_FONT_STRETCH_NORMAL;
		int advanceWidthDelta{};
		int oversampleScale = 1;
	};
	void to_json(nlohmann::json& j, const DirectWriteSource& o);
	void from_json(const nlohmann::json& j, DirectWriteSource& o);

	struct FreeTypeSource {
		std::filesystem::path fontFile;
		uint32_t faceIndex{};
		std::string familyName;
		double height{};
		int weight{};
		DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL;
		DWRITE_FONT_STRETCH stretch = DWRITE_FONT_STRETCH_NORMAL;
		uint32_t loadFlags{};
		int advanceWidthDelta{};
		int oversampleScale = 1;
	};
	void to_json(nlohmann::json& j, const FreeTypeSource& o);
	void from_json(const nlohmann::json& j, FreeTypeSource& o);

	struct GdiSource : LOGFONTW {
		int advanceWidthDelta{};
		int oversampleScale = 1;
	};
	void to_json(nlohmann::json& j, const GdiSource& o);
	void from_json(const nlohmann::json& j, GdiSource& o);

	struct InputFontSource {
		std::optional<GameSource> gameSource;
		std::optional<DirectWriteSource> directWriteSource;
		std::optional<GdiSource> gdiSource;
		std::optional<FreeTypeSource> freeTypeSource;
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

	enum class VerticalAlignment {
		Baseline,
		Top,
		Middle,
		Bottom,
	};
	void to_json(nlohmann::json& j, const VerticalAlignment& o);
	void from_json(const nlohmann::json& j, VerticalAlignment& o);

	struct SingleTargetComponent {
		std::string name;
		std::vector<std::string> ranges;
		bool replace{};
		bool extendRange{};
		int offsetXModifier;
		int offsetYModifier;
		VerticalAlignment alignment{ VerticalAlignment::Baseline };
	};
	void to_json(nlohmann::json& j, const SingleTargetComponent& o);
	void from_json(const nlohmann::json& j, SingleTargetComponent& o);

	struct SingleFontTarget {
		static constexpr int CompactLayout_NoOverride = 0;
		static constexpr int CompactLayout_Override_Enable = 1;
		static constexpr int CompactLayout_Override_Disable = 2;

		double height{};

		std::string ascentFrom;
		std::string lineHeightFrom;

		uint8_t ascent{};
		bool autoAscent{};
		uint8_t lineHeight{};
		bool autoLineHeight{};

		uint8_t maxGlobalOffsetX{};
		uint8_t minGlobalOffsetX{};
		uint8_t globalOffsetY{};
		int compactLayout{};

		uint8_t borderThickness{};
		uint8_t borderOpacity{};

		std::u32string charactersToKernAcrossFonts;
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
		uint16_t glyphGap{};
		bool compactLayout{};
		uint16_t textureWidth{};
		uint16_t textureHeight{};
		Texture::Format textureFormat{};
		std::map<std::string, GameIndexFile> gameIndexFiles;
		std::vector<FontRequirement> fontRequirements;
		std::map<std::string, InputFontSource> sources;
		std::map<std::string, RangeSet> ranges;
		std::map<std::string, SingleTextureTarget> targets;

		void ValidateOrThrow() const;
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
