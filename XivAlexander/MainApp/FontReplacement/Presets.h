#pragma once

#include <FontChanger.Presets/Structs.h>

#include "MainApp/FontReplacement/Utilities.h"

// Presets as the font replacement uses them: faces by game font name (FontChanger.Presets' Preset), read with
// FontChanger.Presets.Native's model.
namespace XivAlexander::Apps::MainApp::FontReplacement::Presets {
	// Faces by name (case-insensitive).
	using Faces = std::map<std::string, std::shared_ptr<const FontChanger::Structs::Face>, LessIgnoringCase>;

	// Reads a preset: a single font set (faces at the top), or several (fontSets); of a name in several font sets, the
	// first. Glyph image folders relative to the preset's folder are made absolute.
	[[nodiscard]] Faces Load(const std::filesystem::path& path);

	// Combines presets into one; of a face in several, the one of the last preset is used.
	void Combine(Faces& into, const Faces& from);

	// Gets the faces of a family only.
	[[nodiscard]] Faces OnlyFamily(const Faces& faces, std::string_view family);

	// Gets the folders of glyph images the faces read (to watch them for changes).
	[[nodiscard]] std::vector<std::filesystem::path> GlyphImageFolders(const Faces& faces);
}
