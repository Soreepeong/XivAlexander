#pragma once

#include <string>
#include <map>
#include <vector>
#include <dwrite_3.h>

namespace Sqex::FontCsv::CreateConfig {
	struct GameSource {
		std::string indexFile;
		std::string fdtPath;
		std::string texturePath;
		int textureCount;
	};
	void to_json(nlohmann::json& j, const GameSource& o);
	void from_json(const nlohmann::json& j, GameSource& o);

	struct DirectWriteSource {
		std::string familyName;
		double height;
		int weight;
		DWRITE_RENDERING_MODE renderMode;
		DWRITE_FONT_STYLE style;
	};
	void to_json(nlohmann::json& j, const DirectWriteSource& o);
	void from_json(const nlohmann::json& j, DirectWriteSource& o);

	struct GdiSource : LOGFONTW {
	};
	void to_json(nlohmann::json& j, const GdiSource& o);
	void from_json(const nlohmann::json& j, GdiSource& o);

	struct InputFontSource {
		GameSource gameSource;
		DirectWriteSource directWriteSource;
		GdiSource gdiSource;
	};
	void to_json(nlohmann::json& j, const InputFontSource& o);
	void from_json(const nlohmann::json& j, InputFontSource& o);

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
	};
	void to_json(nlohmann::json& j, const SingleTargetComponent& o);
	void from_json(const nlohmann::json& j, SingleTargetComponent& o);

	struct SingleFontTarget {
		double height;
		uint8_t ascent;
		uint8_t descent;
		uint8_t maxGlobalOffsetX;
		uint8_t globalOffsetY;
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
		std::map<std::string, InputFontSource> sources;
		std::map<std::string, RangeSet> ranges;
		std::map<std::string, SingleTextureTarget> targets;
	};
	void to_json(nlohmann::json& j, const FontCreateConfig& o);
	void from_json(const nlohmann::json& j, FontCreateConfig& o);
}
