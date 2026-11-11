#include "pch.h"
#include "MainApp/FontReplacement/Fonts/FamilyAtlases.h"

#include "Utils/StringUtils.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

std::shared_ptr<FontReplacement::GlyphAtlas> FontReplacement::FamilyAtlases::Get(const std::string& family) {
	if (const auto it = m_atlases.find(family); it != m_atlases.end())
		return it->second;

	// FontSubpixelPS tells subpixel pages from the game's textures by their odd width. Grayscale pages get an even one.
	const auto& s = m_settings;
	const auto axis = Utils::EqualsI(family, "AXIS");
	auto width = axis ? s.AxisPageWidth : s.OtherPageWidth;
	const auto height = axis ? s.AxisPageHeight : s.OtherPageHeight;
	const auto maxPages = axis ? s.AxisMaxPages : s.OtherMaxPages;
	if (m_subpixel == (width % 2 == 0))
		width--;
	const auto format = s.Format == AtlasTextureFormat::Bgra4444 && m_supports4444() ? AtlasTextureFormat::Bgra4444 : AtlasTextureFormat::Bgra8888;
	const auto atlas = m_atlases.emplace(family, std::make_shared<GlyphAtlas>(width, height, maxPages, family, m_subpixel, format)).first->second;
	if (!atlas->TryEnsurePages(1))
		throw std::runtime_error("Could not add an atlas page.");
	return atlas;
}

void FontReplacement::FamilyAtlases::Upload() {
	for (const auto& atlas : m_atlases | std::views::values)
		atlas->Upload();
}

void FontReplacement::FamilyAtlases::ClearAll() {
	for (const auto& atlas : m_atlases | std::views::values)
		atlas->Clear();
}

void FontReplacement::FamilyAtlases::Dispose() {
	for (const auto& atlas : m_atlases | std::views::values)
		atlas->Dispose();
	m_atlases.clear();
}
