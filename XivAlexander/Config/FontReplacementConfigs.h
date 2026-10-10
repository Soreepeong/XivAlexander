#pragma once

#include <nlohmann/json.hpp>

#include "BaseConfigRepository.h"

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

	/// A source of a game font family's faces: a preset of FFXIV-FontChanger's, or a system font made into faces as its
	/// FaceFromFont makes them.
	struct FontReplacementFamilySource {
		bool Enabled = true;

		/// The preset: a path relative to the presets folder (<configuration folder>\FontPresets) for one imported there, or an
		/// absolute path for one used where it is; empty for a system font.
		std::string Preset;

		/// The system font, if there is no preset.
		FontReplacementFamilyFont Font;

		[[nodiscard]] bool IsPreset() const { return !Preset.empty(); }

		bool operator==(const FontReplacementFamilySource&) const = default;
	};

	/// How a game font family (AXIS, JupiterN, ...) is replaced.
	struct FontReplacementFamily {
		/// Whether the family is replaced at all; if not, the game's own font is kept, as for a family without sources.
		bool Enabled = true;

		/// Whether a system font's digits are made monospaced: with its tabular figures (tnum) if it has them, else by putting
		/// each in a cell as wide as its 0.
		bool MonospacedDigits = true;

		/// The sources, in order: of a preset, only the family's faces are used, and of a face several give, the last one's.
		std::vector<FontReplacementFamilySource> Sources;

		bool operator==(const FontReplacementFamily&) const = default;
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

	/// The font replacement's settings, saved as an object: draws the game's text with fonts of FFXIV-FontChanger's presets,
	/// or of system fonts, rasterized at the size drawn.
	class FontReplacementConfigGroup : public ConfigGroup {
	public:
		using ConfigGroup::ConfigGroup;

		ConfigItem<bool> Enabled{this, "Enabled", false};

		/// What the faces are made from; a change reloads them.
		class FacesGroup : public ConfigGroup {
		public:
			using ConfigGroup::ConfigGroup;

			/// The settings per game font family. A family that isn't here, is turned off, or has no sources (and a face its
			/// sources lack) uses the game's glyphs, with SystemFallback for the characters they lack.
			ConfigItem<std::map<std::string, FontReplacementFamily>> Families{this, "Families"};

			/// Whether characters the presets lack are drawn with Windows' fallback fonts, instead of the game's.
			ConfigItem<bool> SystemFallback{this, "SystemFallback", true};
		} Faces{this, "Faces"};

		ConfigItem<FontReplacementEdgeConfig> Edge{this, "Edge", {}, [](const FontReplacementEdgeConfig& v) { return v.Clamped(); }};
	};

	void to_json(nlohmann::json&, const FontReplacementFamilyFont&);
	void from_json(const nlohmann::json&, FontReplacementFamilyFont&);
	void to_json(nlohmann::json&, const FontReplacementFamilySource&);
	void from_json(const nlohmann::json&, FontReplacementFamilySource&);
	void to_json(nlohmann::json&, const FontReplacementFamily&);
	void from_json(const nlohmann::json&, FontReplacementFamily&);
	void to_json(nlohmann::json&, const FontReplacementEdgeConfig&);
	void from_json(const nlohmann::json&, FontReplacementEdgeConfig&);
}
