#pragma once

#include <FontChanger.Presets/Structs.h>

#include "MainApp/FontReplacement/Utilities.h"

namespace XivAlexander {
	class Config;
}

// Presets as the font replacement uses them: faces by game font name (FontChanger.Presets' Preset), read with
// FontChanger.Presets.Native's model.
namespace XivAlexander::Apps::MainApp::FontReplacement::Presets {
	// Faces by name (case-insensitive).
	using Faces = std::map<std::string, std::shared_ptr<const FontChanger::Structs::Face>, LessIgnoringCase>;

	// Gets the folder presets are imported into, <configuration folder>\FontPresets, making it if it isn't there. A source's
	// preset is a path relative to it, or an absolute path for a preset used where it is.
	[[nodiscard]] std::filesystem::path Folder(Config& config);

	// Gets the path of a source's preset: relative to the folder, or as it is if absolute.
	[[nodiscard]] std::filesystem::path Resolve(const std::filesystem::path& folder, const std::string& preset);

	// Gets the glyph image folders a preset reads relative to its own folder, as it names them: the files that importing
	// the preset alone would leave behind. Throws if the preset can't be read.
	[[nodiscard]] std::vector<std::filesystem::path> RelativeGlyphImageFolders(const std::filesystem::path& path);

	// Whether a glyph image folder of RelativeGlyphImageFolders is under the preset's folder, so that it can be copied
	// along with the preset: not the preset's folder itself, nor anywhere above it.
	[[nodiscard]] bool IsCopyableGlyphImageFolder(const std::filesystem::path& relative);

	// Copies a preset into the folder under a name not taken, and returns its path relative to the folder. A preset
	// without glyph image folders is copied alone; one with them gets a folder of its own, holding it and copies of the
	// folders (all of which must be copyable), so that its relative paths still find them.
	std::filesystem::path Import(const std::filesystem::path& path, const std::filesystem::path& folder, const std::vector<std::filesystem::path>& glyphImageFolders);

	// Reads a preset: a single font set (faces at the top), or several (fontSets); of a name in several font sets, the
	// first. Glyph image folders relative to the preset's folder are made absolute.
	[[nodiscard]] Faces Load(const std::filesystem::path& path);

	// Combines presets into one; of a face in several, the one of the last preset is used.
	void Combine(Faces& into, const Faces& from);

	// Gets the faces of a family only.
	[[nodiscard]] Faces FacesOfFamily(const Faces& faces, std::string_view family);

	// Gets the folders of glyph images the faces read (to watch them for changes).
	[[nodiscard]] std::vector<std::filesystem::path> GlyphImageFolders(const Faces& faces);
}
