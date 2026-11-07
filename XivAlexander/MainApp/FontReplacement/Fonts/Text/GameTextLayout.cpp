#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Text/GameTextLayout.h"

#include <icu.h>

#include "MainApp/FontReplacement/Fonts/FontServices.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

void FontReplacement::GameTextLayout::ActiveRun::Set(const uint8_t* start, int length, const SizedFont* sized, ItalicMode italic, bool wrap, std::shared_ptr<const ShapedRun> shaped) {
	Start = start;
	End = start + length;
	Sized = sized;
	Italic = italic;
	Wrap = wrap;
	Shaped = std::move(shaped);

	// The byte that ended the run is compared too, as what follows the last character could join its cluster.
	CompareLength = length < MaxRunBytes ? length + 1 : length;
	Bytes.assign(start, start + CompareLength);
}

FontReplacement::ItalicMode FontReplacement::GameTextLayout::ActiveRun::ItalicAt(const uint8_t* p) const {
	if (!Shaped->ItalicBytes)
		return Italic;
	return (*Shaped->ItalicBytes)[p - Start] ? ItalicMode::Real : ItalicMode::Upright;
}

bool FontReplacement::GameTextLayout::ActiveRun::Matches() const {
	// Byte by byte, stopping at the first difference, so that a now shorter text isn't read past its terminator.
	for (auto i = 0; i < CompareLength; i++) {
		if (Start[i] != Bytes[i])
			return false;
	}
	return true;
}

FontReplacement::GameTextLayout::GameTextLayout(FontServices& services, FontReplacer& replacer, GameGlyph* emptyGlyph)
	: m_replacer(replacer)
	, m_analyzer(services.TextAnalyzer())
	, m_emptyGlyph(emptyGlyph) {
}

FontReplacement::GameGlyph* FontReplacement::GameTextLayout::TryGetGlyph(SizedFont& sized, const uint8_t* p, ItalicMode italic) {
	return LookUp(sized, false, p, italic, [&](int& length) { return GetShapedRun(sized, p, italic, length); });
}

FontReplacement::GameGlyph* FontReplacement::GameTextLayout::TryGetWrapGlyph(std::span<SizedFont* const> sizes, std::span<const float> scales, const uint8_t* p, ItalicMode italic) {
	return LookUp(*sizes.back(), true, p, italic, [&](int& length) { return GetWrapRun(sizes, scales, p, italic, length); });
}

FontReplacement::GameGlyph* FontReplacement::GameTextLayout::LookUp(const SizedFont& sized, bool wrap, const uint8_t* p, ItalicMode italic, const std::function<std::shared_ptr<const ShapedRun>(int& length)>& shape) {
	for (auto i = m_active.size(); i-- > 0;) {
		const auto& active = *m_active[i];
		if (active.Sized == &sized && active.Wrap == wrap && p >= active.Start && p < active.End && active.ItalicAt(p) == italic && active.Matches())
			return active.Shaped->Glyphs[p - active.Start];
	}

	int length;
	auto shaped = shape(length);
	if (!shaped)
		return nullptr;

	std::unique_ptr<ActiveRun> run;
	if (m_active.size() == MaxActiveRuns) {
		run = std::move(m_active.front());
		m_active.erase(m_active.begin());
	} else {
		run = std::make_unique<ActiveRun>();
	}

	const auto glyph = shaped->Glyphs[0];
	run->Set(p, length, &sized, italic, wrap, std::move(shaped));
	m_active.push_back(std::move(run));
	return glyph;
}

void FontReplacement::GameTextLayout::Clear() {
	m_active.clear();
	m_runs.clear();
	m_wrapRuns.clear();
	m_cells.clear();
	m_spacers.clear();
	m_rasters.clear();
}

int FontReplacement::GameTextLayout::NextGrapheme(int t, int end) const {
	if (t >= end)
		return 0;

	thread_local std::unique_ptr<UBreakIterator, decltype(&ubrk_close)> s_iterator(nullptr, &ubrk_close);
	auto status = U_ZERO_ERROR;
	if (!s_iterator) {
		const auto iterator = ubrk_open(UBRK_CHARACTER, "", nullptr, 0, &status);
		if (U_FAILURE(status))
			return 1;
		s_iterator.reset(iterator);
	}

	status = U_ZERO_ERROR;
	ubrk_setText(s_iterator.get(), reinterpret_cast<const UChar*>(m_text.data() + t), end - t, &status);
	if (U_FAILURE(status))
		return 1;
	const auto next = ubrk_following(s_iterator.get(), 0);
	return next == UBRK_DONE || next <= 0 ? end - t : next;
}

std::optional<std::pair<char32_t, int>> FontReplacement::GameTextLayout::DecodeAt(int u, int end) const {
	const auto c = m_text[u];
	if (IS_HIGH_SURROGATE(c) && u + 1 < end && IS_LOW_SURROGATE(m_text[u + 1]))
		return std::pair{static_cast<char32_t>(0x10000 + ((c - 0xD800) << 10) + (m_text[u + 1] - 0xDC00)), 2};
	if (IsSurrogate(c))
		return std::nullopt;
	return std::pair{static_cast<char32_t>(c), 1};
}

std::shared_ptr<const FontReplacement::GameTextLayout::ShapedRun> FontReplacement::GameTextLayout::GetShapedRun(SizedFont& sized, const uint8_t* p, ItalicMode italic, int& length) {
	const auto count = ReadRun(p, italic, length);
	if (length == 0)
		return nullptr;

	const auto key = std::make_tuple(HashBytes(p, length), length, static_cast<const SizedFont*>(&sized), italic);
	auto it = m_runs.find(key);
	if (it == m_runs.end()) {
		auto shaped = std::make_shared<ShapedRun>();
		shaped->Glyphs.resize(length);
		if (italic != ItalicMode::Images)
			shaped->ItalicBytes.emplace(m_byteItalic, m_byteItalic + length);
		if (count != 0)
			Shape(sized, p, count, shaped->Glyphs, italic);
		if (m_runs.size() >= MaxCachedRuns)
			m_runs.clear();
		it = m_runs.emplace(key, std::move(shaped)).first;
	}

	return it->second;
}

std::shared_ptr<const FontReplacement::GameTextLayout::ShapedRun> FontReplacement::GameTextLayout::GetWrapRun(
	std::span<SizedFont* const> sizes, std::span<const float> scales, const uint8_t* p, ItalicMode italic, int& length) {
	auto& largest = *sizes.back();
	const auto top = GetShapedRun(largest, p, italic, length);
	if (!top)
		return nullptr;

	const auto key = std::make_tuple(HashBytes(p, length), length, static_cast<const SizedFont*>(&largest), italic);
	if (const auto it = m_wrapRuns.find(key); it != m_wrapRuns.end())
		return it->second;

	// Every size reads the same bytes, so their glyphs line up by byte.
	std::vector<std::shared_ptr<const ShapedRun>> runs;
	for (const auto sized : sizes.first(sizes.size() - 1)) {
		int otherLength;
		auto run = GetShapedRun(*sized, p, italic, otherLength);
		if (!run || otherLength != length)
			return top;
		runs.push_back(std::move(run));
	}
	runs.push_back(top);

	auto wrap = std::make_shared<ShapedRun>(*top);
	std::vector<float> pens(runs.size());
	auto furthest = 0.f;
	auto placed = 0;
	for (auto b = 0; b < length; b++) {
		// Characters left to the game advance as it looks them up by themselves.
		const auto glyph = top->Glyphs[b];
		if (!glyph)
			continue;
		for (size_t i = 0; i < runs.size(); i++) {
			if (const auto g = runs[i]->Glyphs[b])
				pens[i] += static_cast<float>(g->Width + g->OffsetX) * scales.back() / scales[i];
			furthest = (std::max)(furthest, pens[i]);
		}

		// A cluster's later characters advance nothing; what the sizes add there goes to the next cluster.
		if (glyph == m_emptyGlyph)
			continue;
		const auto end = static_cast<int>(std::ceil(furthest));
		wrap->Glyphs[b] = GetSpacer(largest, end - placed);
		placed = end;
	}

	if (m_wrapRuns.size() >= MaxCachedRuns)
		m_wrapRuns.clear();
	return m_wrapRuns.emplace(key, std::move(wrap)).first->second;
}

uint64_t FontReplacement::GameTextLayout::HashBytes(const uint8_t* p, int length) {
	auto hash = 0xCBF29CE484222325ull;
	for (auto j = 0; j < length; j++)
		hash = (hash ^ p[j]) * 0x100000001B3ull;
	return hash;
}

void FontReplacement::GameTextLayout::EnsureCapacity(int n) {
	if (static_cast<size_t>(n) <= m_text.size())
		return;
	const auto size = (std::max)(static_cast<size_t>(n), m_text.size() * 2);
	m_text.resize(size);
	m_textToByte.resize(size);
	m_textSource.resize(size);
	m_textItalic.resize(size);
}
