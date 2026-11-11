#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Text/ShapingContext.h"

#include <icu.h>

#include "MainApp/FontReplacement/Fonts/Faces/IFaceSource.h"
#include "MainApp/FontReplacement/Fonts/Faces/LoadedFont.h"
#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::RasterKey FontReplacement::RasterKey::Of(const SizedFont& sized, const PlacedCluster& c) {
	RasterKey key{
		.Px = sized.Px,
		.Element = c.Pick ? c.Pick->Element : nullptr,
		.Source = c.Source,
		.EmSize = c.Px,
		.Origin = c.Origin,
		.RawAdvance = c.RawAdvance,
		.Alignment = c.Pick ? c.Pick->Font->GetAlignment(sized.Px, c.Pick->Game) : FontAlignment{},
		.Glyphs = c.GlyphIds,
		.Advances = c.Advances,
	};
	for (const auto& o : c.Offsets)
		key.Offsets.emplace_back(o.Advance, o.Ascender);
	return key;
}

std::optional<FontReplacement::RasterGlyph> FontReplacement::PlacedCluster::Rasterize() const {
	return Source->Rasterize(Px, {GlyphIds, Advances, Offsets}, {.OriginX = Origin, .Advance = RawAdvance});
}

FontReplacement::PlacedCluster FontReplacement::PlacedCluster::MovedBy(float start, float end) const {
	const auto pen = static_cast<float>(X0) + Origin + start;
	const auto x0 = StdRound(pen);
	auto res = *this;
	res.Key.reset();
	res.Raster.reset();
	res.Origin = StdRound((pen - x0) * ShapingContext::SubpixelSteps) / ShapingContext::SubpixelSteps;
	res.X0 = static_cast<int>(x0);
	res.X1 = static_cast<int>(StdRound(static_cast<float>(X1) + end));
	return res;
}

void FontReplacement::ShapingContext::EnsureCapacity(int n) {
	if (static_cast<size_t>(n) <= Text.size())
		return;
	const auto size = (std::max)(static_cast<size_t>(n), Text.size() * 2);
	Text.resize(size);
	TextToByte.resize(size);
	TextSource.resize(size);
	TextItalic.resize(size);
}

int FontReplacement::ShapingContext::NextGrapheme(int t, int end) const {
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
	ubrk_setText(s_iterator.get(), reinterpret_cast<const UChar*>(Text.data() + t), end - t, &status);
	if (U_FAILURE(status))
		return 1;
	const auto next = ubrk_following(s_iterator.get(), 0);
	return next == UBRK_DONE || next <= 0 ? end - t : next;
}

std::optional<std::pair<char32_t, int>> FontReplacement::ShapingContext::DecodeAt(int u, int end) const {
	const auto c = Text[u];
	if (IS_HIGH_SURROGATE(c) && u + 1 < end && IS_LOW_SURROGATE(Text[u + 1]))
		return std::pair{static_cast<char32_t>(0x10000 + ((c - 0xD800) << 10) + (Text[u + 1] - 0xDC00)), 2};
	if (IsSurrogate(c))
		return std::nullopt;
	return std::pair{static_cast<char32_t>(c), 1};
}
