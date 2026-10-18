#include "pch.h"
#include "MainApp/FontReplacement/Fonts/FontReplacer.h"

#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFont.h"
#include "MainApp/FontReplacement/Fonts/Text/GameTextLayout.h"
#include "MainApp/FontReplacement/Fonts/Text/SystemFallbackFonts.h"
#include "MainApp/FontReplacement/Game/FontNames.h"
#include "MainApp/FontReplacement/GameCode.h"
#include "MainApp/FontReplacement/GameUi.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

thread_local FontReplacement::FontReplacer::LayoutContext FontReplacement::FontReplacer::s_current;

FontReplacement::FontReplacer::FontReplacer() {
	ResolveGameFontStructs();
	GameUi::ResolveUnits();
	GameUi::ResolveTextures();
	const auto set = ResolveGameCode(Game::Resolved::Fonts::FontSet);
	const auto font = ResolveGameCode(Game::Resolved::Fonts::Font);
	const auto functions = ResolveGameCode(Game::Resolved::Fonts::Replacer);
	const auto pickFont = reinterpret_cast<uintptr_t>(set.PickFont);
	const auto getGlyph = reinterpret_cast<uintptr_t>(font.GetGlyph);
	const auto layOutCharacter = reinterpret_cast<uintptr_t>(font.LayOutCharacter);
	const auto buildFontCache = reinterpret_cast<uintptr_t>(functions.BuildFontCache);
	const auto freeFontCache = reinterpret_cast<uintptr_t>(functions.FreeFontCache);
	const auto buildFont = reinterpret_cast<uintptr_t>(font.BuildFont);
	const auto unloadFonts = reinterpret_cast<uintptr_t>(font.UnloadFont);
	m_mainThreadId = GetCurrentThreadId();
	m_rendererVtbl = reinterpret_cast<uintptr_t>(functions.RendererVtable);
	m_renderCountVtbl = reinterpret_cast<uintptr_t>(functions.RenderCountVtable);
	m_fontCacheSlotOffset = functions.FontCacheSlot;
	m_fontCacheFlagsOffset = functions.FontCacheFlags;
	m_useFontCacheFlag = 0x40;

	// Optional: without them, italics are the game's shear.
	m_stateFlagsOffset = functions.StateFlags.value_or(-1);
	m_setFlagsOffset = set.DrawFlags.value_or(-1);
	m_stateItalicFlag = 0x08000000;
	m_setItalicFlag = 0x08000000;

	m_services = std::make_unique<FontServices>();
	m_fallbackFonts = std::make_unique<SystemFallbackFonts>(*m_services);
	m_gameFont = MaterializedFont::CreateGame(*m_services, true, *m_fallbackFonts);
	try {
		FontNames::Initialize();
	} catch (const std::exception& e) {
		Host::Warning("The game's font names weren't found; presets can't be applied: {}", e.what());
	}

	m_blankTexture.emplace(BlankTextureSize, BlankTextureSize);
	m_emptyGlyph = std::make_unique<GlyphSlot>();
	m_emptyGlyph->Glyph.Packed = GameGlyph::Pack(0, 0, 0, 0);
	m_layout = std::make_unique<GameTextLayout>(*m_services, *this, &m_emptyGlyph->Glyph);
	m_freeFontCache = reinterpret_cast<void(*)(uintptr_t)>(freeFontCache);
	m_getGlyphHook.emplace("GetGlyph", getGlyph, [this](GameFontSet* set, uint32_t utf8Value, GameFont* font) { return GetGlyphDetour(set, utf8Value, font); });
	m_pickFontHook.emplace("PickFont", pickFont, [this](GameFontSet* set, uint8_t useCache) { return PickFontDetour(set, useCache); });
	m_buildFontCacheHook.emplace("BuildFontCache", buildFontCache, [this](uintptr_t node) { BuildFontCacheDetour(node); });
	m_layOutCharacterHook.emplace("LayOutCharacter", layOutCharacter, [this](uintptr_t analyzer, const uint8_t** text, uintptr_t state) { return LayOutCharacterDetour(analyzer, text, state); });
	m_buildFontHook.emplace("BuildFont", buildFont, [this](uintptr_t manager, uint16_t index) { return BuildFontDetour(manager, index); });
	m_unloadFontsHook.emplace("UnloadFonts", unloadFonts, [this](uintptr_t manager) { return UnloadFontsDetour(manager); });

	// Caches built so far hold the game's glyphs; if anything above throws, the members undo the setup.
	UpdateFontCaches();
}

FontReplacement::FontReplacer::~FontReplacer() {
	const auto step = [](const char* name, auto&& fn) {
		try {
			fn();
		} catch (const std::exception& e) {
			Host::Error("Undoing the font replacer failed: {}: {}", name, e.what());
		}
	};

	// First, so that the game's font caches are rebuilt while the hooks are still there to build them.
	step("font caches", [this] { SetEnabled(false); });
	step("edge shader", [this] { m_edgeShader.SetSoftness(std::nullopt); });
	step("subpixel shaders", [this] { m_subpixelShaders.Apply({}); });
	m_unloadFontsHook.reset();
	m_buildFontHook.reset();
	m_layOutCharacterHook.reset();
	m_buildFontCacheHook.reset();
	m_pickFontHook.reset();
	m_getGlyphHook.reset();

	// Font sets may still have a copy picked: forgotten once no hook can pick one again.
	step("picked fonts", [] { ForgetPickedFonts(); });

	FreeCopies();
	m_slots.clear();
	m_emptyGlyph.reset();
	m_atlases.clear();
	m_blankTexture.reset();
	m_sizedFonts.clear();
	DisposeFonts();
	m_fallbackFonts.reset();
	m_services.reset();
}

void FontReplacement::FontReplacer::SetFonts(const FontSettings& value) {
	m_layout->Clear();
	DisposeFonts();
	m_sizedFonts.clear();
	m_gameFont = MaterializedFont::CreateGame(*m_services, value.SystemFallback, *m_fallbackFonts);
	for (const auto& [name, def] : value.Preset) {
		try {
			m_presetFonts.emplace(name, std::make_unique<MaterializedFont>(*m_services, def, value.SystemFallback, *m_fallbackFonts));
		} catch (const std::exception& e) {
			Host::Error("Setting up the font {} failed; it is drawn with the game's glyphs: {}", name, e.what());
		}
	}

	// Copies made so far point at the sized fonts just freed.
	ResyncCopies();
	RebuildGlyphs();
}

void FontReplacement::FontReplacer::DisposeFonts() {
	m_presetFonts.clear();
	m_gameFont.reset();
}

bool FontReplacement::FontReplacer::Supports4444() {
	if (m_supports4444)
		return *m_supports4444;

	auto supported = false;
	if (m_blankTexture) {
		UINT support = 0;
		constexpr UINT Needed = D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE;
		const auto hr = GetDevice(m_blankTexture->Resource())->CheckFormatSupport(DXGI_FORMAT_B4G4R4A4_UNORM, &support);
		supported = SUCCEEDED(hr) && (support & Needed) == Needed;
	}
	if (!supported)
		Host::Warning("The game's device doesn't support B4G4R4A4 textures; atlases use B8G8R8A8");
	m_supports4444 = supported;
	return supported;
}

void FontReplacement::FontReplacer::SetAtlas(const FontReplacementAtlasConfig& value) {
	const auto clamped = value.Clamped();
	if (clamped == m_atlas)
		return;
	m_atlas = clamped;
	RemakeAtlases();
}

void FontReplacement::FontReplacer::RemakeAtlases() {
	RebuildGlyphs();

	// Glyphs and copies refer to the old atlases' pages, so they go first.
	m_lastEviction.clear();
	{
		const auto lock = std::scoped_lock(m_pendingPagesMutex);
		m_pendingPageAtlases.clear();
	}
	m_sizedFonts.clear();
	m_atlases.clear();
	ResyncCopies();
	ForgetPickedFonts();
	if (TextInvalidated)
		TextInvalidated();
}

void FontReplacement::FontReplacer::SetEdge(const FontReplacementEdgeConfig& value) {
	const auto edge = value.Drawn();
	if (edge == m_edge)
		return;

	// Glyphs made now show only from the next frame (Upload), so they are remade, blanking text for a frame, only if a margin changes.
	const auto marginChanged = std::ranges::any_of(m_sizedFonts | std::views::values, [&](const auto& sized) {
		return GetEdgeMargin(edge, sized->Px) != GetEdgeMargin(m_edge, sized->Px);
	});
	m_edge = edge;

	// The edge shader draws a round edge of any width unless edges are the game's own 1 px; the game's, widened only by claiming a narrower
	// texture (blocky past about 1.5 px), is the fallback.
	try {
		m_edgeShader.SetSoftness(edge.Enabled && edge != FontReplacementEdgeConfig{.Enabled = true} ? std::optional(edge.Softness) : std::nullopt);
	} catch (const std::exception& e) {
		Host::Error("Using the edge shader failed; edges are widened with the game's: {}", e.what());
		m_edgeShader.SetSoftness(std::nullopt);
	}

	// Copies claim their texture width by the edge.
	for (const auto& [copy, info] : m_copies)
		Sync(copy, info);
	if (marginChanged)
		RebuildGlyphs();
	else if (TextInvalidated)
		TextInvalidated();
}

void FontReplacement::FontReplacer::SetSubpixel(const SubpixelShaders::SettingsRecord& value) {
	if (value == m_subpixel)
		return;
	m_subpixel = value;
	try {
		m_subpixelShaders.Apply(value);
	} catch (const std::exception& e) {
		Host::Error("Subpixel text is off: {}", e.what());
		m_subpixelShaders.Apply({});
	}

	const auto active = m_subpixelShaders.Active();
	const auto order = active ? value.Order : SubpixelOrder::None;
	const auto atlasesChange = std::ranges::any_of(m_atlases | std::views::values, [active](const auto& atlas) { return atlas->Subpixel() != active; });
	if (order == m_services->GetSubpixelOrder() && !atlasesChange)
		return;
	m_services->SetSubpixelOrder(order);
	if (atlasesChange)
		RemakeAtlases();
	else
		RebuildGlyphs();
}

void FontReplacement::FontReplacer::SetVerticalSubpixel(FontReplacementVerticalSubpixelRenderer value) {
	const auto withFreeType = value == FontReplacementVerticalSubpixelRenderer::FreeType;
	if (withFreeType == m_services->DirectWrite().VerticalWithFreeType())
		return;
	m_services->DirectWrite().SetVerticalWithFreeType(withFreeType);
	if (IsVertical(m_services->GetSubpixelOrder()))
		RebuildGlyphs();
}

void FontReplacement::FontReplacer::RebuildGlyphs() {
	m_layout->Clear();
	m_fullAtlases.clear();
	m_gameReads.clear();
	for (const auto& sized : m_sizedFonts | std::views::values) {
		sized->Glyphs.clear();
		sized->GameGlyphs.clear();
		sized->ConvertedGlyphs.clear();
	}
	m_slots.clear();
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
