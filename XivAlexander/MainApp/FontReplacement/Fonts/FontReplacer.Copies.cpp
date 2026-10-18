#include "pch.h"
#include "MainApp/FontReplacement/Fonts/FontReplacer.h"

#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFont.h"
#include "MainApp/FontReplacement/Game/FontNames.h"
#include "Utils/StringUtils.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

bool FontReplacement::FontReplacer::ResyncCopies(GameFont* only) {
	auto any = false;
	for (auto& [copy, info] : m_copies) {
		if (only && info.Original != only)
			continue;
		info.Sized = GetSized(info.Original, info.HalfPx);
		Sync(copy, info);
		any = true;
	}
	return any;
}

FontReplacement::FontReplacer::SizedFont* FontReplacement::FontReplacer::GetSized(GameFont* original, int halfPx) {
	const auto px = static_cast<float>(halfPx) / 2.f;
	struct Candidate {
		std::string Name;
		float Size;
		MaterializedFont* Font;
		GameFont* Game;
	};

	std::vector<Candidate> candidates;
	const auto originalName = FontNames::TryGetFontName(original);
	if (originalName) {
		auto family = FontNames::FontNamesOf(Presets::FamilyOf(*originalName));
		std::ranges::stable_sort(family, std::ranges::greater{}, [](const auto& f) { return f.second; });
		for (auto& [name, size] : family) {
			const auto font = Utils::EqualsI(name, *originalName) ? original : FontNames::TryFindFont(name);
			if (!font || !font->IsReady() || font->Size() <= 0)
				continue;
			const auto materialized = FontOf(name);
			candidates.push_back({std::move(name), size, materialized, font});
		}
	}

	if (candidates.empty()) {
		auto name = originalName.value_or(std::format("0x{:X}", reinterpret_cast<uintptr_t>(original)));
		candidates = {{std::move(name), 0, originalName ? FontOf(*originalName) : m_gameFont.get(), original}};
	} else if (const auto exact = std::ranges::find_if(candidates, [px](const Candidate& f) { return std::abs(f.Size - px) < 0.01f; }); exact != candidates.end()) {
		candidates = {*exact};
	}

	size_t best = 0;
	for (size_t i = 1; i < candidates.size(); i++) {
		if (candidates[i].Font->OwnCodepointCount() > candidates[best].Font->OwnCodepointCount())
			best = i;
	}

	const auto& [fontName, size, materialized, game] = candidates[best];
	const auto key = std::make_tuple(materialized, halfPx, fontName);
	auto it = m_sizedFonts.find(key);
	if (it == m_sizedFonts.end()) {
		const auto [ascent, lineHeight] = materialized->GetLineMetrics(px, game);
		auto sized = std::make_unique<SizedFont>(SizedFont{
			.Font = materialized,
			.Atlas = GetAtlas(Presets::FamilyOf(fontName)),
			.Game = game,
			.Px = px,
			.Ascent = ascent,
			.LineHeight = lineHeight,
		});
		for (const auto& f : candidates) {
			sized->Fonts.emplace_back(f.Font, f.Game);
			sized->Sources.push_back(f.Game);
		}
		it = m_sizedFonts.emplace(key, std::move(sized)).first;
	}
	return it->second.get();
}

FontReplacement::MaterializedFont* FontReplacement::FontReplacer::FontOf(const std::string& fontName) {
	const auto it = m_presetFonts.find(fontName);
	return it != m_presetFonts.end() ? it->second.get() : m_gameFont.get();
}

FontReplacement::GlyphAtlas* FontReplacement::FontReplacer::GetAtlas(const std::string& family) {
	if (const auto it = m_atlases.find(family); it != m_atlases.end())
		return it->second.get();

	// FontSubpixelPS tells subpixel pages from the game's textures by their odd width; grayscale pages get an even one.
	const auto subpixel = m_subpixelShaders.Active();
	const auto axis = Utils::EqualsI(family, "AXIS");
	auto width = axis ? m_atlas.AxisPageWidth : m_atlas.OtherPageWidth;
	const auto height = axis ? m_atlas.AxisPageHeight : m_atlas.OtherPageHeight;
	const auto maxPages = (std::min)(axis ? m_atlas.AxisMaxPages : m_atlas.OtherMaxPages, GameFont::MaxTextures);
	if (subpixel == (width % 2 == 0))
		width--;
	const auto format = m_atlas.Format == AtlasTextureFormat::Bgra4444 && Supports4444() ? AtlasTextureFormat::Bgra4444 : AtlasTextureFormat::Bgra8888;
	const auto atlas = m_atlases.emplace(family, std::make_unique<GlyphAtlas>(width, height, maxPages, family, subpixel, format)).first->second.get();
	atlas->PageAdded = [this, atlas] { OnPageAdded(atlas); };
	if (!atlas->TryEnsurePages(1))
		throw std::runtime_error("Could not add an atlas page.");
	return atlas;
}

void FontReplacement::FontReplacer::Sync(GameFont* copy, const CopyInfo& info) {
	const auto& sized = *info.Sized;
	copy->CopyFrom(info.Original);
	copy->Size() = sized.Px;
	copy->Ascent() = sized.Ascent;
	copy->LineHeight() = sized.LineHeight;

	// Neither the narrower-glyph substitution nor the game's kerning pairs apply to these glyphs.
	copy->Secondary() = nullptr;
	copy->SecondaryRatio() = 0;
	copy->KerningCount() = 0;

	// Real italics are spaced by shaping, so no gap for sheared glyphs leaning past their advance; measuring reads it from the copy too.
	copy->SetItalicCorrection(0);

	// Edge and glare shaders step by one texel of this width (in every vertex): the pages', not the game textures' (lobby fonts' are smaller), else
	// outlines sample texels apart. Claiming narrower widens the step and edge that many times; the edge shader takes the step as its radius.
	const auto width = static_cast<uint16_t>(std::clamp(StdRound(static_cast<float>(sized.Atlas->Width()) / m_edge.GetWidth(sized.Px)), 256.f, 65535.f));
	copy->TextureWidth() = width;
	copy->TextureHeight() = width;

	// None of the game font's textures: they belong to its resource handles, which the game releases on its own.
	ApplyPages(copy, *sized.Atlas);
}

FontReplacement::GameFont* FontReplacement::FontReplacer::GetOrCreateCopy(GameFont* original, int halfPx) {
	if (const auto it = m_copiesByKey.find({original, halfPx}); it != m_copiesByKey.end())
		return it->second;

	const auto copy = static_cast<GameFont*>(std::malloc(GameFont::StructSize));
	if (!copy)
		throw std::bad_alloc();
	try {
		const CopyInfo info{original, halfPx, GetSized(original, halfPx)};
		Sync(copy, info);
		m_copies.emplace(copy, info);
		m_copiesByKey.emplace(std::make_pair(original, halfPx), copy);
	} catch (...) {
		m_copies.erase(copy);
		std::free(copy);
		throw;
	}
	return copy;
}

void FontReplacement::FontReplacer::ApplyPages(GameFont* copy, const GlyphAtlas& atlas) {
	const auto blank = m_blankTexture->Kernel();
	const auto pages = (std::min)(atlas.PageCount(), GameFont::MaxTextures);
	for (auto i = 0; i < GameFont::MaxTextures; i++)
		copy->SetTexture(i, i < pages ? atlas.GetKernelTexture(i) : blank);
	copy->TextureCount() = static_cast<uint16_t>(GameFont::MaxTextures);
}

void FontReplacement::FontReplacer::OnPageAdded(GlyphAtlas* atlas) {
	if (GetCurrentThreadId() != m_mainThreadId) {
		const auto lock = std::scoped_lock(m_pendingPagesMutex);
		m_pendingPageAtlases.insert(atlas);
		return;
	}

	for (const auto& [copy, info] : m_copies) {
		if (info.Sized->Atlas == atlas)
			ApplyPages(copy, *atlas);
	}
}

void FontReplacement::FontReplacer::ApplyPendingPages() {
	std::set<GlyphAtlas*> pending;
	{
		const auto lock = std::scoped_lock(m_pendingPagesMutex);
		if (m_pendingPageAtlases.empty())
			return;
		pending.swap(m_pendingPageAtlases);
	}

	for (const auto atlas : pending)
		OnPageAdded(atlas);
}
