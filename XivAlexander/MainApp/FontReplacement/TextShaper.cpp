#include "pch.h"
#include "MainApp/FontReplacement/TextShaper.h"

#include <FontChanger.Presets/DirectWriteUtil.h>
#include <icu.h>

#include "MainApp/FontReplacement/FontReplacer.h"
#include "MainApp/FontReplacement/GameText.h"
#include "MainApp/FontReplacement/Host.h"
#include "MainApp/FontReplacement/Utilities.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// The <italic> macro.
	constexpr uint8_t ItalicMacro = 0x1A;

	constexpr size_t MaxActiveRuns = 8;
	constexpr size_t MaxCachedRuns = 8192;

	constexpr float SubpixelSteps = 4;

	// nullptr on systems without IDWriteFontFace3.
	IDWriteFontFace3Ptr AsFace3(IDWriteFontFace* face) {
		IDWriteFontFace3Ptr face3;
		if (FAILED(face->QueryInterface(__uuidof(IDWriteFontFace3), reinterpret_cast<void**>(&face3))))
			return nullptr;
		return face3;
	}

	int Share(int advance, int parts, int part) {
		return advance * (part + 1) / parts - advance * part / parts;
	}
}

int FontReplacement::GetNextTextElementLength(std::wstring_view text) {
	if (text.empty())
		return 0;

	thread_local std::unique_ptr<UBreakIterator, decltype(&ubrk_close)> s_iterator(nullptr, &ubrk_close);

	auto status = U_ZERO_ERROR;
	if (!s_iterator) {
		const auto iterator = ubrk_open(UBRK_CHARACTER, nullptr, nullptr, 0, &status);
		if (U_FAILURE(status))
			return 1;
		s_iterator.reset(iterator);
	}

	status = U_ZERO_ERROR;
	ubrk_setText(s_iterator.get(), reinterpret_cast<const UChar*>(text.data()), static_cast<int32_t>(text.size()), &status);
	if (U_FAILURE(status))
		return 1;
	const auto next = ubrk_following(s_iterator.get(), 0);
	return next == UBRK_DONE || next <= 0 ? static_cast<int>(text.size()) : next;
}

FontReplacement::TextShaper::Cluster FontReplacement::TextShaper::Cluster::MovedBy(float start, float end) const {
	const auto pen = static_cast<float>(X0) + Origin + start;
	const auto x0 = std::round(pen);
	auto res = *this;
	res.Key.reset();
	res.Origin = std::round((pen - x0) * SubpixelSteps) / SubpixelSteps;
	res.X0 = static_cast<int>(x0);
	res.X1 = static_cast<int>(std::round(static_cast<float>(X1) + end));
	return res;
}

void FontReplacement::TextShaper::ActiveRun::Set(const uint8_t* start, int length, int endBytes, const SizedFont* sized, ItalicMode italic, std::shared_ptr<const ShapedRun> shaped) {
	Start = start;
	End = start + length;
	Sized = sized;
	Italic = italic;
	Shaped = std::move(shaped);
	CompareLength = length + endBytes;
	Bytes.assign(start, start + CompareLength);
}

FontReplacement::ItalicMode FontReplacement::TextShaper::ActiveRun::ItalicAt(const uint8_t* p) const {
	if (!Shaped->ItalicBytes)
		return Italic;
	return (*Shaped->ItalicBytes)[p - Start] ? ItalicMode::Real : ItalicMode::Upright;
}

bool FontReplacement::TextShaper::ActiveRun::Matches(const uint8_t* p) const {
	for (auto i = static_cast<int>(p - Start); i < CompareLength; i++) {
		if (Start[i] != Bytes[i])
			return false;
	}
	return true;
}

FontReplacement::TextShaper::TextShaper(GlyphRasterizer& rasterizer, FontReplacer& replacer)
	: m_rasterizer(rasterizer)
	, m_replacer(replacer)
	, m_fallback(CreateSystemFallback(rasterizer)) {
}

FontReplacement::TextShaper::~TextShaper() = default;

void FontReplacement::TextShaper::ClearFormats() {
	m_formats.clear();
}

FontReplacement::GameGlyph* FontReplacement::TextShaper::TryGetGlyph(const SizedFont& sized, const uint8_t* p, GameGlyph* emptyGlyph, ItalicMode italic) {
	m_emptyGlyph = emptyGlyph;
	for (auto i = m_active.size(); i-- > 0;) {
		const auto& run = *m_active[i];
		if (run.Sized == &sized && p >= run.Start && p < run.End && run.ItalicAt(p) == italic && run.Matches(p))
			return run.Shaped->Glyphs[p - run.Start];
	}

	auto run = m_active.size() == MaxActiveRuns ? std::move(m_active.front()) : std::make_unique<ActiveRun>();
	const auto shaped = ShapeFrom(sized, p, italic, *run);
	if (m_active.size() == MaxActiveRuns)
		m_active.erase(m_active.begin());
	if (!shaped) {
		// ShapeFrom leaves the run untouched when it doesn't shape.
		if (run->Shaped)
			m_active.insert(m_active.begin(), std::move(run));
		return nullptr;
	}

	m_active.push_back(std::move(run));
	return m_active.back()->Shaped->Glyphs[0];
}

void FontReplacement::TextShaper::Clear() {
	m_active.clear();
	m_runs.clear();
	m_cells.clear();
	m_spacers.clear();
	m_rasters.clear();
}

void FontReplacement::TextShaper::OnGlyphRun(float baselineX, const DWRITE_GLYPH_RUN* run, const DWRITE_GLYPH_RUN_DESCRIPTION* description) {
	// The game draws in logical order: right-to-left runs stay per character, and break the line's clusters.
	if (!m_shapingGlyphs || !m_shapingSized)
		return;
	if (run->bidiLevel & 1) {
		m_clusterGap = true;
		return;
	}

	const auto clusterMap = description->clusterMap;
	const auto length = static_cast<int>(description->stringLength);
	const auto start = static_cast<int>(description->textPosition);
	auto pen = baselineX + m_penShift;
	for (auto k = 0; k < length;) {
		const auto glyphStart = clusterMap[k];
		auto k2 = k + 1;
		while (k2 < length && clusterMap[k2] == glyphStart)
			k2++;
		const auto glyphEnd = k2 < length ? clusterMap[k2] : static_cast<uint16_t>(run->glyphCount);
		const auto count = glyphEnd - glyphStart;

		// Characters the game draws, and those of elements not drawn from their fonts (merged glyphs, glyph images), stay per character.
		const auto& sized = *m_shapingSized;
		const auto element = m_textElement[start + k];
		const auto font = element ? element->Shaped() : nullptr;
		auto missing = element && !font;

		// Drawn from the element's own face (simulations, axis values); FreeType's emboldening advances glyphs further.
		auto face = font ? font->GetRunFace(run->fontFace, run->fontEmSize) : run->fontFace;
		Keep(face);
		const auto extra = font ? font->GetExtraAdvance(run->fontEmSize) : 0.f;
		std::vector<float> advances(count);
		auto advance = 0.f;
		auto layoutAdvance = 0.f;
		for (auto g = glyphStart; g < glyphEnd; g++) {
			advances[g - glyphStart] = run->glyphAdvances[g] + extra;
			advance += advances[g - glyphStart];
			layoutAdvance += run->glyphAdvances[g];
			missing |= run->glyphIndices[g] == 0;
		}

		// Real: a synthesized oblique is drawn upright for the game to shear. Images: a single character uses its font's italic
		// (if any) at the upright advance; the rest stay upright and get sheared.
		std::vector<uint16_t> glyphs(run->glyphIndices + glyphStart, run->glyphIndices + glyphEnd);
		std::optional<std::vector<DWRITE_GLYPH_OFFSET>> offsets;
		if (run->glyphOffsets)
			offsets.emplace(run->glyphOffsets + glyphStart, run->glyphOffsets + glyphEnd);
		auto realItalic = false;
		if (!missing && m_shapingItalic == ItalicMode::Real && m_textItalic[start + k]) {
			realItalic = IsRealItalic(face);
			if (!realItalic)
				face = GetUprightFace(face);
		} else if (!missing && m_shapingItalic == ItalicMode::Images && count == 1) {
			const auto c0 = m_text[start + k];
			std::optional<char32_t> codepoint;
			if (k2 - k == 1 && !(c0 >= 0xD800 && c0 <= 0xDFFF))
				codepoint = c0;
			else if (k2 - k == 2 && IS_HIGH_SURROGATE(c0) && IS_LOW_SURROGATE(m_text[start + k + 1]))
				codepoint = 0x10000 + ((c0 - 0xD800) << 10) + (m_text[start + k + 1] - 0xDC00);
			if (codepoint) {
				const auto italic = GetItalicFace(face);
				uint16_t index = 0;
				const auto cp = static_cast<UINT32>(*codepoint);
				if (italic)
					ThrowOnError(italic->GetGlyphIndices(&cp, 1, &index), "GetGlyphIndices");
				if (index != 0) {
					face = italic;
					glyphs = {index};
					offsets.reset();
					realItalic = true;
				}
			}
		}

		// The transformation scales advances; the cell (ReplacementFace::Wrap) widens them by monospacing and letter spacing.
		const auto x0 = std::round(pen);
		const auto width = advance * (font ? font->Transform().M11 : 1.f);
		const auto spacing = sized.Face->GetLetterSpacing(element && !element->DrawsGame() ? element : nullptr, sized.Px);
		Cluster cluster{
			.Face = face,
			.EmSize = run->fontEmSize,
			.Glyphs = std::move(glyphs),
			.Advances = std::move(advances),
			.Offsets = std::move(offsets),
			.Origin = std::round((pen - x0) * SubpixelSteps) / SubpixelSteps,
			.X0 = static_cast<int>(x0),
			.TextStart = start + k,
			.TextEnd = start + k2,
			.Element = element,
			.Missing = missing,
			.AfterGap = m_clusterGap,
			.RealItalic = realItalic,
		};

		// A monospaced cell is made now, as it gives the advance (from the whole pixel it starts at).
		float next;
		if (!missing && font && element->Def().WrapModifiers.Monospacing.is_enabled()) {
			const auto rawAdvance = static_cast<int>(std::round(width));
			cluster.Cell = sized.Face->Wrap(*element, RasterizeCluster(cluster, rawAdvance, 1), sized.Px, sized.Game, [&](float s) { return RasterizeCluster(cluster, rawAdvance, s); });
			cluster.X1 = cluster.X0 + cluster.Cell->Advance;
			next = static_cast<float>(cluster.X1);
		} else {
			cluster.X1 = static_cast<int>(std::round(pen + width)) + spacing;
			next = pen + width + static_cast<float>(spacing);
		}

		m_clusters.push_back(std::move(cluster));
		m_clusterGap = false;

		// DirectWrite moves the pen by its advances; the difference moves everything after along.
		m_penShift += next - pen - layoutAdvance;
		pen = next;
		k = k2;
	}
}

bool FontReplacement::TextShaper::IsRealItalic(IDWriteFontFace* face) {
	if (face->GetSimulations() & DWRITE_FONT_SIMULATIONS_OBLIQUE)
		return false;
	const auto face3 = AsFace3(face);
	return face3 && face3->GetStyle() != DWRITE_FONT_STYLE_NORMAL;
}

IDWriteFontFace* FontReplacement::TextShaper::GetUprightFace(IDWriteFontFace* face) {
	const auto simulations = face->GetSimulations();
	if (!(simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE))
		return face;
	if (const auto it = m_uprightFaces.find(face); it != m_uprightFaces.end())
		return it->second;

	auto upright = face;
	if (const auto face3 = AsFace3(face)) {
		if (IDWriteFontFaceReferencePtr reference; SUCCEEDED(face3->GetFontFaceReference(&reference))) {
			if (IDWriteFontFace3Ptr made; SUCCEEDED(reference->CreateFontFaceWithSimulations(static_cast<DWRITE_FONT_SIMULATIONS>(simulations & ~DWRITE_FONT_SIMULATIONS_OBLIQUE), &made)))
				upright = Keep(made);
		}
	}

	m_uprightFaces.emplace(face, upright);
	return upright;
}

IDWriteFontFace* FontReplacement::TextShaper::GetItalicFace(IDWriteFontFace* face) {
	if (const auto it = m_italicFaces.find(face); it != m_italicFaces.end())
		return it->second;

	IDWriteFontFace* italic = nullptr;
	if (const auto face3 = AsFace3(face)) {
		std::wstring name;
		if (IDWriteLocalizedStringsPtr names; SUCCEEDED(face3->GetFamilyNames(&names)))
			name = FontChanger::DirectWriteUtil::GetLocalizedString(names, {});

		if (!name.empty()) {
			if (const auto match = m_rasterizer.FindFont(xivres::util::unicode::convert<std::string>(name), face3->GetWeight(), face3->GetStretch(), DWRITE_FONT_STYLE_ITALIC)) {
				if (IDWriteFontFacePtr made; match->GetStyle() != DWRITE_FONT_STYLE_NORMAL && !(match->GetSimulations() & DWRITE_FONT_SIMULATIONS_OBLIQUE) && SUCCEEDED(match->CreateFontFace(&made)))
					italic = Keep(made);
			}
		}
	}

	m_italicFaces.emplace(face, italic);
	return italic;
}

IDWriteFontFace* FontReplacement::TextShaper::Keep(IDWriteFontFace* face) {
	m_keptFaces.try_emplace(face, face);
	return face;
}

void FontReplacement::TextShaper::PlaceClusters() {
	const auto count = m_clusters.size();
	std::vector<int> starts(count);
	for (size_t i = 0; i < count; i++) {
		auto& c = m_clusters[i];
		starts[i] = c.X0;
		if (c.Missing)
			continue;

		// A cluster after one left to the per-character path (or a gap) can't move into it.
		const auto& raster = GetRaster(c);
		if (i > 0 && !c.AfterGap && !m_clusters[i - 1].Missing && raster.Width > 0 && raster.Left < 0)
			starts[i] = (std::max)(c.X0 + raster.Left, starts[i - 1]);
	}

	for (size_t i = 0; i < count; i++) {
		auto& c = m_clusters[i];
		const std::wstring_view text(m_text.data(), m_text.size());

		// A ligature's text elements share its advance evenly so the caret can stop inside it; the first draws all of it.
		auto elements = 0;
		for (auto t = c.TextStart; t < c.TextEnd; t += (std::max)(1, GetNextTextElementLength(text.substr(t, c.TextEnd - t))))
			elements++;

		// A glyph the font lacks (.notdef) stays the per-character path's (the game's own font, at worst).
		GameGlyph* cell = nullptr;
		auto advance = 0;
		if (!c.Missing) {
			const auto end = i + 1 < count && !m_clusters[i + 1].Missing && !m_clusters[i + 1].AfterGap ? starts[i + 1] : c.X1;
			advance = end - starts[i];
			cell = GetCell(c, c.X0 - starts[i], Share(advance, elements, 0));
		}

		auto element = -1;
		auto nextElement = c.TextStart;
		for (auto t = c.TextStart; t < c.TextEnd; t++) {
			const auto startsElement = t == nextElement;
			if (startsElement) {
				element++;
				nextElement += (std::max)(1, GetNextTextElementLength(text.substr(t, c.TextEnd - t)));
			}

			const auto b = m_textToByte[t];
			if (b < 0)
				continue;
			(*m_shapingGlyphs)[b] = !cell ? nullptr
				: t == c.TextStart ? cell
				: startsElement ? GetSpacer(Share(advance, elements, element))
				: m_emptyGlyph;
		}
	}
}

FontReplacement::GameGlyph* FontReplacement::TextShaper::GetSpacer(int advance) {
	const auto& sized = *m_shapingSized;
	const auto key = std::make_tuple(static_cast<const ReplacementFace*>(sized.Face), sized.Px, advance);
	if (const auto it = m_spacers.find(key); it != m_spacers.end())
		return it->second;
	const auto cell = m_replacer.PlaceCell(sized, RasterGlyph{advance}, 0);
	m_spacers.emplace(key, cell);
	return cell;
}

IDWriteFontFallbackPtr FontReplacement::TextShaper::CreateSystemFallback(GlyphRasterizer& rasterizer) {
	const auto factory = rasterizer.Factory();
	IDWriteFontFallbackBuilderPtr builder;
	ThrowOnError(factory->CreateFontFallbackBuilder(&builder), "CreateFontFallbackBuilder");
	if (rasterizer.FindFont(IconFamily, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL)) {
		const DWRITE_UNICODE_RANGE pua{0xE000, 0xF8FF};
		const auto name = xivres::util::unicode::convert<std::wstring>(IconFamily);
		const wchar_t* names[]{name.c_str()};
		ThrowOnError(builder->AddMapping(&pua, 1, names, 1, nullptr, nullptr, nullptr, 1), "AddMapping");
	} else {
		Host::Warning("{} isn't installed; private-use characters are left to the game", IconFamily);
	}

	IDWriteFontFallbackPtr systemFallback;
	ThrowOnError(factory->GetSystemFontFallback(&systemFallback), "GetSystemFontFallback");
	ThrowOnError(builder->AddMappings(systemFallback), "AddMappings");
	IDWriteFontFallbackPtr result;
	ThrowOnError(builder->CreateFontFallback(&result), "CreateFontFallback");
	return result;
}

bool FontReplacement::TextShaper::ShapeFrom(const SizedFont& sized, const uint8_t* p, ItalicMode italic, ActiveRun& run) {
	// To the line end, as UTF-16 (byte offset -1 for low surrogates); italic macros switch italics mid-run so a line is laid
	// out whole, but end an ItalicMode::Images run.
	auto count = 0;
	auto i = 0;
	auto hash = 0xCBF29CE484222325ull;
	auto inItalic = italic != ItalicMode::Upright;
	while (i < MaxRunBytes) {
		const auto b = p[i];
		if (b == 0 || b == 0x0A || b == 0x0D)
			break;

		if (b == GameText::MacroStart) {
			// A line break macro ends the run.
			if (p[i + 1] == 0x10 || (p[i + 1] == ItalicMacro && italic == ItalicMode::Images))
				break;
			const auto total = GameText::MacroLength(p + i);
			if (total == 0 || i + total > MaxRunBytes)
				break;

			// The argument is a plain integer (1 on, 0 off); anything else ends the run.
			if (p[i + 1] == ItalicMacro) {
				int payload, on;
				const auto n = GameText::ReadInteger(p + i + 2, payload);
				if (payload == 0 || GameText::ReadInteger(p + i + 2 + n, on) != payload)
					break;
				inItalic = on != 0;
			}

			for (auto j = 0; j < total; j++)
				hash = (hash ^ p[i + j]) * 0x100000001B3ull;
			i += total;
			continue;
		}

		m_byteItalic[i] = inItalic ? 1 : 0;

		// Stepped as the game does; the run ends before a sequence that would run into the end of the text.
		const auto length = GameText::CharacterLength(p + i);
		if (length == 0 || i + length > MaxRunBytes)
			break;

		auto codepoint = -1;
		if (b >= 0x20)
			codepoint = GameUtf8::Decode(p + i, length);

		for (auto j = 0; j < length; j++)
			hash = (hash ^ p[i + j]) * 0x100000001B3ull;

		// Control characters and broken sequences shape as a replacement character, which keeps them per character.
		const auto r = codepoint < 0 ? U'\xFFFD' : static_cast<char32_t>(codepoint);
		EnsureCapacity(count + 2);
		wchar_t units[2];
		const auto unitCount = xivres::util::unicode::encode(units, r);
		m_text[count] = units[0];
		m_textToByte[count] = codepoint < 0 ? -1 : i;
		count++;
		if (unitCount == 2) {
			m_text[count] = units[1];
			m_textToByte[count] = -1;
			count++;
		}

		i += length;
	}

	if (i == 0)
		return false;

	// The terminating byte is compared too, as what follows could join the last cluster; a run cut at MaxRunBytes has none.
	const auto ended = i < MaxRunBytes;
	const auto key = std::make_tuple(hash, i, &sized, italic);
	auto it = m_runs.find(key);
	if (it == m_runs.end()) {
		auto shaped = std::make_shared<ShapedRun>();
		shaped->Glyphs.resize(i);
		if (italic != ItalicMode::Images)
			shaped->ItalicBytes.emplace(m_byteItalic, m_byteItalic + i);
		if (count != 0)
			Shape(sized, p, count, shaped->Glyphs, italic);
		if (m_runs.size() >= MaxCachedRuns)
			m_runs.clear();
		it = m_runs.emplace(key, std::move(shaped)).first;
	}

	run.Set(p, i, ended ? 1 : 0, &sized, italic, it->second);
	return true;
}

void FontReplacement::TextShaper::EnsureCapacity(int n) {
	if (static_cast<size_t>(n) <= m_text.size())
		return;
	const auto size = (std::max)(static_cast<size_t>(n), m_text.size() * 2);
	m_text.resize(size);
	m_textToByte.resize(size);
	m_textElement.resize(size);
	m_textItalic.resize(size);
}

void FontReplacement::TextShaper::Shape(const SizedFont& sized, const uint8_t* p, int count, std::vector<GameGlyph*>& glyphs, ItalicMode italic) {
	m_absorbed.clear();
	count = ComposeHangul(count);
	AssignElements(sized, count);

	// Each code unit is in italics as its character's byte is (a low surrogate or control character as the one before).
	for (auto t = 0; t < count; t++) {
		const auto b = m_textToByte[t];
		m_textItalic[t] = b >= 0 ? m_byteItalic[b] : t > 0 ? m_textItalic[t - 1] : italic != ItalicMode::Upright;
	}

	m_shapingBytes = p;
	m_shapingGlyphs = &glyphs;
	m_shapingSized = &sized;
	const auto cleanup = xivres::util::on_dtor([this] {
		m_shapingBytes = nullptr;
		m_shapingGlyphs = nullptr;
		m_shapingSized = nullptr;
	});

	if (italic != ItalicMode::Images)
		Collect(sized, count, ItalicMode::Real);
	else if (!CollectSpreadItalics(sized, count))
		Collect(sized, count, ItalicMode::Images);
	PlaceClusters();

	// A jamo composed into its syllable is part of the syllable's cluster.
	for (const auto& [b, owner] : m_absorbed) {
		if (b >= 0 && owner >= 0)
			glyphs[b] = glyphs[owner] ? m_emptyGlyph : nullptr;
	}
}

void FontReplacement::TextShaper::Collect(const SizedFont& sized, int count, ItalicMode italic) {
	// A face measuring as GDI does (hinted, whole-pixel advances) is laid out so; its glyphs are drawn to match.
	const auto measureMode = sized.Face->MeasureMode();
	const auto format = GetFormat(*sized.Face, sized.Px);
	IDWriteTextLayoutPtr layout;
	if (measureMode == DWRITE_MEASURING_MODE_GDI_CLASSIC || measureMode == DWRITE_MEASURING_MODE_GDI_NATURAL) {
		ThrowOnError(m_rasterizer.Factory()->CreateGdiCompatibleTextLayout(
			m_text.data(), static_cast<UINT32>(count), format, 1e6f, 1e6f, 1, nullptr, measureMode == DWRITE_MEASURING_MODE_GDI_NATURAL, &layout), "CreateGdiCompatibleTextLayout");
	} else {
		ThrowOnError(m_rasterizer.Factory()->CreateTextLayout(m_text.data(), static_cast<UINT32>(count), format, 1e6f, 1e6f, &layout), "CreateTextLayout");
	}

	ApplyElements(layout, sized, count);
	for (auto start = 0; italic == ItalicMode::Real && start < count;) {
		auto end = start + 1;
		while (end < count && m_textItalic[end] == m_textItalic[start])
			end++;
		if (m_textItalic[start])
			ThrowOnError(layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, {static_cast<UINT32>(start), static_cast<UINT32>(end - start)}), "SetFontStyle");
		start = end;
	}

	m_shapingItalic = italic;
	m_penShift = 0;
	m_clusters.clear();
	m_clusterGap = false;
	m_collector.Collect(layout, [this](float baselineX, const DWRITE_GLYPH_RUN* run, const DWRITE_GLYPH_RUN_DESCRIPTION* description) {
		OnGlyphRun(baselineX, run, description);
	});
}

bool FontReplacement::TextShaper::CollectSpreadItalics(const SizedFont& sized, int count) {
	Collect(sized, count, ItalicMode::Upright);
	const auto upright = std::move(m_clusters);
	std::fill_n(m_textItalic.begin(), count, static_cast<uint8_t>(1));
	Collect(sized, count, ItalicMode::Real);
	const auto italic = std::move(m_clusters);
	m_clusters.clear();

	const auto n = italic.size();
	if (n == 0 || n != upright.size())
		return false;
	for (size_t i = 0; i < n; i++) {
		const auto& u = upright[i];
		const auto& it = italic[i];
		if (u.TextStart != it.TextStart || u.TextEnd != it.TextEnd || u.Missing || it.Missing || u.AfterGap != it.AfterGap)
			return false;
	}

	// The difference is shared out after each cluster, the last ending where the upright run ends.
	const auto share = static_cast<float>(upright[n - 1].X1 - italic[n - 1].X1) / static_cast<float>(n);
	for (size_t i = 0; i < n; i++)
		m_clusters.push_back(italic[i].MovedBy(share * static_cast<float>(i), share * static_cast<float>(i + 1)));
	return true;
}

int FontReplacement::TextShaper::ComposeHangul(int count) {
	constexpr int SBase = 0xAC00, LBase = 0x1100, VBase = 0x1161, TBase = 0x11A7;
	constexpr int LCount = 19, VCount = 21, TCount = 28, SCount = LCount * VCount * TCount;

	auto w = 0;
	for (auto i = 0; i < count;) {
		int c = m_text[i];
		auto n = 1;
		if (c - LBase >= 0 && c - LBase < LCount && i + 1 < count && m_text[i + 1] - VBase >= 0 && m_text[i + 1] - VBase < VCount) {
			c = SBase + (((c - LBase) * VCount) + (m_text[i + 1] - VBase)) * TCount;
			n = 2;
		}

		if ((n == 2 || (c - SBase >= 0 && c - SBase < SCount && (c - SBase) % TCount == 0)) &&
			i + n < count && m_text[i + n] - TBase > 0 && m_text[i + n] - TBase < TCount) {
			c += m_text[i + n] - TBase;
			n++;
		}

		for (auto j = 1; j < n; j++)
			m_absorbed.emplace_back(m_textToByte[i + j], m_textToByte[i]);
		m_text[w] = static_cast<wchar_t>(c);
		m_textToByte[w] = m_textToByte[i];
		w++;
		i += n;
	}
	return w;
}

void FontReplacement::TextShaper::GetBreaks(std::wstring_view text, std::span<uint8_t> clusterEnd, std::span<uint8_t> wrapAfter) {
	std::ranges::fill(clusterEnd, 0);
	std::ranges::fill(wrapAfter, 0);
	if (text.empty())
		return;

	// Clusters and break opportunities don't depend on the size (fallback fonts are picked the same way at any).
	const auto format = GetFormat(m_replacer.BuiltInFace(), 16);
	IDWriteTextLayoutPtr layout;
	ThrowOnError(m_rasterizer.Factory()->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format, 1e6f, 1e6f, &layout), "CreateTextLayout");
	UINT32 count = 0;
	layout->GetClusterMetrics(nullptr, 0, &count);
	std::vector<DWRITE_CLUSTER_METRICS> metrics(count);
	ThrowOnError(layout->GetClusterMetrics(metrics.data(), count, &count), "GetClusterMetrics");

	size_t position = 0;
	for (const auto& cluster : metrics) {
		position += cluster.length;
		clusterEnd[position] = 1;
		wrapAfter[position] = cluster.canWrapLineAfter ? 1 : 0;
	}
}

void FontReplacement::TextShaper::AssignElements(const SizedFont& sized, int count) {
	const auto face = sized.Face;
	const auto game = sized.Game;
	for (auto i = 0; i < count;) {
		const auto n = IS_HIGH_SURROGATE(m_text[i]) && i + 1 < count && IS_LOW_SURROGATE(m_text[i + 1]) ? 2 : 1;
		const auto codepoint = n == 2 ? static_cast<char32_t>(0x10000 + ((m_text[i] - 0xD800) << 10) + (m_text[i + 1] - 0xDC00)) : static_cast<char32_t>(m_text[i]);
		const auto element = face->GetElement(codepoint, game);
		if (element && !element->DrawsGame()) {
			const auto drawn = ReplacementFace::GetDrawnCodepoint(*element, codepoint);
			if (drawn != codepoint && xivres::util::unicode::encode<wchar_t>(nullptr, drawn) == static_cast<size_t>(n))
				xivres::util::unicode::encode(&m_text[i], drawn);
		}

		for (auto j = 0; j < n; j++)
			m_textElement[i + j] = element;
		i += n;
	}
}

void FontReplacement::TextShaper::ApplyElements(IDWriteTextLayout* layout, const SizedFont& sized, int count) {
	const auto& face = *sized.Face;
	for (auto start = 0; start < count;) {
		const auto element = m_textElement[start];
		auto end = start + 1;
		while (end < count && m_textElement[end] == element)
			end++;

		// Elements not drawn from their fonts are left per character anyway.
		if (const auto font = element ? element->Shaped() : nullptr)
			font->ApplyTo(layout, {static_cast<UINT32>(start), static_cast<UINT32>(end - start)}, face.GetElementPx(*element, sized.Px));

		start = end;
	}
}

IDWriteTextFormat1* FontReplacement::TextShaper::GetFormat(const ReplacementFace& face, float px) {
	const auto key = std::make_pair(&face, static_cast<int>(std::round(px * 2)));
	if (const auto it = m_formats.find(key); it != m_formats.end())
		return it->second;

	const auto primary = face.Primary();
	FontChanger::Structs::LookupStruct segoe;
	segoe.Name = "Segoe UI";
	const auto& lookup = primary ? primary->Def().Lookup : segoe;
	auto format = m_rasterizer.CreateFormat(
		lookup.Name,
		lookup.Weight,
		lookup.Style,
		lookup.Stretch,
		primary ? face.GetElementPx(*primary, px) : px,
		face.SystemFallback() ? m_fallback.GetInterfacePtr() : m_rasterizer.NoFallback());
	return m_formats.emplace(key, std::move(format)).first->second;
}

const FontReplacement::RasterGlyph& FontReplacement::TextShaper::GetRaster(Cluster& c) {
	const auto& sized = *m_shapingSized;
	if (!c.Key) {
		RasterKey key{
			.Face = sized.Face,
			.Px = sized.Px,
			.Element = c.Element,
			.FontFace = c.Face,
			.EmSize = c.EmSize,
			.Origin = c.Origin,
			.Glyphs = c.Glyphs,
			.Advances = c.Advances,
			.HasOffsets = c.Offsets.has_value(),
		};
		if (c.Offsets) {
			for (const auto& o : *c.Offsets)
				key.Offsets.emplace_back(o.advanceOffset, o.ascenderOffset);
		}
		c.Key = std::move(key);
	}

	if (const auto it = m_rasters.find(*c.Key); it != m_rasters.end())
		return it->second;

	auto raster = c.Cell ? *c.Cell : RasterizeCluster(c, 0, 1);
	if (!c.Cell && c.Element)
		raster = sized.Face->Wrap(*c.Element, std::move(raster), sized.Px, sized.Game, [&](float s) { return RasterizeCluster(c, 0, s); });

	// An edge margin makes ink left of the pen: the cell then starts earlier (PlaceClusters), keeping the spacing.
	raster = m_replacer.FinishGlyph(sized, std::move(raster));
	return m_rasters.emplace(*c.Key, std::move(raster)).first->second;
}

FontReplacement::RasterGlyph FontReplacement::TextShaper::RasterizeCluster(const Cluster& c, int advance, float squeezeX) {
	return m_shapingSized->Face->RasterizeRun(
		c.Element, c.Face, c.EmSize, c.Glyphs.data(), c.Advances.data(), c.Offsets ? c.Offsets->data() : nullptr, static_cast<uint32_t>(c.Glyphs.size()), c.Origin, advance, squeezeX);
}

FontReplacement::GameGlyph* FontReplacement::TextShaper::GetCell(Cluster& c, int pad, int advance) {
	const auto& sized = *m_shapingSized;
	const auto& raster = GetRaster(c);
	const auto key = std::make_tuple(*c.Key, pad, advance);
	if (const auto it = m_cells.find(key); it != m_cells.end())
		return it->second;

	auto placed = raster;
	placed.Advance = advance;
	placed.Left += pad;
	const auto b = m_textToByte[c.TextStart];
	const auto utf8 = b < 0 ? 0u : GameUtf8::PackSequence(m_shapingBytes + b, GameUtf8::SequenceLength(m_shapingBytes[b]));
	const auto cell = m_replacer.PlaceCell(sized, std::move(placed), utf8);
	if (c.RealItalic)
		m_replacer.MarkRealItalic(cell);
	m_cells.emplace(key, cell);
	return cell;
}
