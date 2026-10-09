#pragma once

#include <nlohmann/json.hpp>

namespace XivAlexander {
	/// A system font a game font family's faces are drawn with: its family's name (English) and the face's weight, stretch and
	/// style (DWRITE_* values). As FFXIV-FontChanger's plugin has it (FamilyFont).
	struct FontReplacementFamilyFont {
		std::string Name;
		int Weight = 400;
		int Stretch = 5;
		int Style = 0;

		bool operator==(const FontReplacementFamilyFont&) const = default;
	};

	/// The edge outline's width at a text size px: clamp(Scale * px, Min, Max) pixels. The defaults are the game's own edge,
	/// 1 px at every size.
	struct FontReplacementEdgeConfig {
		/// The width per pixel of text size (0: Min at every size).
		float Scale = 0;
		float Min = 1;
		float Max = 1;

		[[nodiscard]] float GetWidth(float px) const { return std::clamp(Scale * px, Min, Max); }

		/// Gets the settings within what the edge can do: 0.25 to 8 px.
		[[nodiscard]] FontReplacementEdgeConfig Clamped() const {
			const auto min = std::clamp(Min, 0.25f, 8.f);
			return {std::clamp(Scale, 0.f, 1.f), min, std::clamp(Max, min, 8.f)};
		}

		bool operator==(const FontReplacementEdgeConfig&) const = default;
	};

	/// How nameplate text is drawn.
	enum class FontReplacementNamePlateMode : uint8_t {
		/// As the game does: baked once at the text node's unscaled size, drawn scaled (soft as plates zoom).
		Game,

		/// Baked once at the size the plate has up close, drawn scaled down from there: 1:1 at full size.
		BakedAtFullSize,

		/// Laid out and drawn every frame at the size shown: always sharp, but costly with many plates.
		Live,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(FontReplacementNamePlateMode, {
		{FontReplacementNamePlateMode::BakedAtFullSize, "BakedAtFullSize"},
		{FontReplacementNamePlateMode::Game, "Game"},
		{FontReplacementNamePlateMode::Live, "Live"},
	})

	/// The font replacement's settings: draws the game's text with fonts of FFXIV-FontChanger's presets, or of system fonts,
	/// rasterized at the size drawn.
	struct FontReplacementConfig {
		bool Enabled = false;

		/// The folder presets are chosen from.
		std::filesystem::path PresetFolder;

		/// The presets in use per game font family (AXIS, JupiterN, ...), as paths relative to the preset folder, in the order
		/// they were selected: of the family's faces, only those are used, and of a face in several, the last one's. A family
		/// without any (and a face its presets lack) uses the game's glyphs, with SystemFallback for the characters they lack.
		std::map<std::string, std::vector<std::string>> FamilyPresets;

		/// The system font per game font family to draw its faces with, made as FFXIV-FontChanger's FaceFromFont makes them;
		/// over the family's presets.
		std::map<std::string, FontReplacementFamilyFont> FamilyFonts;

		/// Whether a system font's digits are made monospaced: with its tabular figures (tnum) if it has them, else by
		/// putting each in a cell as wide as its 0.
		bool MonospacedDigits = true;

		/// Whether characters the presets lack are drawn with Windows' fallback fonts, instead of the game's.
		bool SystemFallback = true;

		FontReplacementEdgeConfig Edge;
		FontReplacementNamePlateMode NamePlateMode = FontReplacementNamePlateMode::BakedAtFullSize;

		[[nodiscard]] FontReplacementConfig Sanitized() const {
			auto res = *this;
			res.Edge = Edge.Clamped();
			return res;
		}

		/// Gets whether the faces are made from different settings: the presets, their families' fonts and how.
		[[nodiscard]] bool FacesDiffer(const FontReplacementConfig& r) const {
			return PresetFolder != r.PresetFolder || FamilyPresets != r.FamilyPresets || FamilyFonts != r.FamilyFonts
				|| MonospacedDigits != r.MonospacedDigits || SystemFallback != r.SystemFallback;
		}

		bool operator==(const FontReplacementConfig&) const = default;
	};

	void to_json(nlohmann::json&, const FontReplacementFamilyFont&);
	void from_json(const nlohmann::json&, FontReplacementFamilyFont&);
	void to_json(nlohmann::json&, const FontReplacementEdgeConfig&);
	void from_json(const nlohmann::json&, FontReplacementEdgeConfig&);
	void to_json(nlohmann::json&, const FontReplacementConfig&);
	void from_json(const nlohmann::json&, FontReplacementConfig&);
}
