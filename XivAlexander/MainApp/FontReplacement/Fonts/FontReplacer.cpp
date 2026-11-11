#include "pch.h"
#include "MainApp/FontReplacement/Fonts/FontReplacer.h"

#include "MainApp/FontReplacement/Fonts/AtlasSet.h"
#include "MainApp/FontReplacement/Fonts/CopyFonts.h"
#include "MainApp/FontReplacement/Fonts/FontServices.h"
#include "MainApp/FontReplacement/Fonts/FrameState.h"
#include "MainApp/FontReplacement/Fonts/Glyphs/CellPlacer.h"
#include "MainApp/FontReplacement/Fonts/Glyphs/GlyphFactory.h"
#include "MainApp/FontReplacement/Fonts/Glyphs/GlyphStore.h"
#include "MainApp/FontReplacement/Fonts/Hooks/FontHooks.h"
#include "MainApp/FontReplacement/Fonts/Hooks/FontLifecycleHooks.h"
#include "MainApp/FontReplacement/Fonts/Hooks/LayoutContext.h"
#include "MainApp/FontReplacement/Fonts/SizedFontRegistry.h"
#include "MainApp/FontReplacement/Fonts/Text/GameTextLayout.h"
#include "MainApp/FontReplacement/Fonts/Text/SystemFallbackFonts.h"
#include "MainApp/FontReplacement/Game/FontNames.h"
#include "MainApp/FontReplacement/GameUi.h"
#include "MainApp/FontReplacement/Host.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::FontReplacer::FontReplacer(FrameState& state)
	: m_state(state) {
	ResolveGameFontStructs();
	GameUi::ResolveUnits();
	GameUi::ResolveTextures();
	m_context = std::make_unique<LayoutContext>();

	m_services = std::make_unique<FontServices>();
	m_fallbackFonts = std::make_unique<SystemFallbackFonts>(*m_services);
	try {
		FontNames::Initialize();
	} catch (const std::exception& e) {
		Host::Warning("The game's font names weren't found; presets can't be applied: {}", e.what());
	}

	m_store = std::make_unique<GlyphStore>();
	m_blankTexture.emplace(BlankTextureSize, BlankTextureSize);
	m_atlases = std::make_unique<AtlasSet>([this] { return Supports4444(); });
	m_sized = std::make_unique<SizedFontRegistry>(FontSet::CreateGame(*m_services, true, *m_fallbackFonts), *m_atlases, *m_store, state);
	m_copies = std::make_unique<CopyFonts>(*m_sized, *m_blankTexture, state);
	m_placer = std::make_unique<CellPlacer>(*m_store, *m_copies);
	m_factory = std::make_unique<GlyphFactory>(*m_store, state);
	m_layout = std::make_unique<GameTextLayout>(*m_services, *m_store, *m_factory, state);
	m_fontHooks = std::make_unique<FontHooks>(*m_copies, *m_sized, *m_layout, *m_factory, *m_placer, *m_store, state, *m_context);
	m_lifecycleHooks = std::make_unique<FontLifecycleHooks>([this](ResetScope scope) { Reset(scope); }, state);

	// Caches built so far hold the game's glyphs. If this throws, the members undo the setup, the hooks first.
	m_fontHooks->UpdateFontCaches();
}

FontReplacement::FontReplacer::~FontReplacer() {
	try {
		Unhook();
	} catch (const std::exception& e) {
		Host::Error("Removing the font hooks failed: {}", e.what());
	}
}

bool FontReplacement::FontReplacer::IsEnabled() const {
	return m_state.IsOn();
}

bool FontReplacement::FontReplacer::Supports4444() {
	if (m_supports4444)
		return *m_supports4444;

	auto supported = false;
	try {
		UINT support = 0;
		constexpr UINT Needed = D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE;
		const auto hr = GetDevice(m_blankTexture->Resource())->CheckFormatSupport(DXGI_FORMAT_B4G4R4A4_UNORM, &support);
		supported = SUCCEEDED(hr) && (support & Needed) == Needed;
	} catch (const std::exception& e) {
		Host::Warning("Checking the support for B4G4R4A4 textures failed: {}", e.what());
	}

	if (!supported)
		Host::Warning("The game's device doesn't support B4G4R4A4 textures; atlases use B8G8R8A8");
	m_supports4444 = supported;
	return supported;
}

void FontReplacement::FontReplacer::SetNamePlateTest(std::function<bool(uintptr_t node)> value) {
	if (m_fontHooks)
		m_fontHooks->IsNamePlateText = std::move(value);
}

FontReplacement::ResetScope FontReplacement::FontReplacer::SetEnabled(bool value) {
	if (m_enabled == value)
		return ResetScope::None;
	m_enabled = value;
	m_state.SetOn(value);
	CopyFonts::ForgetPicked();
	if (m_fontHooks)
		m_fontHooks->UpdateFontCaches();
	return ResetScope::Text;
}

FontReplacement::ResetScope FontReplacement::FontReplacer::SetEdge(const FontReplacementEdgeConfig& value) {
	const auto edge = value.Drawn();
	const auto old = m_state.Edge();
	if (edge == old)
		return ResetScope::None;

	const auto marginChanged = std::ranges::any_of(m_sized->All(), [&](const SizedFont* s) {
		return GlyphFactory::GetEdgeMargin(edge, s->Px) != GlyphFactory::GetEdgeMargin(old, s->Px);
	});
	m_state.SetEdge(edge);
	m_copies->SyncAll();
	return marginChanged ? ResetScope::Glyphs : ResetScope::Text;
}

FontReplacement::ResetScope FontReplacement::FontReplacer::SetAtlas(const FontReplacementAtlasConfig& value) {
	const auto clamped = value.Clamped();
	if (clamped == m_atlases->Settings())
		return ResetScope::None;
	m_atlases->SetSettings(clamped);
	return ResetScope::Atlases;
}

FontReplacement::ResetScope FontReplacement::FontReplacer::SetSubpixel(SubpixelOrder order) {
	if (order == m_services->GetSubpixelOrder())
		return ResetScope::None;
	const auto active = order != SubpixelOrder::None;
	m_atlases->SetSubpixel(active);
	const auto atlasesChange = std::ranges::any_of(m_atlases->All(), [active](const GlyphAtlas* a) { return a->Subpixel() != active; });
	m_services->SetSubpixelOrder(order);
	return atlasesChange ? ResetScope::Atlases : ResetScope::Glyphs;
}

FontReplacement::ResetScope FontReplacement::FontReplacer::SetVerticalSubpixel(FontReplacementVerticalSubpixelRenderer value) {
	const auto withFreeType = value == FontReplacementVerticalSubpixelRenderer::FreeType;
	if (withFreeType == m_services->DirectWrite().VerticalWithFreeType())
		return ResetScope::None;
	m_services->DirectWrite().SetVerticalWithFreeType(withFreeType);
	return IsVertical(m_services->GetSubpixelOrder()) ? ResetScope::Glyphs : ResetScope::None;
}

void FontReplacement::FontReplacer::Reset(ResetScope scope, std::unique_ptr<FontSet> fonts) {
	if (fonts)
		scope |= ResetScope::Fonts;
	if (!Includes(scope, ResetScope::Glyphs))
		return;

	// Nothing of ours refers to the glyphs anymore, and they are freed once the game is done with them.
	m_layout->Clear();
	if (Includes(scope, ResetScope::SizedFonts)) {
		// The game's textures may have gone too.
		m_placer->ClearAll();
		m_sized->Clear();
		m_sized->Fonts().ForgetGameFonts();
	} else {
		m_placer->Clear();
		m_sized->ReleaseAllGlyphs();
	}

	// Copies refer to the old atlases' pages until marked stale below.
	if (Includes(scope, ResetScope::Atlases))
		m_atlases->Dispose();
	else
		m_atlases->ClearAll();

	if (fonts)
		m_sized->SwapFonts(std::move(fonts));
	if (Includes(scope, ResetScope::SizedFonts))
		m_copies->MarkStale();
	CopyFonts::ForgetPicked();
}

void FontReplacement::FontReplacer::Present() {
	m_placer->ReadGameGlyphs();
	try {
		if (m_placer->EvictFull())
			m_state.PlaneReused();
	} catch (const std::exception& e) {
		Host::Error("Making room in a glyph atlas failed; making every glyph again: {}", e.what());
		Reset(ResetScope::Glyphs);
		m_state.PlaneReused();
	}

	m_atlases->Upload();
	if (!m_blankCleared) {
		const std::vector<uint8_t> zeros(static_cast<size_t>(BlankTextureSize) * BlankTextureSize * 4);
		GetImmediateContext(m_blankTexture->Resource())->UpdateSubresource(m_blankTexture->Resource(), 0, nullptr, zeros.data(), BlankTextureSize * 4, 0);
		m_blankCleared = true;
	}

	m_layout->Sweep();
	m_sized->Sweep();
	m_store->FreeUnreferenced(m_state.Frame());
}

void FontReplacement::FontReplacer::Unhook() {
	const auto forget = xivres::util::on_dtor([] { CopyFonts::ForgetPicked(); });
	m_lifecycleHooks.reset();
	m_fontHooks.reset();
}
