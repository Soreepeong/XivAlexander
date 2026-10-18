#pragma once

#include "Config/FontReplacementConfigs.h"
#include "MainApp/FontReplacement/FontStructs.h"
#include "MainApp/FontReplacement/Fonts/Enums/ItalicMode.h"
#include "MainApp/FontReplacement/Fonts/FontServices.h"
#include "MainApp/FontReplacement/Fonts/GlyphAtlas.h"
#include "MainApp/FontReplacement/Fonts/Records/RasterGlyph.h"
#include "MainApp/FontReplacement/Fonts/Shaders/EdgeShader.h"
#include "MainApp/FontReplacement/Fonts/Shaders/SubpixelShaders.h"
#include "MainApp/FontReplacement/Game/KernelTextureWrapper.h"
#include "MainApp/FontReplacement/Game/TextureReader.h"
#include "MainApp/FontReplacement/Host.h"
#include "MainApp/FontReplacement/Presets.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class GameTextLayout;
	class MaterializedFont;
	class MaterializedFontElement;
	class SystemFallbackFonts;

	// Makes the game's text renderer draw glyphs rasterized at the size they are drawn at: PickFont swaps in a copy of the font at the on-screen
	// half-pixel size, and GetGlyph answers with the copy's glyphs (game glyphs copied in). Copies borrow the game font's glyph map (retaken after
	// BuildFont); every texture slot is an atlas page or blank, so no game glyph may reach a copy's draw. Game thread; no glyph pointer outlives a frame.
	class FontReplacer {
	public:
		// Where a sized font's glyph comes from: Font's Element (nullptr if none has it), with Game for the font's game glyphs.
		struct GlyphSource {
			MaterializedFont* Font = nullptr;
			GameFont* Game = nullptr;
			MaterializedFontElement* Element = nullptr;

			bool operator==(const GlyphSource&) const = default;
		};

		// A family's glyphs at one pixel size, shared by the copies of its fonts of that size (a font and its lobby version); Font, with Game (its
		// game font), gives the line metrics and how text is laid out.
		struct SizedFont {
			MaterializedFont* Font;
			GlyphAtlas* Atlas;
			GameFont* Game;
			float Px;
			int Ascent;
			int LineHeight;

			// Fonts glyphs may come from with their game fonts, largest first: only Font at the size of one of the family's fonts.
			std::vector<std::pair<MaterializedFont*, GameFont*>> Fonts;

			// The game fonts the game's glyphs are copied from, largest first.
			std::vector<GameFont*> Sources;

			// By packed UTF-8 value; nullptr for a glyph left to the game.
			std::unordered_map<uint32_t, GameGlyph*> Glyphs;

			// Copied from Sources, by packed UTF-8 value; nullptr if none could be.
			std::unordered_map<uint32_t, GameGlyph*> GameGlyphs;

			// Copied from the glyphs the game fonts' own lookups gave copies, by game font and game glyph.
			std::map<std::pair<GameFont*, const GameGlyph*>, GameGlyph*> ConvertedGlyphs;

			// Pick's of single codepoints.
			std::unordered_map<char32_t, GlyphSource> Picks;

			// A grapheme's (one codepoint for the per-character path): of several fonts, the largest whose element other than the game's has it, else
			// the game's, else one that has none for it.
			GlyphSource Pick(std::span<const char32_t> grapheme);
		};

		// A game glyph's pixels: a rect of a plane of a game font texture (Kernel::Texture), whose top left is (0, Top) from the pen in the game font's
		// pixels, scaled by Scale.
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

		// A preset's fonts for the game fonts of their names (the game's glyphs for the others, or all if empty), drawing what they lack with system
		// fonts if SystemFallback, else the game's.
		struct FontSettings {
			Presets::Fonts Preset;
			bool SystemFallback = true;
		};

	private:
		// Sizes in half pixels: below 4 px nothing is legible; a request over 255 px uses the 255 px copy, scaled up by the renderer.
		static constexpr int MinHalfPx = 8;
		static constexpr int MaxHalfPx = 2 * 255;

		// When the atlas is full, a plane is emptied of glyphs not drawn this recently, at most this often.
		static constexpr uint64_t KeepDrawnMs = 2000;
		static constexpr uint64_t EvictIntervalMs = 1000;

		// What LayOutCharacter lays out a character a font has no glyph of as, in order of preference (packed UTF-8): U+3013, '-', U+3400.
		static constexpr uint32_t MissingGlyphSubstitutes[]{0xE38093, 0x2D, 0xE39080};

		static constexpr int BlankTextureSize = 4;

		enum class CellState : uint8_t {
			// No pixels: an empty glyph, a blank, or one whose pixels couldn't be placed.
			None,

			// Pixels waiting to go to the atlas when the glyph is next drawn.
			Pending,

			// In the atlas, where GameGlyph::Packed says.
			Placed,
		};

		struct Cell {
			CellState State = CellState::None;

			GlyphAtlas* Atlas = nullptr;

			// Coverage while pending (for a game glyph, until its pixels are read); BoxLeft is the ink's left in the box, InkTop its top row.
			RasterGlyph Raster;
			int BoxLeft = 0;
			int InkTop = 0;

			// For a glyph made from a game glyph: where its pixels are read from.
			std::optional<GameTextureSource> Source;

			// GetTickCount64 of the frame it was last drawn in, once placed.
			uint64_t Drawn = 0;

			// Drawn instead while its pixels find no room; nullptr until needed.
			GameGlyph* Blank = nullptr;

			// A real italic glyph, which the game must not shear.
			bool RealItalic = false;
		};

		// Glyph pointers handed to the game point at these, so they must not move.
		struct GlyphSlot {
			GameGlyph Glyph;
			FontReplacer::Cell Cell;
		};

		// A copy of a game font at a size in half pixels: its game font, and the glyphs of its family at its size.
		struct CopyInfo {
			GameFont* Original;
			int HalfPx;
			SizedFont* Sized;
		};

		// The character being laid out on this thread, for GetGlyph's shaped-run lookup. MeasuringOnly: no pixels needed yet; State's flags hold
		// italics; ItalicCleared: italic bit cleared for a real italic glyph, to be set again once the character is laid out.
		struct LayoutContext {
			const uint8_t* Character = nullptr;
			bool MeasuringOnly = false;
			uintptr_t State = 0;
			bool ItalicCleared = false;
		};

		static thread_local LayoutContext s_current;

		std::unique_ptr<FontServices> m_services;
		std::unique_ptr<SystemFallbackFonts> m_fallbackFonts;

		std::optional<Host::Hook<GameFont*, GameFontSet*, uint8_t>> m_pickFontHook;
		std::optional<Host::Hook<GameGlyph*, GameFontSet*, uint32_t, GameFont*>> m_getGlyphHook;
		std::optional<Host::Hook<void, uintptr_t>> m_buildFontCacheHook;
		std::optional<Host::Hook<uintptr_t, uintptr_t, const uint8_t**, uintptr_t>> m_layOutCharacterHook;
		std::optional<Host::Hook<int, uintptr_t, uint16_t>> m_buildFontHook;
		std::optional<Host::Hook<uintptr_t, uintptr_t>> m_unloadFontsHook;
		std::unique_ptr<GameTextLayout> m_layout;

		// Italic bits of the layout state's flags and of GameFontSet.DrawFlags (node italics go there only when drawing; the italic macro sets the
		// copy's), and both flags' offsets, -1 if unknown (italics stay sheared).
		uint32_t m_stateItalicFlag = 0;
		uint32_t m_setItalicFlag = 0;
		int m_stateFlagsOffset = -1;
		int m_setFlagsOffset = -1;

		// A glyph of no width: the characters of a shaped cluster after its first.
		std::unique_ptr<GlyphSlot> m_emptyGlyph;
		void(*m_freeFontCache)(uintptr_t) = nullptr;
		uintptr_t m_rendererVtbl = 0;
		uintptr_t m_renderCountVtbl = 0;

		// AtkTextNode offsets: its font manager cache entry (0 for none) and its cache flags byte; the flag that asks for a cache (ToggleFontCache).
		int m_fontCacheSlotOffset = 0;
		int m_fontCacheFlagsOffset = 0;
		uint8_t m_useFontCacheFlag = 0;

		// A deque: glyphs must not move.
		std::deque<GlyphSlot> m_slots;
		uint64_t m_frameTick = 0;

		// The atlases that ran out of room since they were last made room in, and when that was.
		std::set<GlyphAtlas*> m_fullAtlases;
		std::map<GlyphAtlas*, uint64_t> m_lastEviction;

		// Placed glyphs made from game glyphs, whose pixels are read from the game's textures at the next upload.
		std::vector<GameGlyph*> m_gameReads;
		TextureReader m_gameTextures;

		// One atlas per game font family so one family's text can't push another's out; AXIS (most UI text) has its own page size and count.
		std::map<std::string, std::unique_ptr<GlyphAtlas>, LessIgnoringCase> m_atlases;
		FontReplacementAtlasConfig m_atlas;
		std::optional<bool> m_supports4444;

		// In copies' texture slots without a page, so that a game glyph slipping through draws nothing. Cleared at the first upload.
		std::optional<KernelTextureWrapper> m_blankTexture;
		bool m_blankCleared = false;

		// Copy texture slots change only on the thread the game lays text out on; pages added elsewhere wait for it.
		DWORD m_mainThreadId = 0;
		std::mutex m_pendingPagesMutex;
		std::set<GlyphAtlas*> m_pendingPageAtlases;

		std::map<std::pair<GameFont*, int>, GameFont*> m_copiesByKey;
		std::map<GameFont*, CopyInfo> m_copies;
		std::map<std::tuple<MaterializedFont*, int, std::string>, std::unique_ptr<SizedFont>> m_sizedFonts;

		// The preset's fonts by game font name; the font of game fonts no preset gives one: their own glyphs, with system fonts for what they lack.
		std::unique_ptr<MaterializedFont> m_gameFont;
		std::map<std::string, std::unique_ptr<MaterializedFont>, LessIgnoringCase> m_presetFonts;
		FontSettings m_fonts;
		bool m_enabled = true;

		FontReplacementEdgeConfig m_edge;
		EdgeShader m_edgeShader;

		SubpixelShaders::SettingsRecord m_subpixel;
		SubpixelShaders m_subpixelShaders;

	public:
		// Resolves what it uses of the game and hooks it. Game thread. Throws if this game version can't be worked with.
		FontReplacer();
		FontReplacer(const FontReplacer&) = delete;
		FontReplacer& operator=(const FontReplacer&) = delete;

		// Undoes everything in reverse; a step that fails is logged, and the others still run. Game thread.
		~FontReplacer();

		// The DirectWrite factory and the system's fonts the glyphs are drawn with.
		[[nodiscard]] FontServices& Services() const { return *m_services; }

		// Game thread, when earlier drawn text no longer matches (replacement switched or glyphs rebuilt), for what keeps drawn text (nameplate bakes).
		std::function<void()> TextInvalidated;

		[[nodiscard]] const FontSettings& Fonts() const { return m_fonts; }

		// Game thread, between frames; every glyph is made again, even for the same value.
		void SetFonts(FontSettings value);

		// DXGI 1.2 devices can sample and update B4G4R4A4, but the game also runs on Direct3D 11 devices without it; atlases then take B8G8R8A8.
		[[nodiscard]] bool Supports4444();

		[[nodiscard]] const FontReplacementAtlasConfig& Atlas() const { return m_atlas; }

		// Clamped. Game thread, between frames; a change remakes the atlases and every glyph.
		void SetAtlas(const FontReplacementAtlasConfig& value);

		// Edge outline (FontEdgePS) width per text size: the shader's sample step, one texel of the font's claimed texture width; above 1 px glyph
		// boxes get empty margins, as the edge is only drawn within a glyph's box and one pixel around it.
		[[nodiscard]] const FontReplacementEdgeConfig& Edge() const { return m_edge; }

		// Game thread; glyphs are made again if a size's edge margin changed.
		void SetEdge(const FontReplacementEdgeConfig& value);

		// Between frames, on the game thread: gives copies the atlas pages added off it.
		void EndFrame() { ApplyPendingPages(); }

		[[nodiscard]] const SubpixelShaders::SettingsRecord& Subpixel() const { return m_subpixel; }

		// Game thread, between frames; switching it on or off remakes the atlases and every glyph.
		void SetSubpixel(const SubpixelShaders::SettingsRecord& value);

		// What draws fonts set to DirectWrite for vertical subpixel orders; changing it makes their glyphs again. Game thread.
		[[nodiscard]] FontReplacementVerticalSubpixelRenderer VerticalSubpixel() const;
		void SetVerticalSubpixel(FontReplacementVerticalSubpixelRenderer value);

		// Re-rasterizes everything with current settings. Game thread, between frames; baked nameplates keep their pixels until the game rebakes them.
		void RebuildGlyphs();

		// Game thread.
		[[nodiscard]] bool Enabled() const { return m_enabled; }
		void SetEnabled(bool value);

		// Fills game glyph cells from the game's textures, then uploads the atlas's changes. Present thread, once a frame.
		void Upload();

		// The empty pixels FinishGlyph adds around a glyph of the sized font, on each side.
		[[nodiscard]] int GetEdgeMargin(const SizedFont& sized) const { return GetEdgeMargin(m_edge, sized.Px); }

		// Pads a glyph with empty pixels for a wide edge, which is only drawn within a glyph's box and the pixel around it.
		[[nodiscard]] RasterGlyph FinishGlyph(const SizedFont& sized, RasterGlyph r) const;

		// Pixels go to the atlas when first drawn; for a glyph made from a game glyph they are read from source then (r is blank).
		GameGlyph* PlaceCell(const SizedFont& sized, RasterGlyph r, uint32_t utf8Value, std::optional<GameTextureSource> source = std::nullopt);

		// A real italic glyph, which the game must not shear.
		void MarkRealItalic(GameGlyph* cell) { CellOf(cell).RealItalic = true; }

	private:
		[[nodiscard]] static int GetEdgeMargin(const FontReplacementEdgeConfig& edge, float px);

		void ApplyFonts(const Presets::Fonts& preset, bool systemFallback);
		void DisposeFonts();
		void RemakeAtlases();

		// FontReplacer.Copies.cpp

		// Of one game font (or all if nullptr), the copies take their sized fonts and what they borrow again; false if there were none.
		bool ResyncCopies(GameFont* only = nullptr);

		// Shared by the fonts of one name at the size, going to its family's atlas. At the size of one of the family's fonts, that font's glyphs; at
		// another, each glyph from the largest font drawing it itself (SizedFont::Pick), with the metrics of the font that draws the most itself.
		SizedFont* GetSized(GameFont* original, int halfPx);

		// The preset's font of the name, else the game's.
		MaterializedFont* FontOf(const std::string& fontName);

		// Made on its first use.
		GlyphAtlas* GetAtlas(const std::string& family);

		// Borrows the game font's glyph map, glyphs and flags, keeping the copy's size, metrics and pages; redone after BuildFont frees them.
		void Sync(GameFont* copy, const CopyInfo& info);

		GameFont* GetOrCreateCopy(GameFont* original, int halfPx);

		// Every texture slot: the atlas's page of that index, else the blank texture. Main thread.
		void ApplyPages(GameFont* copy, const GlyphAtlas& atlas);

		void OnPageAdded(GlyphAtlas* atlas);

		void ApplyPendingPages();

		// FontReplacer.Hooks.cpp

		// After an error in a hook (not through SetEnabled, which rebuilds the font caches).
		void DisableAfterError();

		static void ForgetPickedFonts();

		// A font cache keeps glyph pointers laid out at the node's unscaled size while draws pick the on-screen size's font; copy glyphs only fit
		// their copy and must not outlive the replacement, so while enabled nodes get no cache and lay out at each draw.
		void BuildFontCacheDetour(uintptr_t node);

		// Text node font caches are freed while enabled and rebuilt (for nodes asking for one) while disabled.
		void UpdateFontCaches();
		void UpdateFontCaches(uintptr_t uld, int depth);

		// The game rebuilt a font in place (reload, lobby/game font switch), freeing the glyph map and glyphs its copies borrow: they retake them.
		int BuildFontDetour(uintptr_t manager, uint16_t index);
		void OnFontBuilt(GameFont* font);

		// The glyphs made from a game font's glyphs, whose pixels came from its old textures; true if any sized font had some.
		bool ForgetGameGlyphsOf(GameFont* font);

		// The game is about to release its fonts' texture handles: game glyph pixels not read from those textures yet are dropped, and no font set
		// may keep a copy picked until its font is built again.
		uintptr_t UnloadFontsDetour(uintptr_t manager);

		// The glyphs stay blank.
		void DropGameReads(const std::set<uintptr_t>& textures);

		GameFont* PickFontDetour(GameFontSet* set, uint8_t useCache);
		uintptr_t LayOutCharacterDetour(uintptr_t analyzer, const uint8_t** text, uintptr_t state);

		// Italic macros are seen by measuring too; text node italics only by drawing (the bit is in the font set's own flags, before any macro).
		ItalicMode GetItalicMode(GameFontSet* set) const;

		GameGlyph* GetGlyphDetour(GameFontSet* set, uint32_t utf8Value, GameFont* font);

		// All of whose pixels are in its atlas; nullptr if neither the copy nor its game font has one.
		GameGlyph* GetCopyGlyph(GameFontSet* set, uint32_t utf8Value, const CopyInfo& info);

		// For a glyph GetGlyph has nothing for, the game takes the geta mark (U+3013), '-' or U+3400 straight from the game font's glyph map, which
		// would be drawn with a game texture index; the copy's own glyph of those is given instead, else an empty one. Never nullptr.
		GameGlyph* GetMissingGlyph(GameFontSet* set, uint32_t utf8Value, const CopyInfo& info);

		// FontReplacer.Cells.cpp

		// nullptr to leave it to the game.
		GameGlyph* CreateGlyph(SizedFont& sized, uint32_t utf8Value);

		// Drawn from the game's texture, the edge would step by atlas-width texels (a font's textures share one width), so the game glyph of the
		// largest source that has it is copied into the atlas, scaled; pixels are read at the next upload. nullptr if no source has it readable.
		GameGlyph* GetGameGlyph(SizedFont& sized, uint32_t utf8Value);

		// GetGlyph's answer for the copy's game font, copied; an empty glyph of its advance if unreadable.
		GameGlyph* ConvertGameGlyph(SizedFont& sized, GameFont* font, const GameGlyph* game, uint32_t utf8Value);

		// nullptr if its texture can't be read.
		GameGlyph* PlaceGameGlyph(const SizedFont& sized, GameFont* font, const GameGlyph* game, uint32_t utf8Value);

		// 1 within the half-pixel rounding of copy sizes.
		[[nodiscard]] static float GetGameScale(const SizedFont& sized, GameFont* font);

		[[nodiscard]] static RasterGlyph WithSourcePixels(const RasterGlyph& box, const GameTextureSource& s, std::vector<uint8_t> sourceAlpha);

		// A glyph that finds no room draws nothing this time (same advance), and EvictPlane makes room at the next upload.
		GameGlyph* WithPixels(GameGlyph* glyph);

		// false if there is no room; a game glyph's pixels are read at the next upload.
		bool WritePixels(GameGlyph* glyph, Cell& cell, uint64_t drawn);

		// Same advance, draws nothing; for a glyph whose pixels found no room yet.
		GameGlyph* GetBlank(GameGlyph* glyph);

		// Empties the plane where glyphs undrawn for KeepDrawnMs take the most room (if none, evicts least recent until half is free); recent ones
		// are re-placed. Present thread, after this frame's layout: the next frame lays out with the new places and the following upload writes them.
		void EvictPlane(GlyphAtlas* atlas);

		// texture index (page) * planes per page + channel.
		[[nodiscard]] static int PlaneOf(const GameGlyph* glyph);

		static void WriteCell(const GameGlyph* glyph, const Cell& cell);

		static void ReleasePixels(Cell& cell);

		// Draws nothing from now on, advancing as measured.
		static void DropCell(GameGlyph* glyph, Cell& cell);

		// For a glyph whose pixels found no room: draws nothing, advancing as measured.
		static void DropPixels(GameGlyph* glyph);

		GameGlyph* AllocateGlyph();

		[[nodiscard]] static Cell& CellOf(GameGlyph* glyph) { return CONTAINING_RECORD(glyph, GlyphSlot, Glyph)->Cell; }

		void FreeGlyphs();
	};
}
