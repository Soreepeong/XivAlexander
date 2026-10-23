#pragma once

#include <nlohmann/json.hpp>

#include "BaseConfigRepository.h"

namespace XivAlexander {
	// Weight, Stretch and Style are DWRITE_FONT_* values.
	struct FontReplacementFamilyFont {
		std::string FamilyName;
		int Weight = 400;
		int Stretch = 5;
		int Style = 0;

		bool operator==(const FontReplacementFamilyFont&) const = default;
	};

	// Sizes are scaled from the fitted ones (0.05 is 5% larger), and Raise and the spacings are fractions of the size of the font made.
	struct FontReplacementAdjustments {
		// Flags: 1 non-CJK, 2 Han, 4 kana, 8 Hangul, 16 the game's symbols.
		uint32_t Scripts = 31;

		float NonCjkSize = 0;
		float CjkSize = 0;
		float Raise = 0;
		float NonCjkSpacing = 0;
		float CjkSpacing = 0;

		// 0 lighter, 1 normal, 2 heavier, 3 heaviest.
		int StrokeWeight = 1;

		bool operator==(const FontReplacementAdjustments&) const = default;
	};

	struct FontReplacementFamilySource {
		bool Enabled = true;

		// Relative to <configuration folder>\FontPresets if imported there, else absolute; empty for a system font.
		std::string Preset;

		FontReplacementFamilyFont Font;

		bool MonospacedDigits = true;

		FontReplacementAdjustments Adjustments;

		[[nodiscard]] bool IsPreset() const { return !Preset.empty(); }

		bool operator==(const FontReplacementFamilySource&) const = default;
	};

	struct FontReplacementFamily {
		bool Enabled = true;

		// Later sources draw over earlier ones glyph by glyph.
		std::vector<FontReplacementFamilySource> Sources;

		bool operator==(const FontReplacementFamily&) const = default;
	};

	// The defaults, 1 px at every size, are the game's own edge.
	struct FontReplacementEdgeConfig {
		bool Enabled = false;

		// Width per pixel of text size.
		float Scale = 0;
		float Min = 1;
		float Max = 1;

		// 0 is solid with a 1 px rim; up to 1, solid then a linear falloff; above 1, a falloff from the glyph, k * width wide (8 px at most).
		float Softness = 0;

		[[nodiscard]] float GetWidth(float px) const { return std::clamp(Scale * px, Min, Max); }

		// The edge shader reaches at most 8 px past the glyph.
		[[nodiscard]] float GetReach(float px) const { return (std::min)(GetWidth(px) * (std::max)(Softness, 1.f), 8.f); }

		[[nodiscard]] FontReplacementEdgeConfig Clamped() const {
			const auto min = std::clamp(Min, 0.25f, 8.f);
			return {Enabled, std::clamp(Scale, 0.f, 1.f), min, std::clamp(Max, min, 8.f), std::clamp(Softness, 0.f, 8.f)};
		}

		[[nodiscard]] FontReplacementEdgeConfig Drawn() const {
			return Enabled ? Clamped() : FontReplacementEdgeConfig{};
		}

		bool operator==(const FontReplacementEdgeConfig&) const = default;
	};

	// B4G4R4A4 halves the pages' memory at 16 coverage levels, and is used only where the game's device can sample it.
	enum class FontReplacementAtlasTextureFormat {
		Bgra8888,
		Bgra4444,
	};

	// AXIS gets its own limits, as it draws most of the UI's text.
	struct FontReplacementAtlasConfig {
		int AxisPageWidth = 4096;
		int AxisPageHeight = 4096;
		int AxisMaxPages = 4;
		int OtherPageWidth = 2048;
		int OtherPageHeight = 2048;
		int OtherMaxPages = 2;
		FontReplacementAtlasTextureFormat Format = FontReplacementAtlasTextureFormat::Bgra8888;

		// Glyph positions take 12 bits and texture indices 6 bits; the game's texture slots may allow fewer pages.
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

	// Used for RGB and BGR subpixel text unless the mode is System, which takes Windows' values for the game's monitor.
	struct FontReplacementClearTypeConfig {
		float Level = 1;
		float Contrast = 0.5f;
		float Gamma = 1.8f;
		float GrayscaleContrast = 1;

		[[nodiscard]] FontReplacementClearTypeConfig Clamped() const {
			return {std::clamp(Level, 0.f, 1.f), std::clamp(Contrast, 0.f, 4.f), std::clamp(Gamma, 1.f, 2.2f), std::clamp(GrayscaleContrast, 0.f, 4.f)};
		}

		bool operator==(const FontReplacementClearTypeConfig&) const = default;
	};

	// The vertical modes, which Windows doesn't know of, are for monitors turned between portrait and landscape.
	enum class FontReplacementSubpixelMode : uint8_t {
		Off,
		System,
		Rgb,
		Bgr,
		VerticalRgb,
		VerticalBgr,
	};

	// DirectWrite has no vertical ClearType, so vertical layouts use FreeType's hinted LCD_V or unhinted ClearType of glyphs turned a quarter.
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
