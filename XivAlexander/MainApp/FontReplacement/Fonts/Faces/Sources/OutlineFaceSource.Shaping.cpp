#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/OutlineFaceSource.h"

#include "MainApp/FontReplacement/Fonts/FontServices.h"
#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

int FontReplacement::OutlineFaceSource::Shape(float px, std::wstring_view text, const ScriptToken& script, std::vector<ShapedCluster>& clusters) {
	const auto length = static_cast<UINT32>(text.size());
	if (length == 0)
		return 0;

	const auto face = static_cast<IDWriteFontFace*>(m_font->GetFace(px));
	DWRITE_SCRIPT_ANALYSIS analysis{script.Script, static_cast<DWRITE_SCRIPT_SHAPES>(script.Shapes)};
	std::vector<uint16_t> clusterMap(length);
	std::vector<DWRITE_SHAPING_TEXT_PROPERTIES> textProps(length);
	const auto locale = xivres::util::unicode::convert<std::wstring>(m_def.Lookup.LanguageCode.empty() ? std::string("en-us") : m_def.Lookup.LanguageCode);

	const auto features = GetFeatures();
	DWRITE_TYPOGRAPHIC_FEATURES typographic{};
	const DWRITE_TYPOGRAPHIC_FEATURES* typographicPointer = &typographic;
	auto featureLength = length;
	if (features) {
		typographic.features = const_cast<DWRITE_FONT_FEATURE*>(features->data());
		typographic.featureCount = static_cast<UINT32>(features->size());
	}
	const auto featureSets = features ? &typographicPointer : nullptr;
	const auto featureLengths = features ? &featureLength : nullptr;
	const auto featureRanges = features ? 1u : 0u;

	auto& analyzer = m_services.Analyzer();
	auto maxGlyphs = length * 3 / 2 + 16;
	std::vector<uint16_t> glyphs;
	std::vector<DWRITE_SHAPING_GLYPH_PROPERTIES> glyphProps;
	UINT32 count;
	while (true) {
		glyphs.resize(maxGlyphs);
		glyphProps.resize(maxGlyphs);
		const auto hr = analyzer.GetGlyphs(
			text.data(), length, face, FALSE, FALSE, &analysis, locale.c_str(), nullptr, featureSets, featureLengths, featureRanges,
			maxGlyphs, clusterMap.data(), textProps.data(), glyphs.data(), glyphProps.data(), &count);
		if (hr == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER)) {
			maxGlyphs *= 2;
			continue;
		}
		ThrowOnError(hr, "GetGlyphs");
		break;
	}

	std::vector<float> advances(count);
	std::vector<DWRITE_GLYPH_OFFSET> offsets(count);

	// GDI measuring modes give hinted, whole-pixel advances, which the glyphs are drawn to match.
	if (m_measureMode == DWRITE_MEASURING_MODE_GDI_CLASSIC || m_measureMode == DWRITE_MEASURING_MODE_GDI_NATURAL) {
		ThrowOnError(analyzer.GetGdiCompatibleGlyphPlacements(
			text.data(), clusterMap.data(), textProps.data(), length, glyphs.data(), glyphProps.data(), count, face, px, 1, nullptr,
			m_measureMode == DWRITE_MEASURING_MODE_GDI_NATURAL, FALSE, FALSE, &analysis, locale.c_str(), featureSets, featureLengths, featureRanges,
			advances.data(), offsets.data()), "GetGdiCompatibleGlyphPlacements");
	} else {
		ThrowOnError(analyzer.GetGlyphPlacements(
			text.data(), clusterMap.data(), textProps.data(), length, glyphs.data(), glyphProps.data(), count, face, px, FALSE, FALSE, &analysis,
			locale.c_str(), featureSets, featureLengths, featureRanges, advances.data(), offsets.data()), "GetGlyphPlacements");
	}

	// Code units mapping to the same first glyph are a cluster, which ends where the next one's glyphs start.
	for (UINT32 k = 0; k < length;) {
		const auto glyphStart = clusterMap[k];
		auto k2 = k + 1;
		while (k2 < length && clusterMap[k2] == glyphStart)
			k2++;
		const auto glyphEnd = k2 < length ? clusterMap[k2] : count;

		ShapedCluster cluster{.TextStart = static_cast<int>(k), .TextEnd = static_cast<int>(k2)};
		for (auto i = glyphStart; i < glyphEnd; i++) {
			if (glyphs[i] == 0)
				return static_cast<int>(k);
			cluster.GlyphIds.push_back(glyphs[i]);
			cluster.Advances.push_back(advances[i]);
			if (const auto& o = offsets[i]; o.advanceOffset != 0 || o.ascenderOffset != 0) {
				cluster.Offsets.resize(glyphEnd - glyphStart);
				cluster.Offsets[i - glyphStart] = {o.advanceOffset, o.ascenderOffset};
			}
		}

		clusters.push_back(std::move(cluster));
		k = k2;
	}

	return static_cast<int>(length);
}

const std::vector<DWRITE_FONT_FEATURE>* FontReplacement::OutlineFaceSource::GetFeatures() {
	const auto& features = m_def.Lookup.Features;
	if (features.empty())
		return nullptr;
	if (m_features)
		return &*m_features;

	// DirectWrite turns these on for horizontal text only when it is given no features.
	auto& list = m_features.emplace();
	for (const auto tag : {
		     DWRITE_FONT_FEATURE_TAG_STANDARD_LIGATURES,
		     DWRITE_FONT_FEATURE_TAG_CONTEXTUAL_LIGATURES,
		     DWRITE_FONT_FEATURE_TAG_CONTEXTUAL_ALTERNATES,
		     DWRITE_FONT_FEATURE_TAG_KERNING,
	     }) {
		if (!features.contains(tag))
			list.push_back({tag, 1});
	}
	for (const auto& [tag, value] : features)
		list.push_back({tag, value});
	return &list;
}
