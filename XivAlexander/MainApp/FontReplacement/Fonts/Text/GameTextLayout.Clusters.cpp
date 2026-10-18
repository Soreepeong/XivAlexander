#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Text/GameTextLayout.h"

#include "MainApp/FontReplacement/Fonts/Faces/IFaceSource.h"
#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFontElement.h"
#include "MainApp/FontReplacement/Fonts/Text/DirectWriteTextAnalyzer.h"

// Clusters are placed one after another by their advances on screen (with the elements' transformations, emboldening, letter spacing and
// monospacing), not where shaping put them.

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

void FontReplacement::GameTextLayout::Collect(int count, bool italicImages) {
	m_clusters.clear();
	m_analyzer.Itemize(std::span<const wchar_t>(m_text.data(), count), m_textRuns);

	auto pen = 0.f;
	for (const auto& run : m_textRuns) {
		for (auto start = run.Start; start < run.End;) {
			const auto& source = m_textSource[start];
			const auto inItalic = m_textItalic[start] != 0;
			auto end = start + 1;
			while (end < run.End && m_textSource[end] == source && (m_textItalic[end] != 0) == inItalic)
				end++;

			CollectPiece(source, start, end, run, inItalic, italicImages, pen);
			start = end;
		}
	}
}

void FontReplacement::GameTextLayout::CollectPiece(const std::optional<GlyphSource>& glyphSource, int start, int end, const TextRun& run, bool inItalic, bool italicImages, float& pen) {
	const auto element = glyphSource ? glyphSource->Element : nullptr;
	auto source = element ? element->Source() : nullptr;
	auto realItalic = false;
	if (source && inItalic)
		std::tie(source, realItalic) = source->GetItalic();
	if (!source || run.RightToLeft) {
		AddMissing(start, end, pen);
		return;
	}

	const auto px = element->GetPx(m_shapingSized->Px);
	for (auto position = start; position < end;) {
		m_shaped.clear();
		int shapedLength;
		try {
			shapedLength = source->Shape(px, std::wstring_view(m_text.data() + position, end - position), run.Script, m_shaped);
		} catch (const std::exception& e) {
			Host::Error("Shaping with {} failed; the text is laid out per character: {}", element->Def().Lookup.FamilyName, e.what());
			m_shaped.clear();
			shapedLength = 0;
		}

		for (const auto& shaped : m_shaped)
			AddCluster(*glyphSource, source, px, shaped, position, realItalic, italicImages, pen);
		position += shapedLength;

		// The source lacks this grapheme.
		if (position < end) {
			const auto length = NextGrapheme(position, end);
			AddMissing(position, position + length, pen);
			position += length;
		}
	}
}

void FontReplacement::GameTextLayout::AddCluster(const GlyphSource& glyphSource, IFaceSource* source, float px, const ShapedCluster& shaped, int textOffset, bool realItalic, bool italicImages, float& pen) {
	const auto element = glyphSource.Element;
	std::vector glyphs(shaped.GlyphIds);
	std::vector offsets(shaped.Offsets);
	const auto textStart = textOffset + shaped.TextStart;
	const auto textEnd = textOffset + shaped.TextEnd;

	// What shaping moved the glyphs by besides their own advances, towards the next cluster.
	auto kerning = source->GetScreenAdvance(px, {glyphs, shaped.Advances, offsets}) - GetOwnWidth(*source, px, glyphs, offsets);

	// In a node's italics shaped upright, only a glyph of one character can take its italic, which is looked up by codepoint.
	if (italicImages && glyphs.size() == 1) {
		if (const auto decoded = DecodeAt(textStart, textEnd); decoded && decoded->second == textEnd - textStart) {
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
	const auto& mono = element->Def().WrapModifiers.Monospacing;
	const auto monospaced = mono.is_enabled();
	const auto rawAdvance = monospaced ? static_cast<int>(StdRound(width - kerning)) : 0;

	// As in xivres's wrapping_fixed_size_font, a monospaced cluster that drops its kerning drops that of the cluster before it too.
	if (monospaced && mono.DropKerning && rawAdvance > 0) {
		kerning = 0;
		if (!m_clusters.empty()) {
			auto& previous = m_clusters.back();
			if (previous.TextEnd == textStart && previous.GlyphSource == glyphSource && previous.Kerning != 0) {
				pen -= previous.Kerning;
				previous.X1 = static_cast<int>(StdRound(pen));
				previous.Kerning = 0;
			}
		}
	}

	const auto x0 = StdRound(pen);
	Cluster cluster{
		.Source = source,
		.Px = px,
		.GlyphIds = std::move(glyphs),
		.Advances = shaped.Advances,
		.Offsets = std::move(offsets),
		.RawAdvance = rawAdvance,
		.Kerning = kerning,
		.Origin = StdRound((pen - x0) * SubpixelSteps) / SubpixelSteps,
		.X0 = static_cast<int>(x0),
		.TextStart = textStart,
		.TextEnd = textEnd,
		.GlyphSource = glyphSource,
		.RealItalic = realItalic,
	};

	if (monospaced) {
		pen = static_cast<float>(cluster.X0 + GetRaster(cluster).Advance) + kerning;
		cluster.X1 = static_cast<int>(StdRound(pen));
	} else {
		const auto spacing = element->GetLetterSpacing(m_shapingSized->Px);
		cluster.X1 = static_cast<int>(StdRound(pen + width)) + spacing;
		pen += width + static_cast<float>(spacing);
	}

	m_clusters.push_back(std::move(cluster));
}

float FontReplacement::GameTextLayout::GetOwnWidth(IFaceSource& source, float px, std::span<const uint32_t> glyphs, std::span<const GlyphOffset> offsets) {
	std::vector<float> own(glyphs.size());
	for (size_t i = 0; i < glyphs.size(); i++)
		own[i] = source.GetAdvance(px, glyphs[i]);
	return source.GetScreenAdvance(px, {glyphs, own, offsets});
}

void FontReplacement::GameTextLayout::AddMissing(int start, int end, float pen) {
	const auto x = static_cast<int>(StdRound(pen));
	for (auto t = start; t < end;) {
		const auto length = NextGrapheme(t, end);
		m_clusters.push_back({.X0 = x, .X1 = x, .TextStart = t, .TextEnd = t + length});
		t += length;
	}
}

bool FontReplacement::GameTextLayout::CollectSpreadItalics(int count) {
	std::fill_n(m_textItalic.begin(), count, static_cast<uint8_t>(0));
	Collect(count, false);
	const auto upright = std::move(m_clusters);
	std::fill_n(m_textItalic.begin(), count, static_cast<uint8_t>(1));
	Collect(count, false);
	const auto italic = std::move(m_clusters);
	m_clusters.clear();

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
		m_clusters.push_back(italic[i].MovedBy(share * static_cast<float>(i), share * static_cast<float>(i + 1)));
	return true;
}

FontReplacement::GameTextLayout::Cluster FontReplacement::GameTextLayout::Cluster::MovedBy(float start, float end) const {
	const auto pen = static_cast<float>(X0) + Origin + start;
	const auto x0 = StdRound(pen);
	auto res = *this;
	res.Key.reset();
	res.Origin = StdRound((pen - x0) * SubpixelSteps) / SubpixelSteps;
	res.X0 = static_cast<int>(x0);
	res.X1 = static_cast<int>(StdRound(static_cast<float>(X1) + end));
	return res;
}
