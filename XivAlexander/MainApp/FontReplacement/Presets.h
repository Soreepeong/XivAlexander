#pragma once

#include <FontChanger.Presets/Structs.h>

#include "MainApp/FontReplacement/Utilities.h"

namespace XivAlexander {
	class Config;
}

// Faces by game font name (FontChanger.Presets' Preset), read with FontChanger.Presets.Native's model.
namespace XivAlexander::Apps::MainApp::FontReplacement::Presets {
	using Faces = std::map<std::string, std::shared_ptr<const FontChanger::Structs::Face>, LessIgnoringCase>;

	// <configuration folder>\FontPresets, created if missing; a source's preset is relative to it, or absolute if used in place.
	[[nodiscard]] std::filesystem::path Folder(Config& config);

	[[nodiscard]] std::filesystem::path Resolve(const std::filesystem::path& folder, const std::string& preset);

	// As the preset names them: what importing the preset alone would leave behind. Throws if the preset can't be read.
	[[nodiscard]] std::vector<std::filesystem::path> RelativeGlyphImageFolders(const std::filesystem::path& path);

	// Strictly under the preset's folder (not the folder itself or above), so that it can be copied along with the preset.
	[[nodiscard]] bool IsCopyableGlyphImageFolder(const std::filesystem::path& relative);

	// Returns the path relative to folder; a preset with glyph image folders (all copyable) gets its own folder holding copies of them.
	std::filesystem::path Import(const std::filesystem::path& path, const std::filesystem::path& folder, const std::vector<std::filesystem::path>& glyphImageFolders);

	// One font set (faces at the top) or several (fontSets), the first winning per name; relative glyph image folders are made absolute.
	[[nodiscard]] Faces Load(const std::filesystem::path& path);

	// For a face in several, the last preset's wins.
	void Combine(Faces& into, const Faces& from);

	[[nodiscard]] Faces FacesOfFamily(const Faces& faces, std::string_view family);

	// To watch them for changes.
	[[nodiscard]] std::vector<std::filesystem::path> GlyphImageFolders(const Faces& faces);
}
