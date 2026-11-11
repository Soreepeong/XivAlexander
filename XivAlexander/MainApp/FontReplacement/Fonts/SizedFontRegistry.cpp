#include "pch.h"
#include "MainApp/FontReplacement/Fonts/SizedFontRegistry.h"

#include "MainApp/FontReplacement/Fonts/AtlasSet.h"
#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFont.h"
#include "MainApp/FontReplacement/Fonts/Glyphs/GlyphStore.h"
#include "MainApp/FontReplacement/Game/FontNames.h"
#include "MainApp/FontReplacement/Presets.h"
#include "Utils/StringUtils.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::SizedFontRegistry::SizedFontRegistry(std::unique_ptr<FontSet> fonts, AtlasSet& atlases, GlyphStore& store, const FrameState& state)
	: m_fonts(std::move(fonts))
	, m_atlases(atlases)
	, m_store(store)
	, m_state(state) {
}

FontReplacement::SizedFont* FontReplacement::SizedFontRegistry::Get(GameFont* original, int halfPx, bool bold) {
	const auto request = std::make_tuple(original, halfPx, bold);
	if (const auto it = m_byRequest.find(request); it != m_byRequest.end())
		return it->second;

	const auto px = static_cast<float>(halfPx) / 2.f;
	struct Candidate {
		std::string Name;
		float Size;
		MaterializedFont* Font;
		GameFont* Game;
	};

	std::vector<Candidate> fonts;
	const auto originalName = FontNames::TryGetFontName(original);
	if (originalName) {
		auto family = FontNames::FontNamesOf(Presets::FamilyOf(*originalName));
		std::ranges::stable_sort(family, std::ranges::greater{}, [](const auto& f) { return f.second; });
		for (auto& [name, size] : family) {
			const auto loaded = Utils::EqualsI(name, *originalName) ? original : FontNames::TryFindFont(name);
			if (!loaded || !loaded->IsReady() || loaded->Size() <= 0)
				continue;
			const auto font = &m_fonts->FontOf(name);
			fonts.push_back({std::move(name), size, font, loaded});
		}
	}

	if (fonts.empty()) {
		auto name = originalName.value_or(std::format("0x{:X}", reinterpret_cast<uintptr_t>(original)));
		fonts = {{std::move(name), 0, originalName ? &m_fonts->FontOf(*originalName) : &m_fonts->Game(), original}};
	} else if (const auto exact = std::ranges::find_if(fonts, [px](const Candidate& f) { return std::abs(f.Size - px) < 0.01f; }); exact != fonts.end()) {
		fonts = {*exact};
	}

	size_t best = 0;
	for (size_t i = 1; i < fonts.size(); i++) {
		if (fonts[i].Font->OwnCodepointCount() > fonts[best].Font->OwnCodepointCount())
			best = i;
	}

	const auto& [fontName, size, font, game] = fonts[best];
	const auto key = std::make_tuple(font, halfPx, fontName, bold);
	auto it = m_sizedFonts.find(key);
	if (it == m_sizedFonts.end()) {
		const auto [ascent, lineHeight] = font->GetLineMetrics(px, game);
		std::vector<std::pair<MaterializedFont*, GameFont*>> pairs;
		for (const auto& f : fonts)
			pairs.emplace_back(f.Font, f.Game);
		it = m_sizedFonts.emplace(key, std::make_unique<SizedFont>(font, px, ascent, lineHeight, game, m_atlases.Get(Presets::FamilyOf(fontName)), std::move(pairs), bold, m_state)).first;
	}
	return m_byRequest[request] = it->second.get();
}

void FontReplacement::SizedFontRegistry::Sweep() {
	const auto count = CountGlyphs();
	if (count <= m_sweepAt)
		return;

	std::vector<uint64_t> used;
	used.reserve(count);
	for (const auto& sized : m_sizedFonts | std::views::values)
		sized->CollectUsed(used);
	const auto threshold = AgedMaps::Threshold(used, GlyphCapacity, m_state.Frame());
	const auto release = [this](GameGlyph* glyph) { m_store.Release(glyph); };
	for (const auto& sized : m_sizedFonts | std::views::values)
		sized->ReleaseGlyphsUsedBefore(threshold, release);
	m_sweepAt = AgedMaps::NextSweepAt(CountGlyphs(), GlyphCapacity);
}

void FontReplacement::SizedFontRegistry::ReleaseAllGlyphs() {
	const auto release = [this](GameGlyph* glyph) { m_store.Release(glyph); };
	for (const auto& sized : m_sizedFonts | std::views::values)
		sized->ReleaseGlyphs(release);
}

void FontReplacement::SizedFontRegistry::Clear() {
	ReleaseAllGlyphs();
	m_byRequest.clear();
	m_sizedFonts.clear();
	m_sweepAt = GlyphCapacity;
}

void FontReplacement::SizedFontRegistry::SwapFonts(std::unique_ptr<FontSet> fonts) {
	std::swap(m_fonts, fonts);
}

size_t FontReplacement::SizedFontRegistry::CountGlyphs() const {
	size_t count = 0;
	for (const auto& sized : m_sizedFonts | std::views::values)
		count += sized->Glyphs.Size();
	return count;
}
