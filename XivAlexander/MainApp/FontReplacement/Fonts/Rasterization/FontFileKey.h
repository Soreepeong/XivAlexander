#pragma once

#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// The face's first file; loader and key tell files apart. Loader is null if the face has no file.
	struct FontFileKey {
		IDWriteFontFilePtr File;
		IDWriteFontFileLoaderPtr Loader;
		const void* Key = nullptr;
		uint32_t KeySize = 0;

		static FontFileKey Of(IDWriteFontFace* face);

		[[nodiscard]] bool SameFileAs(const FontFileKey& other) const {
			return Loader && Loader == other.Loader && KeySize == other.KeySize && std::memcmp(Key, other.Key, KeySize) == 0;
		}
	};
}
