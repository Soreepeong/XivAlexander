#pragma once

#include "Config/FontReplacementConfigs.h"
#include "MainApp/FontReplacement/Fonts/Enums/SubpixelOrder.h"
#include "MainApp/FontReplacement/Fonts/ResetScope.h"
#include "MainApp/FontReplacement/Game/KernelTextureWrapper.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class AtlasSet;
	class CellPlacer;
	class CopyFonts;
	class FontHooks;
	class FontLifecycleHooks;
	class FontServices;
	class FontSet;
	class FontSetFlags;
	class FrameState;
	class GameTextLayout;
	class GlyphFactory;
	class GlyphStore;
	class SizedFontRegistry;
	class SystemFallbackFonts;

	// Replaces the game's fonts. It makes the parts, holds their settings, and does their work at Present.
	// Its setters skip what didn't change, and return how much must be made again, which the caller passes to Reset.
	// Reset is the one way to make glyphs, sized fonts, atlases or fonts again, called between frames or in a hook outside any layout. Nameplates
	// already baked keep their pixels until baked again.
	class FontReplacer {
		static constexpr int BlankTextureSize = 4;

		FrameState& m_state;
		std::unique_ptr<FontServices> m_services;
		std::unique_ptr<SystemFallbackFonts> m_fallbackFonts;
		std::unique_ptr<FontSetFlags> m_flags;
		std::unique_ptr<GlyphStore> m_store;

		// What copy texture slots without a page hold, so that a game glyph slipping through draws nothing. Cleared at the first Present, as the
		// immediate context is free only then.
		std::optional<KernelTextureWrapper> m_blankTexture;
		bool m_blankCleared = false;

		std::unique_ptr<AtlasSet> m_atlases;
		std::unique_ptr<SizedFontRegistry> m_sized;
		std::unique_ptr<CopyFonts> m_copies;
		std::unique_ptr<CellPlacer> m_placer;
		std::unique_ptr<GlyphFactory> m_factory;
		std::unique_ptr<GameTextLayout> m_layout;

		// Removed first, by Unhook.
		std::unique_ptr<FontHooks> m_fontHooks;
		std::unique_ptr<FontLifecycleHooks> m_lifecycleHooks;

		std::optional<bool> m_supports4444;

		// As last asked for, which an error may have turned off since.
		std::optional<bool> m_enabled;

	public:
		// Game thread. Throws if this game version can't be worked with.
		explicit FontReplacer(FrameState& state);
		FontReplacer(const FontReplacer&) = delete;
		FontReplacer& operator=(const FontReplacer&) = delete;

		// Game thread, once the game holds none of the glyphs and copies.
		~FontReplacer();

		[[nodiscard]] FontServices& Services() const { return *m_services; }

		[[nodiscard]] SystemFallbackFonts& FallbackFonts() const { return *m_fallbackFonts; }

		// Whether fonts are replaced, which an error turns off at once.
		[[nodiscard]] bool IsEnabled() const;

		// DXGI 1.2 devices can sample and update B4G4R4A4, but the game also runs on Direct3D 11 devices without it.
		[[nodiscard]] bool Supports4444();

		// Such text nodes are measured at their unscaled size, as they are baked at a scale of their own.
		void SetNamePlateTest(std::function<bool(uintptr_t node)> value);

		// Applied when it differs from what was last asked for, even if an error turned it off already, so that the font caches follow.
		ResetScope SetEnabled(bool value);

		// Glyphs made now show only from the next frame, so they are made again, blanking text for a frame, only if a margin changes.
		ResetScope SetEdge(const FontReplacementEdgeConfig& value);

		ResetScope SetAtlas(const FontReplacementAtlasConfig& value);

		// order is of the glyphs, None unless active, which is whether the subpixel shaders are on, which atlases are made for.
		ResetScope SetSubpixel(SubpixelOrder order, bool active);

		ResetScope SetVerticalSubpixel(FontReplacementVerticalSubpixelRenderer value);

		// Makes what scope names again, each step once and in order. fonts, if given, replaces the fonts, and the old ones are freed.
		void Reset(ResetScope scope, std::unique_ptr<FontSet> fonts = nullptr);

		// At Present, after this frame's text was laid out: reads game glyphs' pixels, makes room in full atlases (every glyph is made again if it
		// can't be), uploads the atlases, and sweeps the caches. Caches are swept after the game's reads, which hold glyphs without a reference.
		// Runs go first, then the cells they let go of.
		void Present();

		// Removes the hooks and has every font set pick its own font again. The destructor frees the rest once the game holds none of it.
		void Unhook();
	};
}
