#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Faces/FaceSourceCache.h"

#include <FontChanger.Presets/GlyphFiles.h>

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;
using FontChanger::Structs::ElementSourceEnum;

std::string FontReplacement::FaceSourceCache::KeyOf(const FontChanger::Structs::FontElement& def, DWRITE_MEASURING_MODE measureMode) {
	auto json = nlohmann::json(def);
	json.erase("mergeMode");
	json.erase("wrapModifiers");
	json.erase("glyphMerging");
	json.erase("auto");

	// Outlines and images are drawn at the element's size, which the game's and empty fonts are made at.
	if (def.Source == ElementSourceEnum::DirectWrite || def.Source == ElementSourceEnum::FreeType || def.Source == ElementSourceEnum::GlyphImages)
		json.erase("size");

	// Relative glyph file paths are resolved, as the same path may name different folders.
	if (auto& renderSpecific = json["renderSpecific"]; renderSpecific.contains("glyphImages"))
		renderSpecific["glyphImages"]["path"] = xivres::util::unicode::convert<std::string>(FontChanger::GlyphFiles::ResolvePath(def.RendererSpecific.GlyphImages.Path).wstring());
	json["measureMode"] = static_cast<int>(measureMode);
	return json.dump();
}

std::shared_ptr<FontReplacement::IFaceSource> FontReplacement::FaceSourceCache::GetOrCreate(const std::string& key, const std::function<std::unique_ptr<IFaceSource>()>& make) {
	if (const auto it = m_sources.find(key); it != m_sources.end()) {
		if (auto source = it->second.lock())
			return source;
	}

	std::erase_if(m_sources, [](const auto& entry) { return entry.second.expired(); });
	std::shared_ptr<IFaceSource> source = make();
	if (source)
		m_sources.insert_or_assign(key, source);
	return source;
}
