#pragma once

#include <FontChanger.Presets/Structs.h>

#include "MainApp/FontReplacement/Fonts/Faces/IFaceSource.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Sources shared by the elements of one set of fonts that draw the same face the same way, kept while an element uses them.
	// A set's fonts and their sources are used on one thread at a time.
	class FaceSourceCache {
		std::map<std::string, std::weak_ptr<IFaceSource>> m_sources;

	public:
		// Of what a source made from def draws. Element-level settings (codepoints, offsets, merge mode, glyph merging) are left out.
		[[nodiscard]] static std::string KeyOf(const FontChanger::Structs::FontElement& def, DWRITE_MEASURING_MODE measureMode);

		// make is called if no element uses a source of the key. Its source is shared only if it isn't nullptr.
		std::shared_ptr<IFaceSource> GetOrCreate(const std::string& key, const std::function<std::unique_ptr<IFaceSource>()>& make);
	};
}
