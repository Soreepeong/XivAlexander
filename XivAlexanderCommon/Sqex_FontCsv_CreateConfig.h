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

	struct DirectWriteSource {
		std::string familyName;
		double height;
		int weight;
		DWRITE_RENDERING_MODE renderMode;
		DWRITE_FONT_STYLE style;
	};

	struct GdiSource : LOGFONTW {
	};

	struct SingleRange {
		char32_t from;
		char32_t to;
	};

	struct SingleTarget {
		struct Source {
			std::string name;
			std::vector<std::string> ranges;
			bool replace;
		};

		double height;
		uint8_t ascent;
		uint8_t descent;
		uint8_t maxGlobalOffsetX;
		uint8_t globalOffsetY;
		std::u32string charactersToKernAcrossFonts;
		bool alignToBaseline;
	};

	struct FontCreateConfig {
		std::map<std::string, GameSource> sources;
		std::map<std::string, SingleRange> ranges;
		std::map<std::string, std::map<std::string, SingleTarget>> targets;
	};
}
