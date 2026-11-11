#pragma once

#include "Config/FontReplacementConfigs.h"
#include "MainApp/FontReplacement/Fonts/GlyphAtlas.h"
#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Each game font family gets an atlas of its own, so that one family's text can't push another's out. AXIS, the family of most of the
	// UI's text, has its own page size and count.
	class FamilyAtlases {
		std::function<bool()> m_supports4444;
		std::map<std::string, std::shared_ptr<GlyphAtlas>, LessIgnoringCase> m_atlases;
		FontReplacementAtlasConfig m_settings = FontReplacementAtlasConfig{}.Clamped();
		bool m_subpixel = false;

	public:
		explicit FamilyAtlases(std::function<bool()> supports4444)
			: m_supports4444(std::move(supports4444)) {}

		FamilyAtlases(const FamilyAtlases&) = delete;
		FamilyAtlases& operator=(const FamilyAtlases&) = delete;

		// What atlases made from now on are made with.
		[[nodiscard]] const FontReplacementAtlasConfig& Settings() const { return m_settings; }
		void SetSettings(const FontReplacementAtlasConfig& value) { m_settings = value; }

		// Atlases made from now on take subpixel coverage if set.
		void SetSubpixel(bool value) { m_subpixel = value; }

		[[nodiscard]] auto All() const { return m_atlases | std::views::values | std::views::transform([](const auto& a) { return a.get(); }); }

		// Made on its first use.
		std::shared_ptr<GlyphAtlas> Get(const std::string& family);

		void Upload();

		// Keeps the pages, which copies refer to.
		void ClearAll();

		// The next Get makes new ones. Copies still referring to the old ones find them without pages.
		void Dispose();
	};
}
