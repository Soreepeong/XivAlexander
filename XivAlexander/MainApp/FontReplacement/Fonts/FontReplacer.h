#pragma once

#include "Config/FontReplacementConfigs.h"
#include "MainApp/FontReplacement/FontStructs.h"
#include "MainApp/FontReplacement/Fonts/Enums/ItalicMode.h"
#include "MainApp/FontReplacement/Fonts/FontServices.h"
#include "MainApp/FontReplacement/Fonts/GlyphAtlas.h"
#include "MainApp/FontReplacement/Fonts/Records/RasterGlyph.h"
#include "MainApp/FontReplacement/Fonts/Shaders/EdgeShader.h"
#include "MainApp/FontReplacement/Fonts/Shaders/SubpixelShaders.h"
#include "MainApp/FontReplacement/Fonts/SizedFont.h"
#include "MainApp/FontReplacement/Game/KernelTextureWrapper.h"
#include "MainApp/FontReplacement/Game/TextureReader.h"
#include "MainApp/FontReplacement/Host.h"
#include "MainApp/FontReplacement/Presets.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class GameTextLayout;
	class SystemFallbackFonts;

	// Copies borrow their game font's glyph map, but every texture slot is an atlas page or blank, so no game glyph may reach a copy's draw.
	class FontReplacer {
	public:
		// A rect of a game font texture's plane, whose top left is at (0, Top) from the pen in the game font's pixels.
		struct GameTextureSource {
			uintptr_t Texture;
			int X;
			int Y;
			int Width;
			int Height;
			int Plane;
			int Top;
			float Scale;
		};

		// Game fonts the preset has no font of keep the game's glyphs, and characters a font lacks come from the game's glyphs unless SystemFallback.
		struct FontSettings {
			Presets::Fonts Preset;
			bool SystemFallback = true;
		};

	private:
		std::unique_ptr<FontServices> m_services;
		std::unique_ptr<SystemFallbackFonts> m_fallbackFonts;

		// FontReplacer.Hooks.cpp

		// Sizes in half pixels: below 4 px nothing is legible; a request over 255 px uses the 255 px copy, scaled up by the renderer.
		static constexpr int MinHalfPx = 8;
		static constexpr int MaxHalfPx = 2 * 255;

		// What LayOutCharacter lays out a character a font has no glyph of as, in order of preference (packed UTF-8): U+3013, '-', U+3400.
		static constexpr uint32_t MissingGlyphSubstitutes[]{0xE38093, 0x2D, 0xE39080};

		// For GetGlyph, which isn't told which character of the text LayOutCharacter is laying out.
		struct LayoutContext {
			const uint8_t* Character = nullptr;

			// Laid out by an analyzer that only measures, so its glyph needs no pixels yet.
			bool MeasuringOnly = false;

			uintptr_t State = 0;

			// The italic bit was cleared for a real italic glyph, to be set again once the character is laid out.
			bool ItalicCleared = false;
		};

		static thread_local LayoutContext s_current;

		std::optional<Host::Hook<GameFont*, GameFontSet*, uint8_t>> m_pickFontHook;
		std::optional<Host::Hook<GameGlyph*, GameFontSet*, uint32_t, GameFont*>> m_getGlyphHook;
		std::optional<Host::Hook<void, uintptr_t>> m_buildFontCacheHook;
		std::optional<Host::Hook<uintptr_t, uintptr_t, const uint8_t**, uintptr_t>> m_layOutCharacterHook;
		std::optional<Host::Hook<int, uintptr_t, uint16_t>> m_buildFontHook;
		std::optional<Host::Hook<uintptr_t, uintptr_t>> m_unloadFontsHook;
		uintptr_t m_rendererVtbl = 0;
		uintptr_t m_renderCountVtbl = 0;
		void(*m_freeFontCache)(uintptr_t) = nullptr;

		// AtkTextNode offsets of its font manager cache entry (0 for none) and cache flags byte, whose m_useFontCacheFlag asks for a cache.
		int m_fontCacheSlotOffset = 0;
		int m_fontCacheFlagsOffset = 0;
		uint8_t m_useFontCacheFlag = 0;

		// Of the layout state's flags and of GameFontSet::DrawFlags; an offset of -1 is unknown, leaving italics sheared.
		uint32_t m_stateItalicFlag = 0;
		uint32_t m_setItalicFlag = 0;
		int m_stateFlagsOffset = -1;
		int m_setFlagsOffset = -1;

		std::unique_ptr<GameTextLayout> m_layout;

		// FontReplacer.Cells.cpp

		// When an atlas is full, a plane is emptied of glyphs not drawn this recently, at most this often.
		static constexpr uint64_t KeepDrawnMs = 2000;
		static constexpr uint64_t EvictIntervalMs = 1000;

		enum class CellState : uint8_t {
			// An empty glyph, a blank, or one whose pixels couldn't be placed.
			None,
			Pending,
			Placed,
		};

		struct Cell {
			CellState State = CellState::None;
			GlyphAtlas* Atlas = nullptr;

			// Kept while pending, and for a game glyph until its pixels are read.
			RasterGlyph Raster;

			// The box's left relative to the pen, as Raster's Left is.
			int BoxLeft = 0;

			// The ink's top row in the box.
			int InkTop = 0;

			std::optional<GameTextureSource> Source;

			// GetTickCount64 of the frame it was last drawn in, once placed.
			uint64_t Drawn = 0;

			// Drawn instead while its pixels find no room.
			GameGlyph* Blank = nullptr;

			// A real italic glyph, which the game must not shear.
			bool RealItalic = false;
		};

		// Glyph pointers handed to the game point at these, so they must not move.
		struct GlyphSlot {
			GameGlyph Glyph;
			FontReplacer::Cell Cell;
		};

		// A glyph of no width, for the characters of a shaped cluster after its first.
		std::unique_ptr<GlyphSlot> m_emptyGlyph;

		// A deque, as glyphs must not move.
		std::deque<GlyphSlot> m_slots;
		uint64_t m_frameTick = 0;

		std::set<GlyphAtlas*> m_fullAtlases;
		std::map<GlyphAtlas*, uint64_t> m_lastEviction;

		// Placed glyphs made from game glyphs, whose pixels are read from the game's textures at the next upload.
		std::vector<GameGlyph*> m_gameReads;
		TextureReader m_gameTextures;

		// FontReplacer.Copies.cpp

		struct CopyInfo {
			GameFont* Original;
			int HalfPx;
			SizedFont* Sized;
		};

		// One atlas per game font family so one family's text can't push another's out; AXIS (most UI text) has its own page size and count.
		std::map<std::string, std::unique_ptr<GlyphAtlas>, LessIgnoringCase> m_atlases;

		// Copy texture slots change only on the thread the game lays text out on; pages added elsewhere wait for it.
		DWORD m_mainThreadId = 0;
		std::mutex m_pendingPagesMutex;
		std::set<GlyphAtlas*> m_pendingPageAtlases;

		std::map<std::pair<GameFont*, int>, GameFont*> m_copiesByKey;
		std::map<GameFont*, CopyInfo> m_copies;
		std::map<std::tuple<MaterializedFont*, int, std::string>, std::unique_ptr<SizedFont>> m_sizedFonts;

		// FontReplacer.cpp

		static constexpr int BlankTextureSize = 4;

		FontReplacementAtlasConfig m_atlas;
		std::optional<bool> m_supports4444;

		// Fills copy texture slots without a page, so that a game glyph slipping through draws nothing.
		std::optional<KernelTextureWrapper> m_blankTexture;
		bool m_blankCleared = false;

		std::unique_ptr<MaterializedFont> m_gameFont;
		std::map<std::string, std::unique_ptr<MaterializedFont>, LessIgnoringCase> m_presetFonts;
		bool m_enabled = true;

		FontReplacementEdgeConfig m_edge;
		EdgeShader m_edgeShader;

		SubpixelShaders::SettingsRecord m_subpixel;
		SubpixelShaders m_subpixelShaders;

	public:
		// Game thread; throws if this game version can't be worked with.
		FontReplacer();
		FontReplacer(const FontReplacer&) = delete;
		FontReplacer& operator=(const FontReplacer&) = delete;

		// Game thread; a step that fails is logged, and the others still run.
		~FontReplacer();

		// Game thread, when earlier drawn text no longer matches, for what keeps drawn text around (nameplate bakes).
		std::function<void()> TextInvalidated;

		// Game thread, between frames; every glyph is made again, even for the same value.
		void SetFonts(const FontSettings& value);

		// DXGI 1.2 devices can sample and update B4G4R4A4, but the game also runs on Direct3D 11 devices without it.
		[[nodiscard]] bool Supports4444();

		// Clamped; game thread, between frames.
		void SetAtlas(const FontReplacementAtlasConfig& value);

		// Game thread.
		void SetEdge(const FontReplacementEdgeConfig& value);

		// Between frames, as copy texture slots change only on the game thread.
		void EndFrame() { ApplyPendingPages(); }

		// Game thread, between frames.
		void SetSubpixel(const SubpixelShaders::SettingsRecord& value);

		// Game thread.
		void SetVerticalSubpixel(FontReplacementVerticalSubpixelRenderer value);

		// Game thread, between frames; baked nameplates keep their pixels until the game bakes them again.
		void RebuildGlyphs();

		// Game thread.
		[[nodiscard]] bool Enabled() const { return m_enabled; }
		void SetEnabled(bool value);

		// Present thread, once a frame.
		void Upload();

		[[nodiscard]] int GetEdgeMargin(const SizedFont& sized) const { return GetEdgeMargin(m_edge, sized.Px); }

		// Adds empty margins for a wide edge, as the edge is only drawn within a glyph's box and the pixel around it.
		[[nodiscard]] RasterGlyph FinishGlyph(const SizedFont& sized, RasterGlyph r) const;

		// With source, r is blank and its pixels are read from the game's texture once placed.
		GameGlyph* PlaceCell(const SizedFont& sized, RasterGlyph r, uint32_t utf8Value, std::optional<GameTextureSource> source = std::nullopt);

		void MarkRealItalic(GameGlyph* cell) { CellOf(cell).RealItalic = true; }

	private:
		[[nodiscard]] static int GetEdgeMargin(const FontReplacementEdgeConfig& edge, float px);

		void DisposeFonts();
		void RemakeAtlases();

		// FontReplacer.Copies.cpp

		// false if there were no copies (of only, if given).
		bool ResyncCopies(GameFont* only = nullptr);

		// At the size of one of the family's fonts, that font's glyphs with its game font's metrics; at another, each glyph from the largest font
		// that draws it itself, with the metrics of the font that draws the most glyphs itself.
		SizedFont* GetSized(GameFont* original, int halfPx);

		MaterializedFont* FontOf(const std::string& fontName);
		GlyphAtlas* GetAtlas(const std::string& family);

		// BuildFont frees the glyph map and glyphs a copy borrows from its game font, so they are borrowed again after it.
		void Sync(GameFont* copy, const CopyInfo& info);

		GameFont* GetOrCreateCopy(GameFont* original, int halfPx);
		void FreeCopies();

		// Main thread; the blank texture fills the slots after the pages, so that the renderer has a vertex buffer and a texture for every index.
		void ApplyPages(GameFont* copy, const GlyphAtlas& atlas);

		void OnPageAdded(GlyphAtlas* atlas);
		void ApplyPendingPages();

		// FontReplacer.Hooks.cpp

		// Not through SetEnabled, which rebuilds the font caches.
		void DisableAfterError();

		static void ForgetPickedFonts();

		// A font cache keeps the glyph pointers laid out at the node's unscaled size, which fit only that size's copy and must not outlive the
		// replacement, so while enabled, nodes get no cache and are laid out at each draw.
		void BuildFontCacheDetour(uintptr_t node);

		void UpdateFontCaches();
		void UpdateFontCaches(uintptr_t uld, int depth);

		// The game rebuilds a font in place (a reload, a switch between the lobby's fonts and the game's), freeing the glyph map and glyphs its
		// copies borrow.
		int BuildFontDetour(uintptr_t manager, uint16_t index);
		void OnFontBuilt(GameFont* font);

		// Their pixels came from the font's old textures, and converted ones are keyed by its old glyph pointers.
		bool ForgetGameGlyphsOf(GameFont* font);

		// The game unloads every font (a switch between the lobby's fonts and the game's, a UI shader reload, shutdown), releasing their textures'
		// resource handles.
		uintptr_t UnloadFontsDetour(uintptr_t manager);

		GameFont* PickFontDetour(GameFontSet* set, uint8_t useCache);
		uintptr_t LayOutCharacterDetour(uintptr_t analyzer, const uint8_t** text, uintptr_t state);

		// An italic macro sets the state's bit, which measuring sees too; a text node's italics are only in the font set's own flags, which only
		// drawing sees.
		ItalicMode GetItalicMode(GameFontSet* set) const;

		GameGlyph* GetGlyphDetour(GameFontSet* set, uint32_t utf8Value, GameFont* font);

		// nullptr if neither the copy nor its game font has a glyph of the character.
		GameGlyph* GetCopyGlyph(GameFontSet* set, uint32_t utf8Value, const CopyInfo& info);

		// LayOutCharacter would take its substitutes from the font's glyph map directly, getting game glyphs of texture indices the copy has no
		// textures of, so the copy's own glyphs of them (else one of no width) are given instead.
		GameGlyph* GetMissingGlyph(GameFontSet* set, uint32_t utf8Value, const CopyInfo& info);

		// FontReplacer.Cells.cpp

		// nullptr leaves the glyph to the game.
		GameGlyph* CreateGlyph(SizedFont& sized, uint32_t utf8Value);

		// From the largest of the sized font's game fonts that has it, copied into the atlas because the edge steps by texels of the width all of a
		// font's textures share, which is the atlas's.
		GameGlyph* GetGameGlyph(SizedFont& sized, uint32_t utf8Value);

		GameGlyph* ConvertGameGlyph(SizedFont& sized, GameFont* font, const GameGlyph* game, uint32_t utf8Value);
		GameGlyph* PlaceGameGlyph(const SizedFont& sized, GameFont* font, const GameGlyph* game, uint32_t utf8Value);

		// 1 within the half-pixel rounding of copy sizes.
		[[nodiscard]] static float GetGameScale(const SizedFont& sized, GameFont* font);

		[[nodiscard]] static RasterGlyph WithSourcePixels(const RasterGlyph& box, const GameTextureSource& s, std::vector<uint8_t> sourceAlpha);

		// A glyph that finds no room draws nothing this time, at the same advance, and room is made at the next upload.
		GameGlyph* WithPixels(GameGlyph* glyph);

		bool WritePixels(GameGlyph* glyph, Cell& cell, uint64_t drawn);
		GameGlyph* GetBlank(GameGlyph* glyph);

		// Present thread, after this frame's layout, so that the next frame lays out with the new places and the upload after it writes them.
		void EvictPlane(GlyphAtlas* atlas);

		[[nodiscard]] static int PlaneOf(const GameGlyph* glyph);
		static void WriteCell(const GameGlyph* glyph, const Cell& cell);
		static void ReleasePixels(Cell& cell);
		static void DropCell(GameGlyph* glyph, Cell& cell);
		static void DropPixels(GameGlyph* glyph);
		void DropGameReads(const std::set<uintptr_t>& textures);
		GameGlyph* AllocateGlyph();

		[[nodiscard]] static Cell& CellOf(GameGlyph* glyph) { return CONTAINING_RECORD(glyph, GlyphSlot, Glyph)->Cell; }
	};
}
