#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Text/Shaper.h"

#include <execution>

#include "MainApp/FontReplacement/Fonts/Faces/IFaceSource.h"
#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFontElement.h"
#include "MainApp/FontReplacement/Fonts/Text/CellCache.h"
#include "MainApp/FontReplacement/Fonts/Glyphs/GlyphBox.h"
#include "MainApp/FontReplacement/Fonts/Text/DirectWriteTextAnalyzer.h"
#include "MainApp/FontReplacement/Fonts/Text/HangulComposer.h"
#include "MainApp/FontReplacement/Host.h"
#include "MainApp/FontReplacement/Utilities/GameUtf8.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	constexpr uint8_t NewLineMacro = 0x10;
	constexpr uint8_t BoldMacro = 0x19;
	constexpr uint8_t ItalicMacro = 0x1A;

	// A ligature's text elements share its advance evenly.
	int Share(int advance, int parts, int part) {
		return advance * (part + 1) / parts - advance * part / parts;
	}
}

int FontReplacement::Shaper::ReadRun(const uint8_t* p, ItalicMode italic, bool decode, int& length) {
	auto& ctx = m_ctx;
	auto count = 0;
	auto i = 0;
	auto inItalic = italic != ItalicMode::Upright;
	while (i < ShapingContext::MaxRunBytes) {
		const auto b = p[i];
		if (b == 0 || b == 0x0A || b == 0x0D)
			break;

		if (b == GameUtf8::Stx) {
			// Bold is laid out with another sized font.
			const auto macro = p[i + 1];
			if (macro == NewLineMacro || macro == BoldMacro || (macro == ItalicMacro && italic == ItalicMode::Images))
				break;
			const auto total = GameUtf8::MacroLength(p + i);
			if (total == 0 || i + total > ShapingContext::MaxRunBytes)
				break;

			// <italic>'s argument must be a plain integer (1 on, 0 off). Anything else ends the run.
			if (macro == ItalicMacro) {
				int payload, n, on, onLength;
				if (!GameUtf8::TryReadInteger(p + i + 2, payload, n) || !GameUtf8::TryReadInteger(p + i + 2 + n, on, onLength) || onLength != payload)
					break;
				inItalic = on != 0;
			}

			if (decode)
				std::fill_n(ctx.ByteItalic.begin() + i, total, static_cast<uint8_t>(inItalic));
			i += total;
			continue;
		}

		// Stepped as the game does.
		const auto charLength = GameUtf8::CharacterLength(p + i);
		if (charLength == 0 || i + charLength > ShapingContext::MaxRunBytes)
			break;
		if (!decode) {
			i += charLength;
			continue;
		}

		std::fill_n(ctx.ByteItalic.begin() + i, charLength, static_cast<uint8_t>(inItalic));

		// Control characters and broken sequences shape as a replacement character, which keeps them per character.
		char32_t r = U'\xFFFD';
		const auto valid = b >= 0x20 && GameUtf8::TryDecode(p + i, charLength, r);
		if (!valid)
			r = U'\xFFFD';
		ctx.EnsureCapacity(count + 2);
		ctx.TextToByte[count] = valid ? i : -1;
		const auto units = static_cast<int>(xivres::util::unicode::encode(&ctx.Text[count], r));
		if (units == 2)
			ctx.TextToByte[count + 1] = -1;
		count += units;
		i += charLength;
	}

	length = i;
	return count;
}

void FontReplacement::Shaper::Shape(SizedFont& sized, const uint8_t* p, int count, std::span<GameGlyph*> glyphs, ItalicMode italic) {
	ClusterRun(sized, count, italic);
	PlaceClusters(p, glyphs);
	for (const auto& [b, owner] : m_ctx.Absorbed) {
		if (b >= 0 && owner >= 0)
			glyphs[b] = glyphs[owner] ? m_emptyGlyph : nullptr;
	}
}

void FontReplacement::Shaper::Measure(SizedFont& sized, int count, std::span<int> advances, ItalicMode italic) {
	ClusterRun(sized, count, italic);
	MeasureClusters(advances);
	for (const auto& [b, owner] : m_ctx.Absorbed) {
		if (b >= 0 && owner >= 0)
			advances[b] = advances[owner] == Missing ? Missing : 0;
	}
}

void FontReplacement::Shaper::ClusterRun(SizedFont& sized, int count, ItalicMode italic) {
	auto& ctx = m_ctx;
	ctx.Sized = &sized;
	ctx.Absorbed.clear();
	count = HangulComposer::Compose(ctx.Text, ctx.TextToByte, count, ctx.Absorbed);
	AssignSources(count);

	if (italic != ItalicMode::Images) {
		// A code unit with no byte of its own is in the italics of the one before it.
		for (auto t = 0; t < count; t++) {
			const auto b = ctx.TextToByte[t];
			ctx.TextItalic[t] = b >= 0 ? ctx.ByteItalic[b] : t > 0 ? ctx.TextItalic[t - 1] : italic != ItalicMode::Upright;
		}

		Collect(count, false);
	} else if (!CollectSpreadItalics(count)) {
		std::fill_n(ctx.TextItalic.begin(), count, static_cast<uint8_t>(0));
		Collect(count, true);
	}
}

void FontReplacement::Shaper::AssignSources(int count) {
	auto& ctx = m_ctx;
	char32_t runes[MaxGraphemeRunes];
	for (auto t = 0; t < count;) {
		const auto length = ctx.NextGrapheme(t, count);
		auto n = 0;
		for (auto u = t; u < t + length && n < MaxGraphemeRunes;) {
			const auto [rune, used] = ctx.DecodeAt(u, t + length).value_or(std::pair{U'\xFFFD', 1});
			runes[n++] = rune;
			u += used;
		}

		const auto source = ctx.Sized->Pick(std::span<const char32_t>(runes, n));

		// Codepoint replacements are written into the text in place, so only where the UTF-16 length stays the same.
		if (const auto element = source.Element; element && !element->DrawsGame()) {
			for (auto u = t; u < t + length;) {
				const auto decoded = ctx.DecodeAt(u, t + length);
				if (!decoded) {
					u++;
					continue;
				}

				const auto [rune, used] = *decoded;
				const auto drawn = element->GetDrawnCodepoint(rune);
				if (drawn != rune && xivres::util::unicode::encode<wchar_t>(nullptr, drawn) == static_cast<size_t>(used))
					xivres::util::unicode::encode(&ctx.Text[u], drawn);
				u += used;
			}
		}

		std::fill_n(ctx.TextSource.begin() + t, length, std::optional(source));
		t += length;
	}
}

float FontReplacement::Shaper::GetOwnWidth(IFaceSource& source, float px, std::span<const uint32_t> glyphs, std::span<const GlyphOffset> offsets) {
	std::vector<float> own(glyphs.size());
	for (size_t i = 0; i < glyphs.size(); i++)
		own[i] = source.GetAdvance(px, glyphs[i]);
	return source.GetScreenAdvance(px, {glyphs, own, offsets});
}

void FontReplacement::Shaper::AddMissing(int start, int end, float pen) {
	auto& ctx = m_ctx;
	const auto x = static_cast<int>(StdRound(pen));
	for (auto t = start; t < end;) {
		const auto length = ctx.NextGrapheme(t, end);
		ctx.Clusters.push_back({.X0 = x, .X1 = x, .TextStart = t, .TextEnd = t + length});
		t += length;
	}
}

void FontReplacement::Shaper::Collect(int count, bool italicImages) {
	auto& ctx = m_ctx;
	ctx.Clusters.clear();

	auto pen = 0.f;
	for (auto start = 0; start < count;) {
		const auto source = ctx.TextSource[start];
		const auto inItalic = ctx.TextItalic[start] != 0;
		auto end = start + 1;
		while (end < count && ctx.TextSource[end] == source && (ctx.TextItalic[end] != 0) == inItalic)
			end++;

		// Text left to the game needs no scripts.
		if (!source || !source->Element || !source->Element->Source()) {
			AddMissing(start, end, pen);
			start = end;
			continue;
		}

		m_analyzer.Itemize(std::span<const wchar_t>(ctx.Text.data() + start, end - start), ctx.TextRuns);
		for (const auto& run : ctx.TextRuns)
			CollectPiece(source, start + run.Start, start + run.End, run, inItalic, italicImages, pen);
		start = end;
	}
}

void FontReplacement::Shaper::CollectPiece(const std::optional<GlyphSource>& glyphSource, int start, int end, const TextRun& run, bool inItalic, bool italicImages, float& pen) {
	auto& ctx = m_ctx;
	const auto element = glyphSource ? glyphSource->Element : nullptr;
	auto source = element ? element->Source() : nullptr;
	if (source && ctx.Sized->Bold)
		source = source->GetBold();
	auto realItalic = false;
	if (source && inItalic)
		std::tie(source, realItalic) = source->GetItalic();
	if (!source || run.RightToLeft) {
		AddMissing(start, end, pen);
		return;
	}

	const auto px = element->GetPx(ctx.Sized->Px);
	for (auto position = start; position < end;) {
		ctx.Shaped.clear();
		int shapedLength;
		try {
			shapedLength = source->Shape(px, std::wstring_view(ctx.Text.data() + position, end - position), run.Script, ctx.Shaped);
		} catch (const std::exception& e) {
			Host::Error("Shaping with {} failed; the text is laid out per character: {}", element->Def().Lookup.FamilyName, e.what());
			ctx.Shaped.clear();
			shapedLength = 0;
		}

		for (const auto& shaped : ctx.Shaped)
			AddCluster(*glyphSource, source, px, shaped, position, realItalic, italicImages, pen);
		position += shapedLength;

		// The source lacks this grapheme.
		if (position < end) {
			const auto length = ctx.NextGrapheme(position, end);
			AddMissing(position, position + length, pen);
			position += length;
		}
	}
}

void FontReplacement::Shaper::AddCluster(const GlyphSource& glyphSource, IFaceSource* source, float px, const ShapedCluster& shaped, int textOffset, bool realItalic, bool italicImages, float& pen) {
	auto& ctx = m_ctx;
	const auto element = glyphSource.Element;
	std::vector glyphs(shaped.GlyphIds);
	std::vector offsets(shaped.Offsets);
	const auto textStart = textOffset + shaped.TextStart;
	const auto textEnd = textOffset + shaped.TextEnd;

	// What shaping moved the glyphs by besides their own advances, towards the next cluster. Only monospacing replaces the advances, keeping that.
	const auto& mono = element->Def().WrapModifiers.Monospacing;
	const auto monospaced = mono.is_enabled();
	auto kerning = monospaced ? source->GetScreenAdvance(px, {glyphs, shaped.Advances, offsets}) - GetOwnWidth(*source, px, glyphs, offsets) : 0.f;

	// In a node's italics, shaped upright, a glyph of one character is drawn with its font's italic, if it has one.
	if (italicImages && glyphs.size() == 1) {
		if (const auto decoded = ctx.DecodeAt(textStart, textEnd); decoded && decoded->second == textEnd - textStart) {
			if (const auto [italic, isReal] = source->GetItalic(); isReal) {
				if (const auto id = italic->GetGlyphId(decoded->first)) {
					source = italic;
					glyphs = {id};
					offsets.clear();
					realItalic = true;
				}
			}
		}
	}

	const auto width = source->GetScreenAdvance(px, {glyphs, shaped.Advances, offsets});
	const auto rawAdvance = monospaced ? static_cast<int>(StdRound(width - kerning)) : 0;

	// As in xivres's wrapping_fixed_size_font, a monospaced cluster that drops its kerning drops that of the cluster before it too.
	if (monospaced && mono.DropKerning && rawAdvance > 0) {
		kerning = 0;
		if (!ctx.Clusters.empty()) {
			auto& previous = ctx.Clusters.back();
			if (previous.TextEnd == textStart && previous.GlyphSource == glyphSource && previous.Kerning != 0) {
				pen -= previous.Kerning;
				previous.X1 = static_cast<int>(StdRound(pen));
				previous.Kerning = 0;
			}
		}
	}

	const auto x0 = StdRound(pen);
	FontReplacement::Cluster cluster{
		.Source = source,
		.Px = px,
		.GlyphIds = std::move(glyphs),
		.Advances = shaped.Advances,
		.Offsets = std::move(offsets),
		.RawAdvance = rawAdvance,
		.Kerning = kerning,
		.Origin = StdRound((pen - x0) * ShapingContext::SubpixelSteps) / ShapingContext::SubpixelSteps,
		.X0 = static_cast<int>(x0),
		.TextStart = textStart,
		.TextEnd = textEnd,
		.GlyphSource = glyphSource,
		.RealItalic = realItalic,
	};

	if (monospaced) {
		pen = static_cast<float>(cluster.X0 + m_cells.GetRaster(*ctx.Sized, cluster).Advance) + kerning;
		cluster.X1 = static_cast<int>(StdRound(pen));
	} else {
		const auto spacing = element->GetLetterSpacing(ctx.Sized->Px);
		cluster.X1 = static_cast<int>(StdRound(pen + width)) + spacing;
		pen += width + static_cast<float>(spacing);
	}

	ctx.Clusters.push_back(std::move(cluster));
}

bool FontReplacement::Shaper::CollectSpreadItalics(int count) {
	auto& ctx = m_ctx;
	std::fill_n(ctx.TextItalic.begin(), count, static_cast<uint8_t>(0));
	Collect(count, false);
	const auto upright = std::move(ctx.Clusters);
	std::fill_n(ctx.TextItalic.begin(), count, static_cast<uint8_t>(1));
	Collect(count, false);
	const auto italic = std::move(ctx.Clusters);
	ctx.Clusters.clear();

	const auto n = italic.size();
	if (n == 0 || n != upright.size())
		return false;
	for (size_t i = 0; i < n; i++) {
		const auto& u = upright[i];
		const auto& it = italic[i];
		if (u.TextStart != it.TextStart || u.TextEnd != it.TextEnd || u.Missing() || it.Missing())
			return false;
	}

	const auto share = static_cast<float>(upright[n - 1].X1 - italic[n - 1].X1) / static_cast<float>(n);
	for (size_t i = 0; i < n; i++)
		ctx.Clusters.push_back(italic[i].MovedBy(share * static_cast<float>(i), share * static_cast<float>(i + 1)));
	return true;
}

void FontReplacement::Shaper::PlaceClusters(const uint8_t* bytes, std::span<GameGlyph*> glyphs) {
	RasterizeConcurrently();
	auto& ctx = m_ctx;
	const auto& sized = *ctx.Sized;
	auto& clusters = ctx.Clusters;
	const auto count = clusters.size();
	std::vector<int> starts(count);
	for (size_t i = 0; i < count; i++) {
		auto& c = clusters[i];
		starts[i] = c.X0;
		if (c.Missing())
			continue;

		// A cluster after one left to the per-character path can't move into it.
		const auto& raster = m_cells.GetRaster(sized, c);
		if (const auto overhang = GlyphBox::InkLeftOfPen(raster); i > 0 && !clusters[i - 1].Missing() && overhang > 0)
			starts[i] = (std::max)(c.X0 - overhang, starts[i - 1]);
	}

	for (size_t i = 0; i < count; i++) {
		auto& c = clusters[i];

		// A ligature's text elements share its advance evenly, so that the caret and selection can stop inside it.
		ctx.ElementStarts.clear();
		for (auto t = c.TextStart; t < c.TextEnd; t += ctx.NextGrapheme(t, c.TextEnd))
			ctx.ElementStarts.push_back(t);
		const auto elements = static_cast<int>(ctx.ElementStarts.size());

		GameGlyph* cell = nullptr;
		auto advance = 0;
		if (!c.Missing()) {
			const auto end = i + 1 < count && !clusters[i + 1].Missing() ? starts[i + 1] : c.X1;
			advance = end - starts[i];
			cell = m_cells.GetCell(sized, c, bytes, ctx.TextToByte[c.TextStart], c.X0 - starts[i], Share(advance, elements, 0));
		}

		auto element = 0;
		for (auto t = c.TextStart; t < c.TextEnd; t++) {
			const auto startsElement = element + 1 < elements && ctx.ElementStarts[element + 1] == t;
			if (startsElement)
				element++;

			const auto b = ctx.TextToByte[t];
			if (b < 0)
				continue;
			glyphs[b] = !cell ? nullptr
				: t == c.TextStart ? cell
				: startsElement ? m_cells.GetSpacer(sized, Share(advance, elements, element))
				: m_emptyGlyph;
		}
	}
}

void FontReplacement::Shaper::MeasureClusters(std::span<int> advances) {
	auto& ctx = m_ctx;
	std::ranges::fill(advances, Missing);
	const auto margin = m_cells.GetEdgeMargin(*ctx.Sized);
	const auto& clusters = ctx.Clusters;
	const auto count = clusters.size();
	for (size_t i = 0; i < count; i++) {
		const auto& c = clusters[i];
		if (c.Missing())
			continue;
		const auto end = i + 1 < count && !clusters[i + 1].Missing() ? clusters[i + 1].X0 : c.X1;
		auto advance = (std::max)(end - c.X0, 0);

		// A cell that can't start earlier advances by its edge margin more (CellCache::GetCell), assumed of every such cluster.
		if (i == 0 || clusters[i - 1].Missing())
			advance += margin;

		ctx.ElementStarts.clear();
		for (auto t = c.TextStart; t < c.TextEnd; t += ctx.NextGrapheme(t, c.TextEnd))
			ctx.ElementStarts.push_back(t);
		const auto elements = static_cast<int>(ctx.ElementStarts.size());
		auto element = 0;
		for (auto t = c.TextStart; t < c.TextEnd; t++) {
			const auto startsElement = element + 1 < elements && ctx.ElementStarts[element + 1] == t;
			if (startsElement)
				element++;
			if (const auto b = ctx.TextToByte[t]; b >= 0)
				advances[b] = t == c.TextStart || startsElement ? Share(advance, elements, element) : 0;
		}
	}
}

void FontReplacement::Shaper::RasterizeConcurrently() {
	auto& ctx = m_ctx;
	const auto& sized = *ctx.Sized;
	m_pending.clear();
	m_seen.clear();
	for (auto& c : ctx.Clusters) {
		if (!c.Missing() && c.Source->RasterizesConcurrently() && m_cells.NeedsRaster(sized, c) && m_seen.insert(m_cells.KeyOf(sized, c)).second)
			m_pending.push_back(&c);
	}
	if (m_pending.size() < 2)
		return;

	// A cluster that fails is rasterized again by GetRaster, which reports it.
	std::vector<std::optional<RasterGlyph>> raws(m_pending.size());
	std::vector<uint8_t> failed(m_pending.size());
	std::vector<size_t> indices(m_pending.size());
	std::iota(indices.begin(), indices.end(), size_t{});
	std::for_each(std::execution::par, indices.begin(), indices.end(), [&](size_t i) {
		try {
			raws[i] = m_pending[i]->Rasterize();
		} catch (...) {
			failed[i] = 1;
		}
	});

	for (size_t i = 0; i < m_pending.size(); i++) {
		if (!failed[i])
			m_cells.AddRaster(sized, *m_pending[i], std::move(raws[i]));
	}
}
