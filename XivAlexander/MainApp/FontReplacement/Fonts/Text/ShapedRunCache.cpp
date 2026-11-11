#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Text/ShapedRunCache.h"

#include "MainApp/FontReplacement/Fonts/DirectWriteServices.h"
#include "MainApp/FontReplacement/Fonts/Glyphs/GlyphStore.h"
#include "MainApp/FontReplacement/Host.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::ShapedRunCache::ShapedRun::ShapedRun(std::vector<GameGlyph*> glyphs, std::vector<uint8_t> bytes, ItalicMode italic, std::span<const uint8_t> italicBytes)
	: Glyphs(std::move(glyphs))
	, Bytes(std::move(bytes)) {
	if (italic == ItalicMode::Images || std::ranges::all_of(italicBytes, [first = italicBytes[0]](uint8_t b) { return b == first; }))
		Italic = italic == ItalicMode::Images ? italic : italicBytes[0] ? ItalicMode::Real : ItalicMode::Upright;
	else
		ItalicBytes.emplace(italicBytes.begin(), italicBytes.end());
}

FontReplacement::ItalicMode FontReplacement::ShapedRunCache::ShapedRun::ItalicAt(size_t b) const {
	if (!ItalicBytes)
		return Italic;
	return (*ItalicBytes)[b] ? ItalicMode::Real : ItalicMode::Upright;
}

void FontReplacement::ShapedRunCache::ActiveRun::Set(const uint8_t* start, int length, const SizedFont* sized, bool wrap, std::shared_ptr<const ShapedRun> shaped) {
	m_start = start;
	m_end = start + length;
	m_last = start;
	Sized = sized;
	Wrap = wrap;
	m_shaped = std::move(shaped);
	m_compareLength = length < ShapingContext::MaxRunBytes ? static_cast<size_t>(length) + 1 : static_cast<size_t>(length);
	m_endByte = m_compareLength > static_cast<size_t>(length) ? start[length] : 0;
}

std::optional<FontReplacement::GameGlyph*> FontReplacement::ShapedRunCache::ActiveRun::TryGet(const uint8_t* p, ItalicMode italic) {
	if (p < m_start || p >= m_end)
		return std::nullopt;
	const auto b = static_cast<size_t>(p - m_start);
	if (m_shaped->ItalicAt(b) != italic || (p <= m_last && !Matches()))
		return std::nullopt;
	m_last = p;
	return m_shaped->Glyphs[b];
}

bool FontReplacement::ShapedRunCache::ActiveRun::Matches() const {
	// Byte by byte, stopping at the first difference, so that a now shorter text isn't read past its terminator.
	const auto& bytes = m_shaped->Bytes;
	for (size_t i = 0; i < bytes.size(); i++) {
		if (m_start[i] != bytes[i])
			return false;
	}
	return m_compareLength == bytes.size() || m_start[bytes.size()] == m_endByte;
}

FontReplacement::ShapedRunCache::ShapedRunCache(DirectWriteServices& services, GlyphStore& store, GlyphFactory& factory, const FrameState& state)
	: m_store(store)
	, m_emptyGlyph(store.EmptyGlyph())
	, m_cells(store, factory, state)
	, m_shaper(services.TextAnalyzer(), m_cells, m_emptyGlyph)
	, m_runs(state, CachedRuns)
	, m_wrapRuns(state, CachedRuns) {
}

FontReplacement::GameGlyph* FontReplacement::ShapedRunCache::TryGetGlyph(SizedFont& sized, const uint8_t* p, ItalicMode italic) {
	return LookUp(sized, {}, {}, p, italic);
}

FontReplacement::GameGlyph* FontReplacement::ShapedRunCache::TryGetWrapGlyph(std::span<SizedFont* const> sizes, std::span<const float> scales, const uint8_t* p, ItalicMode italic) {
	return LookUp(*sizes.back(), sizes, scales, p, italic);
}

void FontReplacement::ShapedRunCache::Clear() {
	ClearActive();
	const auto drop = [this](const std::shared_ptr<const ShapedRun>& run) { Drop(*run); };
	m_runs.Clear(drop);
	m_wrapRuns.Clear(drop);
	m_cells.Clear();
}

void FontReplacement::ShapedRunCache::Sweep() {
	ClearActive();
	const auto drop = [this](const std::shared_ptr<const ShapedRun>& run) { Drop(*run); };
	m_runs.Sweep(drop);
	m_wrapRuns.Sweep(drop);
	m_cells.Sweep();
}

uint64_t FontReplacement::ShapedRunCache::HashBytes(const uint8_t* p, size_t length) {
	auto hash = 0xCBF29CE484222325ull;
	for (; length >= sizeof(uint64_t); p += sizeof(uint64_t), length -= sizeof(uint64_t)) {
		uint64_t word;
		std::memcpy(&word, p, sizeof word);
		hash = (hash ^ word) * 0x100000001B3ull;
		hash ^= hash >> 32;
	}
	for (; length > 0; p++, length--)
		hash = (hash ^ *p) * 0x100000001B3ull;
	return hash;
}

void FontReplacement::ShapedRunCache::ClearActive() {
	for (auto& active : m_active)
		m_freeActive.push_back(std::move(active));
	m_active.clear();
}

void FontReplacement::ShapedRunCache::Hold(const ShapedRun& run) {
	for (const auto glyph : run.Glyphs)
		m_store.Keep(glyph);
}

void FontReplacement::ShapedRunCache::Drop(const ShapedRun& run) {
	for (const auto glyph : run.Glyphs)
		m_store.Release(glyph);
}

FontReplacement::GameGlyph* FontReplacement::ShapedRunCache::LookUp(SizedFont& sized, std::span<SizedFont* const> sizes, std::span<const float> scales, const uint8_t* p, ItalicMode italic) {
	const auto wrap = !sizes.empty();
	for (auto i = m_active.size(); i-- > 0;) {
		auto& active = *m_active[i];
		if (active.Sized == &sized && active.Wrap == wrap) {
			if (const auto glyph = active.TryGet(p, italic))
				return *glyph;
		}
	}

	int length;
	const auto shaped = wrap ? GetWrapRun(sizes, scales, p, italic, length) : GetShapedRun(sized, p, italic, length);
	if (!shaped)
		return nullptr;

	// A run of the same kind there is stale, and could answer past the end of the new one unchecked.
	for (auto i = m_active.size(); i-- > 0;) {
		if (m_active[i]->Sized == &sized && m_active[i]->Wrap == wrap && m_active[i]->Overlaps(p, length)) {
			m_freeActive.push_back(std::move(m_active[i]));
			m_active.erase(m_active.begin() + static_cast<ptrdiff_t>(i));
		}
	}

	std::unique_ptr<ActiveRun> run;
	if (m_active.size() == MaxActiveRuns) {
		run = std::move(m_active.front());
		m_active.erase(m_active.begin());
	} else if (!m_freeActive.empty()) {
		run = std::move(m_freeActive.back());
		m_freeActive.pop_back();
	} else {
		run = std::make_unique<ActiveRun>();
	}

	run->Set(p, length, &sized, wrap, shaped);
	m_active.push_back(std::move(run));
	return shaped->Glyphs[0];
}

std::shared_ptr<const FontReplacement::ShapedRunCache::ShapedRun> FontReplacement::ShapedRunCache::FindRun(
	AgedMap<RunKey, std::shared_ptr<const ShapedRun>>& runs, const SizedFont& sized, const uint8_t* p, ItalicMode italic, RunKey& key, int& length) {
	m_shaper.ReadRun(p, italic, false, length);
	key = {HashBytes(p, static_cast<size_t>(length)), length, &sized, italic};
	if (length == 0)
		return nullptr;
	const auto known = runs.Find(key);
	if (!known)
		return nullptr;
	if (std::ranges::equal((*known)->Bytes, std::span(p, static_cast<size_t>(length))))
		return *known;
	runs.Remove(key, [this](const std::shared_ptr<const ShapedRun>& run) { Drop(*run); });
	return nullptr;
}

std::shared_ptr<const FontReplacement::ShapedRunCache::ShapedRun> FontReplacement::ShapedRunCache::GetShapedRun(SizedFont& sized, const uint8_t* p, ItalicMode italic, int& length) {
	RunKey key;
	if (auto known = FindRun(m_runs, sized, p, italic, key, length))
		return known;
	if (length == 0)
		return nullptr;

	const auto count = m_shaper.ReadRun(p, italic, true, length);
	std::vector<GameGlyph*> glyphs(static_cast<size_t>(length));
	if (count != 0) {
		try {
			m_shaper.Shape(sized, p, count, glyphs, italic);
		} catch (const std::exception& e) {
			// Cached all null, so that the run's characters are looked up by themselves instead of shaping it again for each.
			Host::Error("Shaping failed; the run's characters are laid out by themselves: {}", e.what());
			std::ranges::fill(glyphs, nullptr);
		}
	}

	return Cache(m_runs, key, std::make_shared<const ShapedRun>(std::move(glyphs), std::vector(p, p + length), italic, m_shaper.ByteItalic().first(static_cast<size_t>(length))));
}

std::shared_ptr<const FontReplacement::ShapedRunCache::ShapedRun> FontReplacement::ShapedRunCache::GetWrapRun(
	std::span<SizedFont* const> sizes, std::span<const float> scales, const uint8_t* p, ItalicMode italic, int& length) {
	auto& largest = *sizes.back();
	RunKey key;
	if (auto known = FindRun(m_wrapRuns, largest, p, italic, key, length))
		return known;
	if (length == 0)
		return nullptr;

	// Measured only, as no size draws these. Every size reads the same bytes, so their advances line up by byte.
	std::vector<std::vector<int>> advances(sizes.size());
	for (size_t i = 0; i < sizes.size(); i++) {
		// Read again for each, as measuring changes what was read.
		const auto count = m_shaper.ReadRun(p, italic, true, length);
		advances[i].assign(static_cast<size_t>(length), Shaper::Missing);
		if (count == 0)
			continue;

		try {
			m_shaper.Measure(*sizes[i], count, advances[i], italic);
		} catch (const std::exception& e) {
			// Cached all null, so that the run's characters are measured by themselves instead of shaping it again for each.
			Host::Error("Measuring a run at the UI scales failed; its characters are measured by themselves: {}", e.what());
			advances.back().assign(static_cast<size_t>(length), Shaper::Missing);
			break;
		}
	}

	const auto& top = advances.back();
	std::vector<GameGlyph*> wrap(static_cast<size_t>(length));
	std::vector<float> pens(sizes.size());
	auto furthest = 0.f;
	auto placed = 0;
	for (size_t b = 0; b < static_cast<size_t>(length); b++) {
		// Characters left to the game advance as it looks them up by themselves.
		if (top[b] == Shaper::Missing)
			continue;
		for (size_t i = 0; i < sizes.size(); i++) {
			if (advances[i][b] != Shaper::Missing)
				pens[i] += static_cast<float>(advances[i][b]) * scales.back() / scales[i];
			furthest = (std::max)(furthest, pens[i]);
		}

		// A cluster's later characters advance nothing; what the sizes add there goes to the next cluster.
		if (top[b] == 0) {
			wrap[b] = m_emptyGlyph;
			continue;
		}

		// A spacer advances at most 127, and what it can't goes to the next cluster.
		const auto advance = (std::min)(static_cast<int>(std::ceil(furthest)) - placed, 127);
		wrap[b] = m_cells.GetSpacer(largest, advance);
		placed += advance;
	}

	return Cache(m_wrapRuns, key, std::make_shared<const ShapedRun>(std::move(wrap), std::vector(p, p + length), italic, m_shaper.ByteItalic().first(static_cast<size_t>(length))));
}

std::shared_ptr<const FontReplacement::ShapedRunCache::ShapedRun> FontReplacement::ShapedRunCache::Cache(
	AgedMap<RunKey, std::shared_ptr<const ShapedRun>>& runs, const RunKey& key, std::shared_ptr<const ShapedRun> run) {
	Hold(*run);
	return runs.Add(key, std::move(run));
}
