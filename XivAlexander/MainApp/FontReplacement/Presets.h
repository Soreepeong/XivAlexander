#pragma once

#include <FontChanger.Presets/Structs.h>

#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace XivAlexander {
	class Config;
}

// Fonts by game font name (FontChanger.Presets' Preset), read with FontChanger.Presets.Native's model.
namespace XivAlexander::Apps::MainApp::FontReplacement::Presets {
	using Fonts = std::map<std::string, std::shared_ptr<const FontChanger::Structs::Font>, LessIgnoringCase>;

	// <configuration folder>\FontPresets, created if missing; a source's preset is relative to it, or absolute if used in place.
	[[nodiscard]] std::filesystem::path Folder(Config& config);

	[[nodiscard]] std::filesystem::path Resolve(const std::filesystem::path& folder, const std::string& preset);

	// As the preset names them, since those are what importing the preset alone would leave behind.
	[[nodiscard]] std::vector<std::filesystem::path> RelativeGlyphImageFolders(const std::filesystem::path& path);

	// Strictly under the preset's folder (not the folder itself or above), so that it can be copied along with the preset.
	[[nodiscard]] bool IsCopyableGlyphImageFolder(const std::filesystem::path& relative);

	// Returns the path relative to folder; a preset with glyph image folders (all copyable) gets its own folder holding copies of them.
	std::filesystem::path Import(const std::filesystem::path& path, const std::filesystem::path& folder, const std::vector<std::filesystem::path>& glyphImageFolders);

	// One font set (fonts at the top) or several (fontSets), the first winning per name; relative glyph image folders are made absolute.
	[[nodiscard]] Fonts Load(const std::filesystem::path& path);

	// For a font in several, the last preset's wins.
	void Combine(Fonts& into, const Fonts& from);

	// A font in both gets from's elements after into's, drawn over them glyph by glyph; sizes scaled to into's first element.
	void Overlay(Fonts& into, const Fonts& from);

	// The game font's family (AXIS of AXIS_12, JupiterN of Jupiter_45), else the name before the last underscore.
	[[nodiscard]] std::string FamilyOf(std::string_view fontName);

	[[nodiscard]] Fonts FontsOfFamily(const Fonts& fonts, std::string_view family);

	// To watch them for changes.
	[[nodiscard]] std::vector<std::filesystem::path> GlyphImageFolders(const Fonts& fonts);
}
