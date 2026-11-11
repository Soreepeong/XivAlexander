#include "pch.h"
#include "MainApp/FontReplacement/Fonts/CopyFonts.h"

#include "MainApp/FontReplacement/Fonts/FrameState.h"
#include "MainApp/FontReplacement/Fonts/GlyphAtlas.h"
#include "MainApp/FontReplacement/Fonts/SizedFontRegistry.h"
#include "MainApp/FontReplacement/Game/KernelTextureWrapper.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

void FontReplacement::CopyInfo::Resize(SizedFont* value) {
	m_sized = value;
	m_bold = nullptr;
	m_boldFailed = false;
	m_wrapSizes.clear();
}

FontReplacement::SizedFont* FontReplacement::CopyInfo::GetBold(SizedFontRegistry& registry) {
	if (m_boldFailed)
		return m_sized;
	if (!m_bold)
		m_bold = registry.Get(Original, HalfPx, true);
	return m_bold;
}

std::span<FontReplacement::SizedFont* const> FontReplacement::CopyInfo::GetWrapSizes(
	SizedFontRegistry& registry, std::span<const float> scales, float sizeY, bool bold, SizedFont* drawn) {
	if (!m_wrapSizes.empty() && m_wrapSizes.back() == drawn && m_wrapSizeY == sizeY && m_wrapBold == bold)
		return m_wrapSizes;

	std::vector<SizedFont*> sizes(scales.size());
	for (size_t i = 0; i + 1 < sizes.size(); i++)
		sizes[i] = registry.Get(Original, CopyFonts::HalfPxOf(sizeY * scales[i]), bold);
	sizes.back() = drawn;
	m_wrapSizes = std::move(sizes);
	m_wrapSizeY = sizeY;
	m_wrapBold = bold;
	return m_wrapSizes;
}

FontReplacement::CopyFonts::~CopyFonts() {
	for (const auto copy : m_copies | std::views::keys)
		std::free(copy);
}

int FontReplacement::CopyFonts::HalfPxOf(float px) {
	return std::clamp(static_cast<int>(StdRound(px * 2)), MinHalfPx, MaxHalfPx);
}

void FontReplacement::CopyFonts::ForgetPicked() {
	const auto manager = GameFontManager::Instance();
	if (!manager || !manager->FontSets())
		return;
	for (auto i = 0; i < GameFontManager::FontSetCount; i++) {
		const auto set = manager->FontSet(i);
		set->CurrentFont() = nullptr;
		set->CurrentFontSize() = 0;
	}
}

FontReplacement::CopyInfo* FontReplacement::CopyFonts::TryGet(GameFont* copy) {
	if (!copy || copy != m_lastCopy) {
		const auto it = m_copies.find(copy);
		if (it == m_copies.end())
			return nullptr;
		m_lastCopy = copy;
		m_lastInfo = &it->second;
	}

	if (m_lastInfo->Generation != m_generation)
		Resync(copy, *m_lastInfo);
	return m_lastInfo;
}

FontReplacement::GameFont* FontReplacement::CopyFonts::OriginalOf(GameFont* font) const {
	const auto it = m_copies.find(font);
	return it != m_copies.end() ? it->second.Original : font;
}

FontReplacement::GameFont* FontReplacement::CopyFonts::Pick(GameFont* original, float px, bool mayStandIn, int& halfPx, bool& clamped) {
	const auto asked = HalfPxOf(px);
	const auto key = std::make_pair(original, asked);
	PickedCopy pick{};
	if (const auto it = m_picks.find(key); it != m_picks.end()) {
		pick = it->second;
	} else if (!mayStandIn || !TryStandIn(key, pick)) {
		m_unsettled.erase(key);
		auto drawn = asked;
		if (const auto lineHeight = m_sized.Get(original, drawn, false)->LineHeight; lineHeight > MaxDrawnLineHeight) {
			// Line metrics are rounded, so the size whose lines fit is checked.
			drawn = (std::max)(MinHalfPx, drawn * MaxDrawnLineHeight / lineHeight);
			while (drawn > MinHalfPx && m_sized.Get(original, drawn, false)->LineHeight > MaxDrawnLineHeight)
				drawn--;
		}

		pick = {GetOrCreate(original, drawn), drawn};
		m_picks.emplace(key, pick);
	}

	// Picks are forgotten by MarkStale, so the copy is up to date.
	halfPx = pick.HalfPx;
	clamped = pick.HalfPx != asked;
	return pick.Copy;
}

FontReplacement::GameFont* FontReplacement::CopyFonts::GetOrCreate(GameFont* original, int halfPx) {
	const auto key = std::make_pair(original, halfPx);
	if (const auto it = m_byKey.find(key); it != m_byKey.end()) {
		TryGet(it->second);
		return it->second;
	}

	const auto copy = static_cast<GameFont*>(std::malloc(GameFont::StructSize));
	if (!copy)
		throw std::bad_alloc();
	try {
		auto info = CopyInfo(original, halfPx, m_sized.Get(original, halfPx, false));
		info.Generation = m_generation;
		Sync(copy, info);
		m_copies.emplace(copy, std::move(info));
		m_byKey.emplace(key, copy);
		m_madeSizes[original].push_back(halfPx);
	} catch (...) {
		m_copies.erase(copy);
		std::free(copy);
		throw;
	}
	return copy;
}

void FontReplacement::CopyFonts::MarkStale() {
	m_generation++;
	m_picks.clear();
	m_unsettled.clear();
	for (const auto& [copy, info] : m_copies)
		Sync(copy, info);
}

void FontReplacement::CopyFonts::SyncAll() {
	for (const auto& [copy, info] : m_copies) {
		if (info.Generation == m_generation)
			Sync(copy, info);
	}
}

void FontReplacement::CopyFonts::ApplyPages(GlyphAtlas* atlas) {
	for (const auto& [copy, info] : m_copies) {
		if (info.Sized()->Atlas == atlas)
			ApplyPages(copy, *atlas);
	}
}

bool FontReplacement::CopyFonts::TryStandIn(const std::pair<GameFont*, int>& key, PickedCopy& pick) {
	const auto frame = m_state.Frame();
	auto streak = 1;
	if (const auto it = m_unsettled.find(key); it != m_unsettled.end() && it->second.Frame + 1 >= frame)
		streak = it->second.Frame == frame ? it->second.Streak : it->second.Streak + 1;
	m_unsettled[key] = {frame, streak};
	const auto sizes = m_madeSizes.find(key.first);
	if (streak >= SettleFrames || sizes == m_madeSizes.end())
		return false;

	auto nearest = sizes->second.front();
	for (const auto size : sizes->second) {
		if (std::abs(size - key.second) < std::abs(nearest - key.second))
			nearest = size;
	}

	pick = {GetOrCreate(key.first, nearest), nearest};
	return true;
}

void FontReplacement::CopyFonts::Resync(GameFont* copy, CopyInfo& info) {
	info.Resize(m_sized.Get(info.Original, info.HalfPx, false));
	info.Generation = m_generation;
	Sync(copy, info);
}

void FontReplacement::CopyFonts::Sync(GameFont* copy, const CopyInfo& info) {
	const auto& s = *info.Sized();
	copy->CopyFrom(info.Original);
	copy->Size() = s.Px;
	copy->Ascent() = s.Ascent;
	copy->LineHeight() = s.LineHeight;

	// Neither the narrower-glyph substitution nor the game's kerning pairs apply to these glyphs.
	copy->Secondary() = nullptr;
	copy->SecondaryRatio() = 0;
	copy->KerningCount() = 0;

	// Real italics are spaced by shaping, so no gap after them for sheared glyphs. Measuring reads it from the copy too.
	copy->SetItalicCorrection(0);

	// The edge and glare shaders step by one texel of this width (it goes into every vertex), which must be the pages'. Claiming a narrower
	// width widens the edge as many times, and the edge shader takes the step as its radius.
	const auto width = static_cast<uint16_t>(std::clamp(StdRound(static_cast<float>(s.Atlas->Width()) / m_edge.GetWidth(s.Px)), 256.f, 65535.f));
	copy->TextureWidth() = width;
	copy->TextureHeight() = width;

	// The game font's textures belong to its resource handles, which the game releases on its own.
	ApplyPages(copy, *s.Atlas);
}

void FontReplacement::CopyFonts::ApplyPages(GameFont* copy, const GlyphAtlas& atlas) const {
	const auto blank = m_blankTexture.Kernel();
	const auto pages = (std::min)(atlas.PageCount(), GameFont::MaxTextures);
	for (auto i = 0; i < GameFont::MaxTextures; i++)
		copy->SetTexture(i, i < pages ? atlas.GetKernelTexture(i) : blank);
	copy->TextureCount() = static_cast<uint16_t>(GameFont::MaxTextures);
}
