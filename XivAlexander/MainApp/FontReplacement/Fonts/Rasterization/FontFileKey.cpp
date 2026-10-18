#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Rasterization/FontFileKey.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::FontFileKey FontReplacement::FontFileKey::Of(IDWriteFontFace* face) {
	FontFileKey res;
	UINT32 count = 1;
	IDWriteFontFile* file = nullptr;
	if (FAILED(face->GetFiles(&count, &file)) || !file)
		return res;
	res.File.Attach(file);
	if (FAILED(res.File->GetReferenceKey(&res.Key, &res.KeySize)) || FAILED(res.File->GetLoader(&res.Loader)))
		return {};
	return res;
}
