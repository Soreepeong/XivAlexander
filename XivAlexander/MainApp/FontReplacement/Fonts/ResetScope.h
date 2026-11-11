#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// How much is made again. Each includes what it is made from, but a font swap keeps the atlases and new atlases keep the fonts.
	enum class ResetScope : uint32_t {
		None = 0,

		// Only what keeps text laid out (nameplates) lays it out again.
		Text = 1,

		Glyphs = Text | 2,

		// Also everything made from the game's fonts.
		SizedFonts = Glyphs | 4,

		Atlases = SizedFonts | 8,

		Fonts = SizedFonts | 16,
	};

	[[nodiscard]] constexpr ResetScope operator|(ResetScope a, ResetScope b) {
		return static_cast<ResetScope>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}

	constexpr ResetScope& operator|=(ResetScope& a, ResetScope b) {
		return a = a | b;
	}

	// Whether scope includes all of what part does.
	[[nodiscard]] constexpr bool Includes(ResetScope scope, ResetScope part) {
		return (static_cast<uint32_t>(scope) & static_cast<uint32_t>(part)) == static_cast<uint32_t>(part);
	}
}
