#pragma once

#include <nlohmann/json.hpp>

#include "BaseConfigRepository.h"

namespace XivAlexander {
	/// A system font by English family name and DWRITE_* weight, stretch and style, as FFXIV-FontChanger's plugin has it (FamilyFont).
	struct FontReplacementFamilyFont {
		std::string Name;
		int Weight = 400;
		int Stretch = 5;
		int Style = 0;

		bool operator==(const FontReplacementFamilyFont&) const = default;
	};

	/// An FFXIV-FontChanger preset, or a system font made into faces as its FaceFromFont does.
	struct FontReplacementFamilySource {
		bool Enabled = true;

		/// Relative to <configuration folder>\FontPresets if imported there, else absolute; empty for a system font.
		std::string Preset;

		FontReplacementFamilyFont Font;

		[[nodiscard]] bool IsPreset() const { return !Preset.empty(); }

		bool operator==(const FontReplacementFamilySource&) const = default;
	};

	/// How a game font family (AXIS, JupiterN, ...) is replaced.
	struct FontReplacementFamily {
		/// If false, the game's own font is kept, as for a family without sources.
		bool Enabled = true;

		/// Uses the system font's tabular figures (tnum) if it has them, else puts each digit in a cell as wide as its 0.
		bool MonospacedDigits = true;

		/// In order: of a preset only the family's faces are used, and of a face several give, the last one's.
		std::vector<FontReplacementFamilySource> Sources;

		bool operator==(const FontReplacementFamily&) const = default;
	};

	/// Edge outline width at text size px: clamp(Scale * px, Min, Max) pixels; the defaults (1 px at every size) are the game's own.
	struct FontReplacementEdgeConfig {
		/// Width per pixel of text size (0: Min at every size).
		float Scale = 0;
		float Min = 1;
		float Max = 1;

		[[nodiscard]] float GetWidth(float px) const { return std::clamp(Scale * px, Min, Max); }

		/// Within what the edge can do: 0.25 to 8 px.
		[[nodiscard]] FontReplacementEdgeConfig Clamped() const {
			const auto min = std::clamp(Min, 0.25f, 8.f);
			return {std::clamp(Scale, 0.f, 1.f), min, std::clamp(Max, min, 8.f)};
		}

		bool operator==(const FontReplacementEdgeConfig&) const = default;
	};

	/// Saved as one object.
	class FontReplacementConfigGroup : public ConfigGroup {
	public:
		using ConfigGroup::ConfigGroup;

		ConfigItem<bool> Enabled{this, "Enabled", false};

		/// What the faces are made from; a change reloads them.
		class FacesGroup : public ConfigGroup {
		public:
			using ConfigGroup::ConfigGroup;

			/// Families missing here, off, or without sources (and faces their sources lack) use the game's glyphs, with SystemFallback for the rest.
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
