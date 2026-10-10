#include "pch.h"
#include "MainApp/FontReplacement/FontReplacer.h"

#include "MainApp/FontReplacement/GameFontNames.h"
#include "MainApp/FontReplacement/GameLayout.h"
#include "MainApp/FontReplacement/GameUi.h"
#include "MainApp/FontReplacement/Host.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// Half px. Below 4 px nothing is legible; above 255 px the 255 px copy is scaled up by the renderer (byte-sized glyph fields, PlaceCell).
	constexpr int MinHalfPx = 8;
	constexpr int MaxHalfPx = 2 * 255;

	// A full atlas empties a plane of glyphs not drawn within KeepDrawnMs, at most every EvictIntervalMs, else overflowing text evicts every frame.
	constexpr uint64_t KeepDrawnMs = 2000;
	constexpr uint64_t EvictIntervalMs = 1000;

	// AXIS (most of the UI's text) has pages of the game's size.
	constexpr int AxisAtlasSize = 4096;
	constexpr int OtherAtlasSize = 2048;
}

thread_local FontReplacement::FontReplacer::LayoutContext FontReplacement::FontReplacer::s_current;

FontReplacement::FontReplacer::FontReplacer() {
	{
		uintptr_t pickFont = 0, getGlyph = 0, layOutCharacter = 0, buildFontCache = 0, freeFontCache = 0, buildFont = 0;
		GameLayout::Resolve("Font replacement", [&] {
			ResolveGameFontStructs();
			GameUi::ResolveUnits();
			GameUi::ResolveTextures();
			pickFont = GameLayout::Address("PickFont");
			getGlyph = GameLayout::Address("GetGlyph");
			layOutCharacter = GameLayout::Address("LayOutCharacter");
			buildFontCache = GameLayout::Address("BuildFontCache");
			freeFontCache = GameLayout::Address("FreeFontCache");
			buildFont = GameLayout::Address("BuildFont");
			m_rendererVtbl = GameLayout::Address("FontAnalyzerVtables", "AtkFontAnalyzerRenderer.Vtable");
			m_renderCountVtbl = GameLayout::Address("FontAnalyzerVtables", "AtkFontAnalyzerRenderCount.Vtable");
			m_fontCacheSlotOffset = GameLayout::Get("AtkTextNode.FontCacheSlot");
			m_fontCacheFlagsOffset = GameLayout::Get("AtkTextNode.FontCacheFlags");
			m_useFontCacheFlag = static_cast<uint8_t>(GameLayout::Get("AtkTextNode.FontCacheFlags.UseFontCache"));
			m_missingGlyphSubstitutes = GameLayout::GetList("LayOutCharacter.MissingGlyphSubstitutes");

			// Optional: without them, italics are the game's shear.
			m_stateFlagsOffset = GameLayout::TryGet("FontAnalyzerState.Flags").value_or(-1);
			m_setFlagsOffset = GameLayout::TryGet("GameFontSet.DrawFlags").value_or(-1);
			m_stateItalicFlag = static_cast<uint32_t>(GameLayout::Get("FontAnalyzerState.Flags.Italic"));
			m_setItalicFlag = static_cast<uint32_t>(GameLayout::Get("GameFontSet.DrawFlags.Italic"));

			if (getGlyph && layOutCharacter && GameLayout::Address("LayOutCharacter", "GetGlyph") != getGlyph)
				throw std::runtime_error("LayOutCharacter doesn't call the GetGlyph found.");
			if (const auto toggle = buildFontCache && freeFontCache ? GameLayout::Match("ToggleFontCache") : nullptr;
				toggle && (GameLayout::Target(*toggle, "BuildFontCache") != buildFontCache || GameLayout::Target(*toggle, "FreeFontCache") != freeFontCache))
				throw std::runtime_error("ToggleFontCache doesn't call the BuildFontCache and FreeFontCache found.");
		});
		GlyphAtlas::FirstTextureIndex = GetMaxGameFontTextureCount();

		m_rasterizer = std::make_unique<GlyphRasterizer>();
		m_builtInFace = ReplacementFace::CreateBuiltIn(*m_rasterizer);
		m_gameFace = ReplacementFace::CreateGame(*m_rasterizer, true, m_builtInFace.get());
		try {
			GameFontNames::Initialize();
		} catch (const std::exception& e) {
			Host::Warning("The game's font names weren't found; presets can't be applied: {}", e.what());
		}

		m_emptyGlyph = std::make_unique<GlyphSlot>();
		m_emptyGlyph->Glyph.Packed = GameGlyph::Pack(0, 0, 0, GlyphAtlas::FirstTextureIndex);
		m_shaper = std::make_unique<TextShaper>(*m_rasterizer, *this);
		m_freeFontCache = reinterpret_cast<void(*)(uintptr_t)>(freeFontCache);
		m_getGlyphHook.emplace("GetGlyph", getGlyph, [this](GameFontSet* set, uint32_t utf8Value, GameFont* font) { return GetGlyphDetour(set, utf8Value, font); });
		m_pickFontHook.emplace("PickFont", pickFont, [this](GameFontSet* set, uint8_t useCache) { return PickFontDetour(set, useCache); });
		m_buildFontCacheHook.emplace("BuildFontCache", buildFontCache, [this](uintptr_t node) { BuildFontCacheDetour(node); });
		m_layOutCharacterHook.emplace("LayOutCharacter", layOutCharacter, [this](uintptr_t analyzer, const uint8_t** text, uintptr_t state) { return LayOutCharacterDetour(analyzer, text, state); });
		m_buildFontHook.emplace("BuildFont", buildFont, [this](uintptr_t manager, uint16_t index) { return BuildFontDetour(manager, index); });

		// Caches built so far hold the game's glyphs. If anything above throws, the members undo the setup.
		UpdateFontCaches();
	}
}

FontReplacement::FontReplacer::~FontReplacer() {
	// While the hooks still exist: no font set may keep a copy as cached pick, nor a font cache a copy's glyphs; caches are rebuilt with game fonts.
	if (m_pickFontHook && m_getGlyphHook && m_buildFontCacheHook)
		SetEnabled(false);
	m_edgeShader.Set(false);
	m_buildFontHook.reset();
	m_layOutCharacterHook.reset();
	m_buildFontCacheHook.reset();
	m_pickFontHook.reset();
	m_getGlyphHook.reset();
	ForgetPickedFonts();

	for (const auto copy : m_copies | std::views::keys)
		std::free(copy);
	m_copies.clear();
	m_copiesByKey.clear();
	m_shaper.reset();
	FreeGlyphs();
	m_emptyGlyph.reset();

	m_atlases.clear();
	m_sizedFonts.clear();
	DisposePresetFaces();
	m_builtInFace.reset();
	m_rasterizer.reset();
}

void FontReplacement::FontReplacer::SetPreset(const Presets::Faces& preset, bool systemFallback) {
	m_shaper->ClearFormats();
	DisposePresetFaces();
	m_faceByFont.clear();
	m_sizedFonts.clear();
	m_gameFace = ReplacementFace::CreateGame(*m_rasterizer, systemFallback, m_builtInFace.get());
	for (const auto& [name, def] : preset) {
		try {
			m_presetFaces.emplace(name, std::make_unique<ReplacementFace>(*m_rasterizer, def, systemFallback, m_builtInFace.get()));
		} catch (const std::exception& e) {
			Host::Error("Setting up the face {} failed; its fonts use the game's glyphs: {}", name, e.what());
		}
	}

	for (auto& [copy, info] : m_copies) {
		info.Sized = GetSized(info.Original, info.HalfPx);
		Sync(copy, info);
	}

	RebuildGlyphs();
}

FontReplacement::ReplacementFace& FontReplacement::FontReplacer::GetFace(GameFont* original) {
	if (const auto it = m_faceByFont.find(original); it != m_faceByFont.end())
		return *it->second;

	auto face = m_gameFace.get();
	if (!m_presetFaces.empty()) {
		if (const auto name = GameFontNames::GetFaceName(original)) {
			if (const auto it = m_presetFaces.find(*name); it != m_presetFaces.end())
				face = it->second.get();
		}
	}
	m_faceByFont.emplace(original, face);
	return *face;
}

void FontReplacement::FontReplacer::DisposePresetFaces() {
	m_presetFaces.clear();
	m_gameFace.reset();
}

FontReplacement::SizedFont* FontReplacement::FontReplacer::GetSized(GameFont* original, int halfPx) {
	auto& face = GetFace(original);
	const auto fontName = GameFontNames::GetFaceName(original).value_or(std::format("0x{:X}", reinterpret_cast<uintptr_t>(original)));
	const auto key = std::make_tuple(&face, halfPx, fontName);
	auto it = m_sizedFonts.find(key);
	if (it == m_sizedFonts.end()) {
		const auto px = static_cast<float>(halfPx) / 2.f;
		const auto metrics = face.GetLineMetrics(px, original);
		it = m_sizedFonts.emplace(key, std::make_unique<SizedFont>(SizedFont{
			.Face = &face,
			.Px = px,
			.Ascent = metrics.Ascent,
			.LineHeight = metrics.LineHeight,
			.Game = original,
			.Atlas = GetAtlas(GameFontNames::FamilyOf(fontName)),
		})).first;
	}
	return it->second.get();
}

FontReplacement::GlyphAtlas* FontReplacement::FontReplacer::GetAtlas(const std::string& family) {
	if (const auto it = m_atlases.find(family); it != m_atlases.end())
		return it->second.get();

	auto atlas = std::make_unique<GlyphAtlas>(GetAtlasSize(family), family);
	const auto p = atlas.get();
	atlas->PageAdded = [this, p] { OnPageAdded(p); };
	m_atlases.emplace(family, std::move(atlas));

	// The renderer only has vertex buffers for texture indices below a font's texture count, so the first page must exist before any glyph.
	p->EnsurePage();
	return p;
}

void FontReplacement::FontReplacer::SetEdge(FontReplacementEdgeConfig edge) {
	edge = edge.Clamped();
	if (edge == m_edge)
		return;
	m_edge = edge;

	// Round edge of any width, set per font by its claimed texture width, unless edges are the game's own (1 px). Fallback: the game's fixed
	// pattern, widened only by claiming a narrower texture (blocky past ~1.5 px).
	try {
		m_edgeShader.Set(edge != FontReplacementEdgeConfig{});
	} catch (const std::exception& e) {
		Host::Error("Using the edge shader failed; edges are widened with the game's: {}", e.what());
		m_edgeShader.Set(false);
	}

	// Copies claim their texture width by the edge.
	for (const auto& [copy, info] : m_copies)
		Sync(copy, info);
	RebuildGlyphs();
}

int FontReplacement::FontReplacer::GetAtlasSize(std::string_view family) {
	return EqualsIgnoringCase(family, "AXIS") ? AxisAtlasSize : OtherAtlasSize;
}

uint16_t FontReplacement::FontReplacer::GetClaimedTextureWidth(const FontReplacementEdgeConfig& edge, int atlasSize, float px) {
	return static_cast<uint16_t>(std::clamp(std::round(static_cast<float>(atlasSize) / edge.GetWidth(px)), 256.f, 65535.f));
}

float FontReplacement::FontReplacer::GetDrawnPx(float size) {
	return static_cast<float>(std::clamp(static_cast<int>(std::round(size * 2)), MinHalfPx, MaxHalfPx)) / 2.f;
}

int FontReplacement::FontReplacer::GetEdgeMargin(const FontReplacementEdgeConfig& edge, float px) {
	return (std::max)(0, static_cast<int>(std::ceil(edge.GetWidth(px))) - 1);
}

FontReplacement::RasterGlyph FontReplacement::FontReplacer::FinishGlyph(const SizedFont& sized, RasterGlyph r) const {
	const auto m = GetEdgeMargin(m_edge, sized.Px);
	if (m == 0 || r.Width == 0)
		return r;
	const auto w = r.Width + 2 * m;
	const auto h = r.Height + 2 * m;
	std::vector<uint8_t> alpha(static_cast<size_t>(w) * h);
	for (auto y = 0; y < r.Height; y++)
		std::copy_n(&r.Alpha[static_cast<size_t>(y) * r.Width], r.Width, &alpha[static_cast<size_t>(y + m) * w + m]);
	return {r.Advance, r.Left - m, r.Top - m, w, h, std::move(alpha)};
}

void FontReplacement::FontReplacer::RebuildGlyphs() {
	m_shaper->Clear();
	m_fullAtlases.clear();
	m_gameReads.clear();
	for (auto& info : m_copies | std::views::values)
		info.GameGlyphs.clear();
	for (const auto& sized : m_sizedFonts | std::views::values)
		sized->Glyphs.clear();
	FreeGlyphs();
	for (const auto& atlas : m_atlases | std::views::values)
		atlas->Clear();
	ForgetPickedFonts();
	if (TextInvalidated)
		TextInvalidated();
}

void FontReplacement::FontReplacer::SetEnabled(bool value) {
	if (m_enabled == value)
		return;
	m_enabled = value;
	ForgetPickedFonts();
	UpdateFontCaches();
	if (TextInvalidated)
		TextInvalidated();
}

void FontReplacement::FontReplacer::ForgetPickedFonts() {
	const auto manager = GameFontManager::Instance();
	if (!manager || !manager->FontSets())
		return;
	for (auto i = 0; i < GameFontManager::FontSetCount; i++) {
		const auto set = manager->FontSet(i);
		set->CurrentFont() = nullptr;
		set->CurrentFontSize() = 0;
	}
}

void FontReplacement::FontReplacer::BuildFontCacheDetour(uintptr_t node) {
	if (m_enabled)
		m_freeFontCache(node);
	else
		m_buildFontCacheHook->Original(node);
}

void FontReplacement::FontReplacer::UpdateFontCaches() {
	for (const auto unit : GameUi::GetLoadedUnits())
		UpdateFontCaches(GameUi::GetUnitUld(unit), 0);
}

void FontReplacement::FontReplacer::UpdateFontCaches(uintptr_t uld, int depth) {
	if (depth > 16)
		return;
	const auto count = GameUi::GetNodeCount(uld);
	for (auto i = 0; i < count; i++) {
		const auto node = GameUi::GetNode(uld, i);
		if (!node)
			continue;
		const auto type = GameUi::GetNodeType(node);
		if (type == GameUi::TextNodeType) {
			if (m_enabled) {
				if (*reinterpret_cast<const uint16_t*>(node + m_fontCacheSlotOffset))
					m_freeFontCache(node);
			} else if (*reinterpret_cast<const uint8_t*>(node + m_fontCacheFlagsOffset) & m_useFontCacheFlag) {
				m_buildFontCacheHook->Original(node);
			}
		} else if (type >= GameUi::FirstComponentNodeType) {
			if (const auto componentUld = GameUi::GetComponentUld(node))
				UpdateFontCaches(componentUld, depth + 1);
		}
	}
}

int FontReplacement::FontReplacer::BuildFontDetour(uintptr_t manager, uint16_t index) {
	const auto result = m_buildFontHook->Original(manager, index);
	try {
		const auto fonts = reinterpret_cast<GameFontManager*>(manager);
		if (index < fonts->FontCount())
			OnFontBuilt(fonts->Font(index));
	} catch (const std::exception& e) {
		Host::Error("Refreshing the copies of a rebuilt font failed; disabling: {}", e.what());
		m_enabled = false;
		ForgetPickedFonts();
	}
	return result;
}

void FontReplacement::FontReplacer::OnFontBuilt(GameFont* font) {
	m_faceByFont.erase(font);
	auto any = false;
	for (auto& [copy, info] : m_copies) {
		if (info.Original != font)
			continue;
		info.Sized = GetSized(font, info.HalfPx);
		info.GameGlyphs.clear();
		Sync(copy, info);
		any = true;
	}

	if (any && TextInvalidated)
		TextInvalidated();
}

FontReplacement::GameFont* FontReplacement::FontReplacer::PickFontDetour(GameFontSet* set, uint8_t useCache) {
	auto font = m_pickFontHook->Original(set, useCache);
	if (!font)
		return font;

	// The cached pick may be an earlier copy: re-decide from its game font, as the picker's cache is keyed by a size copies aren't chosen by.
	if (const auto it = m_copies.find(font); it != m_copies.end())
		font = it->second.Original;

	if (!m_enabled)
		return font;

	// Leave fonts not built yet (the glyph map may be gone) or using the texture indices the atlas pages take.
	if (!font->IsReady() || font->TextureCount() > GlyphAtlas::FirstTextureIndex)
		return font;

	try {
		// On-screen size even where the game picked another (fixed font resolution node's unscaled size, or its own pick scale), so nothing is scaled.
		const auto size = set->ScaledSizeY();
		const auto halfPx = std::clamp(static_cast<int>(std::round(size * 2)), MinHalfPx, MaxHalfPx);
		const auto copy = GetOrCreateCopy(font, halfPx);

		// Rasterized at half-pixel sizes, but claim the exact size: any renderer scale other than exactly 1 resamples bilinearly (soft text), while
		// a quarter pixel size error doesn't show. Past the largest size the renderer scales up anyway.
		if (halfPx < MaxHalfPx && halfPx > MinHalfPx)
			copy->Size() = size;
		set->CurrentFont() = copy;
		return copy;
	} catch (const std::exception& e) {
		Host::Error("Replacing a font failed; disabling: {}", e.what());
		m_enabled = false;
		ForgetPickedFonts();
		return font;
	}
}

uintptr_t FontReplacement::FontReplacer::LayOutCharacterDetour(uintptr_t analyzer, const uint8_t** text, uintptr_t state) {
	const auto outer = s_current;
	const auto vtbl = *reinterpret_cast<const uintptr_t*>(analyzer);
	s_current = {
		.Character = *text,
		.MeasuringOnly = vtbl != m_rendererVtbl && vtbl != m_renderCountVtbl,
		.State = state,
	};
	const auto restore = xivres::util::on_dtor([&] {
		// The italic macro reads the bit when italics end (the glyph after moves by the font's italic correction).
		if (s_current.ItalicCleared)
			*reinterpret_cast<uint32_t*>(state + m_stateFlagsOffset) |= m_stateItalicFlag;
		s_current = outer;
	});
	return m_layOutCharacterHook->Original(analyzer, text, state);
}

FontReplacement::ItalicMode FontReplacement::FontReplacer::GetItalicMode(GameFontSet* set) const {
	if (m_stateFlagsOffset < 0 || !s_current.State || !(*reinterpret_cast<const uint32_t*>(s_current.State + m_stateFlagsOffset) & m_stateItalicFlag))
		return ItalicMode::Upright;
	return set && m_setFlagsOffset >= 0 && (*reinterpret_cast<const uint32_t*>(reinterpret_cast<uintptr_t>(set) + m_setFlagsOffset) & m_setItalicFlag)
		? ItalicMode::Images
		: ItalicMode::Real;
}

FontReplacement::GameGlyph* FontReplacement::FontReplacer::GetMissingGlyph(GameFontSet* set, uint32_t utf8Value, GameFont* font) {
	for (const auto substitute : m_missingGlyphSubstitutes) {
		if (utf8Value == static_cast<uint32_t>(substitute))
			return nullptr;
		if (const auto glyph = GetGlyphDetour(set, static_cast<uint32_t>(substitute), font))
			return glyph;
	}
	return nullptr;
}

FontReplacement::GameGlyph* FontReplacement::FontReplacer::GetGlyphDetour(GameFontSet* set, uint32_t utf8Value, GameFont* font) {
	const auto infoIt = m_copies.find(font);
	if (infoIt == m_copies.end())
		return m_getGlyphHook->Original(set, utf8Value, font);

	auto& info = infoIt->second;
	auto& sized = *info.Sized;

	// Laid out by LayOutCharacter at a known text position: use its shaped cluster's glyph, unless this is a lookup of the game's fallback characters.
	if (const auto p = s_current.Character; p && GameUtf8::PackSequence(p, GameUtf8::SequenceLength(*p)) == utf8Value) {
		try {
			const auto italic = GetItalicMode(set);
			if (const auto shaped = m_shaper->TryGetGlyph(sized, p, &m_emptyGlyph->Glyph, italic)) {
				// Real italics draw unsheared: clear the italic bit while laid out (quads are emitted after this lookup); LayOutCharacterDetour restores it.
				if (italic != ItalicMode::Upright && shaped != &m_emptyGlyph->Glyph && CellOf(shaped).RealItalic) {
					*reinterpret_cast<uint32_t*>(s_current.State + m_stateFlagsOffset) &= ~m_stateItalicFlag;
					s_current.ItalicCleared = true;
				}

				return WithPixels(shaped);
			}
		} catch (const std::exception& e) {
			Host::Error("Shaping failed; the character is laid out by itself: {}", e.what());
		}
	}

	auto it = sized.Glyphs.find(utf8Value);
	if (it == sized.Glyphs.end()) {
		GameGlyph* glyph;
		try {
			glyph = CreateGlyph(sized, utf8Value);
		} catch (const std::exception& e) {
			Host::Error("Rasterizing 0x{:X} at {} px failed: {}", utf8Value, sized.Px, e.what());
			glyph = nullptr;
		}
		it = sized.Glyphs.emplace(utf8Value, glyph).first;
	}

	if (it->second)
		return WithPixels(it->second);

	// Looked up in the game font, whose glyph map the copy borrows.
	const auto game = m_getGlyphHook->Original(set, utf8Value, info.Original);
	if (!game)
		return s_current.Character ? GetMissingGlyph(set, utf8Value, font) : nullptr;

	try {
		const auto own = GetGameGlyph(info, game, utf8Value);
		return own ? WithPixels(own) : game;
	} catch (const std::exception& e) {
		Host::Error("Copying the game's glyph 0x{:X} failed: {}", utf8Value, e.what());
		return game;
	}
}

FontReplacement::GameGlyph* FontReplacement::FontReplacer::GetGameGlyph(CopyInfo& info, GameGlyph* game, uint32_t utf8Value) {
	if (const auto it = info.GameGlyphs.find(utf8Value); it != info.GameGlyphs.end())
		return it->second;

	const auto original = info.Original;
	const auto textureIndex = game->TextureIndex();
	const auto texture = textureIndex < GlyphAtlas::FirstTextureIndex ? original->GetTexture(textureIndex) : 0;
	GameGlyph* glyph = nullptr;
	if (texture && GameTextureReader::CanRead(texture)) {
		const auto& sized = *info.Sized;
		const auto scale = sized.Px / original->Size();
		const auto top = game->OffsetY - original->Ascent();
		int left, t, w, h;
		RasterGlyph::ScaledBounds(0, top, game->Width, game->Height, scale, 0, 0, left, t, w, h);
		RasterGlyph r{static_cast<int>(std::round(static_cast<float>(game->Width + game->OffsetX) * scale)), left, t, w, h, std::vector<uint8_t>(static_cast<size_t>(w) * h)};
		const GameTextureSource source{texture, game->X(), game->Y(), game->Width, game->Height, game->Channel(), 0, top, scale};
		glyph = PlaceCell(sized, FinishGlyph(sized, std::move(r)), utf8Value, source);
	}

	info.GameGlyphs.emplace(utf8Value, glyph);
	return glyph;
}

void FontReplacement::FontReplacer::Upload() {
	m_frameTick = GetTickCount64();
	for (const auto glyph : m_gameReads) {
		auto& cell = CellOf(glyph);
		try {
			const auto& s = *cell.Source;
			auto pixels = m_gameTextures.Read(s.Texture, s.X, s.Y, s.Width, s.Height, s.Plane);
			cell.Raster = WithSourcePixels(cell.Raster, s, std::move(pixels));
			WriteCell(glyph, cell);
		} catch (const std::exception& e) {
			Host::Error("Reading a game glyph's pixels failed: {}", e.what());
		}

		cell.Raster = {};
		cell.Source.reset();
	}

	m_gameReads.clear();

	for (const auto atlas : std::vector(m_fullAtlases.begin(), m_fullAtlases.end())) {
		if (const auto it = m_lastEviction.find(atlas); it != m_lastEviction.end() && GetTickCount64() - it->second < EvictIntervalMs)
			continue;
		try {
			EvictPlane(atlas);
		} catch (const std::exception& e) {
			Host::Error("Making room in the {} glyph atlas failed; making every glyph again: {}", atlas->Name(), e.what());
			RebuildGlyphs();
			break;
		}
	}

	for (const auto& atlas : m_atlases | std::views::values)
		atlas->Upload();
}

FontReplacement::RasterGlyph FontReplacement::FontReplacer::WithSourcePixels(const RasterGlyph& box, const GameTextureSource& s, std::vector<uint8_t> sourceAlpha) {
	RasterGlyph scaled{0, s.Left, s.Top, s.Width, s.Height, std::move(sourceAlpha)};
	if (s.Scale != 1)
		scaled = scaled.Scaled(s.Scale, 0, 0);
	auto res = box;
	res.Alpha.assign(static_cast<size_t>(box.Width) * box.Height, 0);
	RasterGlyph::BlitMax(res.Alpha, box.Width, box.Height, scaled, scaled.Left - box.Left, scaled.Top - box.Top);
	return res;
}

void FontReplacement::FontReplacer::Sync(GameFont* copy, const CopyInfo& info) const {
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
	const auto width = GetClaimedTextureWidth(m_edge, sized.Atlas->Size(), sized.Px);
	copy->TextureWidth() = width;
	copy->TextureHeight() = width;
	ApplyPages(copy, *sized.Atlas);
}

FontReplacement::GameFont* FontReplacement::FontReplacer::GetOrCreateCopy(GameFont* original, int halfPx) {
	if (const auto it = m_copiesByKey.find({original, halfPx}); it != m_copiesByKey.end())
		return it->second;

	const auto copy = static_cast<GameFont*>(std::malloc(GameFont::StructSize));
	if (!copy)
		throw std::bad_alloc();
	CopyInfo info{original, halfPx, GetSized(original, halfPx), {}};
	Sync(copy, info);
	m_copies.emplace(copy, std::move(info));
	m_copiesByKey.emplace(std::make_pair(original, halfPx), copy);
	return copy;
}

void FontReplacement::FontReplacer::ApplyPages(GameFont* copy, const GlyphAtlas& atlas) {
	for (auto i = 0; i < atlas.PageCount(); i++)
		copy->SetTexture(GlyphAtlas::FirstTextureIndex + i, atlas.GetKernelTexture(i));
	copy->TextureCount() = static_cast<uint16_t>(GlyphAtlas::FirstTextureIndex + atlas.PageCount());
}

void FontReplacement::FontReplacer::OnPageAdded(GlyphAtlas* atlas) {
	for (const auto& [copy, info] : m_copies) {
		if (info.Sized->Atlas == atlas)
			ApplyPages(copy, *atlas);
	}
}

FontReplacement::GameGlyph* FontReplacement::FontReplacer::CreateGlyph(SizedFont& sized, uint32_t utf8Value) {
	const auto codepoint = GameUtf8::Unpack(utf8Value);

	// A broken sequence, or a codepoint the face leaves to the game, stays the game's.
	if (codepoint < 0)
		return nullptr;
	auto r = sized.Face->TryRasterize(static_cast<char32_t>(codepoint), sized.Px, sized.Game);
	return r ? PlaceCell(sized, FinishGlyph(sized, std::move(*r)), utf8Value) : nullptr;
}

FontReplacement::GameGlyph* FontReplacement::FontReplacer::PlaceCell(const SizedFont& sized, RasterGlyph r, uint32_t utf8Value, std::optional<GameTextureSource> source) {
	// The game has no left bearing (boxes start at the pen): overhanging ink moves right into the box; past the byte-sized width it's cut on the right.
	const auto boxLeft = (std::min)(r.Left, 0);
	const auto width = r.Width == 0 ? 0 : (std::min)(r.Left + r.Width - boxLeft, 255);

	// Boxes are a full line high like the game's: italics shift each quad's top edge by a line-height-based amount, slanting alike only if quads
	// span the same rows. Ink beyond the line (stacked marks, taller fallback scripts) grows the box, slanting a bit less; measuring uses the line height.
	const auto inkTop = sized.Ascent + r.Top;
	auto top = 0;
	auto height = sized.LineHeight;
	if (width != 0) {
		top = std::clamp(inkTop, -128, 0);
		height = (std::min)((std::max)(sized.LineHeight, inkTop + r.Height) - top, 255);
	}

	const auto glyph = AllocateGlyph();
	glyph->Utf8Value = utf8Value;
	glyph->Packed = GameGlyph::Pack(0, 0, 0, GlyphAtlas::FirstTextureIndex);
	glyph->Width = static_cast<uint8_t>(width);
	glyph->Height = static_cast<uint8_t>(height);
	glyph->OffsetX = static_cast<int8_t>(std::clamp(r.Advance - width, -128, 127));
	glyph->OffsetY = static_cast<int8_t>(top);

	// Text is measured far more than drawn (every change, at the unscaled size too): pixels wait here until drawn (WithPixels, which all glyphs pass).
	if (width != 0) {
		CellOf(glyph) = {
			.State = CellState::Pending,
			.Atlas = sized.Atlas,
			.Raster = std::move(r),
			.BoxLeft = boxLeft,
			.InkTop = inkTop - top,
			.Source = source,
		};
	}

	return glyph;
}

FontReplacement::GameGlyph* FontReplacement::FontReplacer::WithPixels(GameGlyph* glyph) {
	if (s_current.MeasuringOnly || glyph == &m_emptyGlyph->Glyph)
		return glyph;

	auto& cell = CellOf(glyph);
	if (cell.State == CellState::Placed) {
		cell.Drawn = m_frameTick;
		return glyph;
	}

	if (cell.State != CellState::Pending)
		return glyph;
	try {
		if (WritePixels(glyph, cell, m_frameTick))
			return glyph;
	} catch (const std::exception& e) {
		Host::Error("Placing a glyph in the atlas failed: {}", e.what());
		cell.State = CellState::None;
		cell.Raster = {};
		cell.Source.reset();
		DropPixels(glyph);
		return glyph;
	}

	m_fullAtlases.insert(cell.Atlas);
	return GetBlank(glyph);
}

bool FontReplacement::FontReplacer::WritePixels(GameGlyph* glyph, Cell& cell, uint64_t drawn) {
	int page, plane, x, y;
	if (!cell.Atlas->TryAllocate(glyph->Width, glyph->Height, page, plane, x, y))
		return false;

	glyph->Packed = GameGlyph::Pack(x, y, plane, GlyphAtlas::FirstTextureIndex + page);
	cell.State = CellState::Placed;
	cell.Drawn = drawn;
	if (cell.Source) {
		m_gameReads.push_back(glyph);
	} else {
		WriteCell(glyph, cell);
		cell.Raster = {};
	}
	return true;
}

FontReplacement::GameGlyph* FontReplacement::FontReplacer::GetBlank(GameGlyph* glyph) {
	auto& cell = CellOf(glyph);
	if (cell.Blank)
		return cell.Blank;
	const auto b = AllocateGlyph();
	*b = *glyph;
	DropPixels(b);
	cell.Blank = b;
	return b;
}

void FontReplacement::FontReplacer::EvictPlane(GlyphAtlas* atlas) {
	const auto now = GetTickCount64();
	const auto planes = static_cast<size_t>(GlyphAtlas::MaxPlanes());
	std::vector<int64_t> stale(planes);
	std::vector<uint64_t> oldest(planes, UINT64_MAX);
	for (const auto& [glyph, cell] : m_slots) {
		if (cell.State != CellState::Placed || cell.Atlas != atlas)
			continue;
		const auto plane = PlaneOf(&glyph);
		if (now - cell.Drawn > KeepDrawnMs)
			stale[plane] += GlyphAtlas::PaddedArea(glyph.Width, glyph.Height);
		oldest[plane] = (std::min)(oldest[plane], cell.Drawn);
	}

	auto victim = static_cast<int>(std::ranges::max_element(stale) - stale.begin());
	if (stale[victim] == 0)
		victim = static_cast<int>(std::ranges::min_element(oldest) - oldest.begin());

	struct Moved {
		GlyphSlot* Slot;
		uint64_t Drawn;
		std::vector<uint8_t> Pixels;
	};
	std::vector<Moved> moved;
	for (auto& slot : m_slots) {
		const auto glyph = &slot.Glyph;
		if (slot.Cell.State != CellState::Placed || slot.Cell.Atlas != atlas || PlaneOf(glyph) != victim)
			continue;
		moved.push_back({&slot, slot.Cell.Drawn, atlas->Read(glyph->TextureIndex() - GlyphAtlas::FirstTextureIndex, glyph->Channel(), glyph->X(), glyph->Y(), glyph->Width, glyph->Height)});
	}

	std::ranges::sort(moved, [](const Moved& a, const Moved& b) { return a.Drawn > b.Drawn; });
	atlas->ClearPlane(victim / GlyphAtlas::PlanesPerPage, victim % GlyphAtlas::PlanesPerPage);

	auto budget = stale[victim] == 0 ? static_cast<int64_t>(atlas->Size()) * atlas->Size() / 2 : INT64_MAX;
	auto kept = 0;
	for (auto& [slot, drawn, pixels] : moved) {
		const auto glyph = &slot->Glyph;
		auto& cell = slot->Cell;
		cell.State = CellState::Pending;
		cell.Raster = {0, 0, 0, glyph->Width, glyph->Height, std::move(pixels)};
		cell.BoxLeft = 0;
		cell.InkTop = 0;
		glyph->Packed = GameGlyph::Pack(0, 0, 0, GlyphAtlas::FirstTextureIndex);
		const auto area = GlyphAtlas::PaddedArea(glyph->Width, glyph->Height);
		if (now - drawn <= KeepDrawnMs && area <= budget && WritePixels(glyph, cell, drawn)) {
			budget -= area;
			kept++;
		}
	}

	m_fullAtlases.erase(atlas);
	m_lastEviction[atlas] = now;
	Host::Information("The {} glyph atlas was full: emptied plane {}, kept {} recently drawn glyphs of its {}", atlas->Name(), victim, kept, moved.size());
}

int FontReplacement::FontReplacer::PlaneOf(const GameGlyph* glyph) {
	return (glyph->TextureIndex() - GlyphAtlas::FirstTextureIndex) * GlyphAtlas::PlanesPerPage + glyph->Channel();
}

void FontReplacement::FontReplacer::WriteCell(const GameGlyph* glyph, const Cell& cell) {
	const auto& r = cell.Raster;

	// The ink's rows in the box (cut off only past the game's byte-sized fields).
	const auto firstRow = (std::max)(0, -cell.InkTop);
	const auto lastRow = (std::min)(r.Height, glyph->Height - cell.InkTop);
	if (lastRow > firstRow) {
		cell.Atlas->Write(
			glyph->TextureIndex() - GlyphAtlas::FirstTextureIndex,
			glyph->Channel(),
			glyph->X(),
			glyph->Y() + cell.InkTop + firstRow,
			glyph->Width,
			lastRow - firstRow,
			std::span(r.Alpha).subspan(static_cast<size_t>(firstRow) * r.Width, static_cast<size_t>(lastRow - firstRow) * r.Width),
			r.Width,
			r.Left - cell.BoxLeft);
	}
}

void FontReplacement::FontReplacer::DropPixels(GameGlyph* glyph) {
	glyph->OffsetX = static_cast<int8_t>(std::clamp(glyph->OffsetX + glyph->Width, -128, 127));
	glyph->Width = 0;
}

FontReplacement::GameGlyph* FontReplacement::FontReplacer::AllocateGlyph() {
	return &m_slots.emplace_back().Glyph;
}

void FontReplacement::FontReplacer::FreeGlyphs() {
	m_slots.clear();
}
