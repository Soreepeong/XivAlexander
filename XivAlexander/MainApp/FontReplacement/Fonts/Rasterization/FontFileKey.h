#pragma once

#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Of the face's first file only; Loader is null if the face has none.
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
