#pragma once

#include <nlohmann/json.hpp>

#include "BaseConfigRepository.h"

namespace XivAlexander {
	/// A system font by English family name and DWRITE_* weight, stretch and style, as FFXIV-FontChanger's plugin has it (FamilyFont).
	struct FontReplacementFamilyFont {
		std::string FamilyName;
		int Weight = 400;
		int Stretch = 5;
		int Style = 0;

		bool operator==(const FontReplacementFamilyFont&) const = default;
	};

	/// Changes to the fonts made from a system font, as FFXIV-FontChanger's plugin has them (FontAdjustments): sizes are scaled from the fitted
	/// ones, and moves and spacing are in fractions of the size of the font made.
	struct FontReplacementAdjustments {
		/// Flags of what the font draws: 1 non-CJK, 2 Han, 4 kana, 8 Hangul, 16 the game's symbols.
		uint32_t Scripts = 31;

		/// How much larger the glyphs of scripts other than CJK, and of CJK scripts, are drawn than fitted: 0.05 is 5% larger.
		float NonCjkSize = 0;
		float CjkSize = 0;

		/// How far up the glyphs are moved, over the size; down if negative.
		float Raise = 0;

		/// Letter spacing of scripts other than CJK, and of CJK scripts, over the size.
		float NonCjkSpacing = 0;
		float CjkSpacing = 0;

		/// 0 lighter, 1 normal, 2 heavier, 3 heaviest.
		int StrokeWeight = 1;

		bool operator==(const FontReplacementAdjustments&) const = default;
	};

	/// An FFXIV-FontChanger preset, or a system font made into fonts as its FontsFromSystemFont does.
	struct FontReplacementFamilySource {
		bool Enabled = true;

		/// Relative to <configuration folder>\FontPresets if imported there, else absolute; empty for a system font.
		std::string Preset;

		FontReplacementFamilyFont Font;

		bool MonospacedDigits = true;

		FontReplacementAdjustments Adjustments;

		[[nodiscard]] bool IsPreset() const { return !Preset.empty(); }

		bool operator==(const FontReplacementFamilySource&) const = default;
	};

	/// How a game font family (AXIS, JupiterN, ...) is replaced.
	struct FontReplacementFamily {
		/// If false, the game's own font is kept, as for a family without sources.
		bool Enabled = true;

		/// In order: of a preset only the family's fonts are used, and later sources draw over earlier ones glyph by glyph.
		std::vector<FontReplacementFamilySource> Sources;

		bool operator==(const FontReplacementFamily&) const = default;
	};

	/// Edge outline width at text size px: clamp(Scale * px, Min, Max) pixels; the defaults (1 px at every size) are the game's own.
	struct FontReplacementEdgeConfig {
		/// If false, the game's own edge is drawn, as with the defaults.
		bool Enabled = false;

		/// Width per pixel of text size (0: Min at every size).
		float Scale = 0;
		float Min = 1;
		float Max = 1;

		/// 0 is solid with a 1 px rim; up to 1, solid then a linear falloff; above 1, a falloff from the glyph, k * width wide (8 px at most).
		float Softness = 0;

		[[nodiscard]] float GetWidth(float px) const { return std::clamp(Scale * px, Min, Max); }

		/// How far the edge reaches past the glyph, as the edge shader keeps it.
		[[nodiscard]] float GetReach(float px) const { return (std::min)(GetWidth(px) * (std::max)(Softness, 1.f), 8.f); }

		/// Within what the edge can do: 0.25 to 8 px, softness 0 to 8.
		[[nodiscard]] FontReplacementEdgeConfig Clamped() const {
			const auto min = std::clamp(Min, 0.25f, 8.f);
			return {Enabled, std::clamp(Scale, 0.f, 1.f), min, std::clamp(Max, min, 8.f), std::clamp(Softness, 0.f, 8.f)};
		}

		/// As drawn: clamped if enabled, else the defaults.
		[[nodiscard]] FontReplacementEdgeConfig Drawn() const {
			return Enabled ? Clamped() : FontReplacementEdgeConfig{};
		}

		bool operator==(const FontReplacementEdgeConfig&) const = default;
	};

	/// Glyph atlases, one per game font family: side length of a page in texels and how many pages one may take, for AXIS (most of the UI's text)
	/// and for the other families. Pages are made as glyphs need them; a 4096 page takes 64 MB of video memory, a 2048 one 16 MB.
	/// B4G4R4A4 halves the pages' memory at 16 coverage levels; used only where the game's device can sample it.
	enum class FontReplacementAtlasTextureFormat {
		Bgra8888,
		Bgra4444,
	};

	struct FontReplacementAtlasConfig {
		int AxisPageWidth = 4096;
		int AxisPageHeight = 4096;
		int AxisMaxPages = 4;
		int OtherPageWidth = 2048;
		int OtherPageHeight = 2048;
		int OtherMaxPages = 2;
		FontReplacementAtlasTextureFormat Format = FontReplacementAtlasTextureFormat::Bgra8888;

		/// Sides 256 to 4096 (glyph positions take 12 bits), 1 to 64 pages (texture indices take 6 bits); the game's texture slots may allow fewer.
		[[nodiscard]] FontReplacementAtlasConfig Clamped() const {
			return {
				std::clamp(AxisPageWidth, 256, 4096),
				std::clamp(AxisPageHeight, 256, 4096),
				std::clamp(AxisMaxPages, 1, 64),
				std::clamp(OtherPageWidth, 256, 4096),
				std::clamp(OtherPageHeight, 256, 4096),
				std::clamp(OtherMaxPages, 1, 64),
				Format == FontReplacementAtlasTextureFormat::Bgra4444 ? Format : FontReplacementAtlasTextureFormat::Bgra8888,
			};
		}

		bool operator==(const FontReplacementAtlasConfig&) const = default;
	};

	/// ClearType level, enhanced contrast and gamma of RGB and BGR subpixel text; System takes Windows' for the game's monitor (the ClearType Text Tuner's).
	struct FontReplacementClearTypeConfig {
		float Level = 1;
		float Contrast = 0.5f;
		float Gamma = 1.8f;

		/// Level 0 to 1, contrast 0 to 4, gamma 1 to 3.
		[[nodiscard]] FontReplacementClearTypeConfig Clamped() const {
			return {std::clamp(Level, 0.f, 1.f), std::clamp(Contrast, 0.f, 4.f), std::clamp(Gamma, 1.f, 2.2f)};
		}

		bool operator==(const FontReplacementClearTypeConfig&) const = default;
	};

	/// Subpixel text; System follows Windows' ClearType setting for the game's monitor. The vertical ones stack the stripes top to bottom, which Windows
	/// doesn't know of: a monitor turned to portrait, or a portrait panel turned to landscape.
	enum class FontReplacementSubpixelMode : uint8_t {
		Off,
		System,
		Rgb,
		Bgr,
		VerticalRgb,
		VerticalBgr,
	};

	/// What draws fonts set to DirectWrite for vertical subpixel layouts, for which DirectWrite has no ClearType of its own: FreeType's LCD_V rendering,
	/// hinted as their grid fitting says, or DirectWrite's ClearType of the text turned a quarter and turned back, without hinting.
	enum class FontReplacementVerticalSubpixelRenderer : uint8_t {
		FreeType,
		DirectWrite,
	};

	enum class FontReplacementNamePlateMode : uint8_t {
		Game,
		BakedAtFullSize,
		Live,
	};

	class FontReplacementConfigGroup : public ConfigGroup {
	public:
		using ConfigGroup::ConfigGroup;

		ConfigItem<bool> Enabled{this, "Enabled", false};

		class FontsGroup : public ConfigGroup {
		public:
			using ConfigGroup::ConfigGroup;
			ConfigItem<std::map<std::string, FontReplacementFamily>> Families{this, "Families"};
			ConfigItem<bool> SystemFallback{this, "SystemFallback", true};
		} Fonts{this, "Fonts"};

		ConfigItem<FontReplacementEdgeConfig> Edge{this, "Edge", {}, [](const FontReplacementEdgeConfig& v) { return v.Clamped(); }};
		ConfigItem<FontReplacementSubpixelMode> Subpixel{this, "Subpixel", FontReplacementSubpixelMode::Off};
		ConfigItem<FontReplacementClearTypeConfig> ClearType{this, "ClearType", {}, [](const FontReplacementClearTypeConfig& v) { return v.Clamped(); }};
		ConfigItem<FontReplacementVerticalSubpixelRenderer> VerticalSubpixel{this, "VerticalSubpixel", FontReplacementVerticalSubpixelRenderer::FreeType};
		ConfigItem<FontReplacementNamePlateMode> NamePlates{this, "NamePlates", FontReplacementNamePlateMode::BakedAtFullSize};
		ConfigItem<FontReplacementAtlasConfig> Atlas{this, "Atlas", {}, [](const FontReplacementAtlasConfig& v) { return v.Clamped(); }};
	};

	void to_json(nlohmann::json&, const FontReplacementFamilyFont&);
	void from_json(const nlohmann::json&, FontReplacementFamilyFont&);
	void to_json(nlohmann::json&, const FontReplacementAdjustments&);
	void from_json(const nlohmann::json&, FontReplacementAdjustments&);
	void to_json(nlohmann::json&, const FontReplacementFamilySource&);
	void from_json(const nlohmann::json&, FontReplacementFamilySource&);
	void to_json(nlohmann::json&, const FontReplacementFamily&);
	void from_json(const nlohmann::json&, FontReplacementFamily&);
	void to_json(nlohmann::json&, const FontReplacementEdgeConfig&);
	void from_json(const nlohmann::json&, FontReplacementEdgeConfig&);
	void to_json(nlohmann::json&, const FontReplacementAtlasConfig&);
	void from_json(const nlohmann::json&, FontReplacementAtlasConfig&);
	void to_json(nlohmann::json&, const FontReplacementClearTypeConfig&);
	void from_json(const nlohmann::json&, FontReplacementClearTypeConfig&);
	void to_json(nlohmann::json&, const FontReplacementSubpixelMode&);
	void from_json(const nlohmann::json&, FontReplacementSubpixelMode&);
	void to_json(nlohmann::json&, const FontReplacementVerticalSubpixelRenderer&);
	void from_json(const nlohmann::json&, FontReplacementVerticalSubpixelRenderer&);
	void to_json(nlohmann::json&, const FontReplacementNamePlateMode&);
	void from_json(const nlohmann::json&, FontReplacementNamePlateMode&);
}
