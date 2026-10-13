#pragma once

#include "Config/FontReplacementConfigs.h"
#include "MainApp/FontReplacement/EdgeShader.h"
#include "MainApp/FontReplacement/FontStructs.h"
#include "MainApp/FontReplacement/GameTextureReader.h"
#include "MainApp/FontReplacement/GlyphAtlas.h"
#include "MainApp/FontReplacement/Presets.h"
#include "MainApp/FontReplacement/ReplacementFace.h"
#include "MainApp/FontReplacement/SubpixelText.h"
#include "MainApp/FontReplacement/TextShaper.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// A rect of a game font texture plane (Kernel::Texture); top left at (Left, Top) from the pen in game font pixels, scaled by Scale.
	struct GameTextureSource {
		uintptr_t Texture;
		int X;
		int Y;
		int Width;
		int Height;
		int Plane;
		int Left;
		int Top;
		float Scale;
	};

	// Where a SizedFont's glyph of a codepoint comes from: Face's Element (nullptr: the system's fonts), with Game for the face's game glyphs.
	struct GlyphSource {
		ReplacementFace* Face;
		GameFont* Game;
		FaceElement* Element;
	};

	// A family's glyphs at one size, shared by its fonts' copies of that size; Face (with Game, its game font) gives line metrics and layout defaults.
	struct SizedFont {
		ReplacementFace* Face;
		float Px;
		int Ascent;
		int LineHeight;
		GameFont* Game;

		GlyphAtlas* Atlas;

		// Faces glyphs may come from with their game fonts, largest first: only Face at a size of one of the family's faces.
		std::vector<std::pair<ReplacementFace*, GameFont*>> Faces;

		// Game fonts the game's glyphs are copied from, largest first.
		std::vector<GameFont*> Sources;

		// By packed UTF-8 value; nullptr for a glyph left to the game.
		std::unordered_map<uint32_t, GameGlyph*> Glyphs;

		// Copied from Sources, by packed UTF-8 value; nullptr if none could be.
		std::unordered_map<uint32_t, GameGlyph*> GameGlyphs;

		// Copies of the game glyphs GetGlyph gave for a copy's game font, by that glyph; never nullptr (blank if unreadable).
		std::unordered_map<const GameGlyph*, GameGlyph*> Converted;

		// Pick's, by codepoint.
		mutable std::unordered_map<char32_t, GlyphSource> Picks;

		// Of several faces, the largest whose element other than the game's has it, else one leaving it to the system's fonts, else the game's.
		[[nodiscard]] const GlyphSource& Pick(char32_t codepoint) const;
	};

	// Hooks PickFont (swaps in a copy at the exact on-screen half-px size so the renderer doesn't scale) and GetGlyph (the copy's glyphs, game glyphs copied in).
	// Copies borrow the game font's glyph map (retaken after BuildFont); every texture slot is an atlas page or blank, so no game glyph may reach a copy's draw.
	// Game thread; no glyph pointer outlives a frame.
	class FontReplacer {
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

		// In half pixels. AppliedPages: atlas pages in the copy's texture slots.
		struct CopyInfo {
			GameFont* Original;
			int HalfPx;
			SizedFont* Sized;
			int AppliedPages = 0;
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

		std::optional<Host::Hook<GameFont*, GameFontSet*, uint8_t>> m_pickFontHook;
		std::optional<Host::Hook<GameGlyph*, GameFontSet*, uint32_t, GameFont*>> m_getGlyphHook;
		std::optional<Host::Hook<void, uintptr_t>> m_buildFontCacheHook;
		std::optional<Host::Hook<uintptr_t, uintptr_t, const uint8_t**, uintptr_t>> m_layOutCharacterHook;
		std::optional<Host::Hook<int, uintptr_t, uint16_t>> m_buildFontHook;
		std::optional<Host::Hook<uintptr_t, uintptr_t>> m_unloadFontHook;
		std::unique_ptr<TextShaper> m_shaper;

		// Italic bits of the layout state's flags and of GameFontSet.DrawFlags (node italics go there only when drawing; the italic macro sets the
		// copy's), and both flags' offsets, -1 if unknown (italics stay sheared).
		uint32_t m_stateItalicFlag = 0;
		uint32_t m_setItalicFlag = 0;
		int m_stateFlagsOffset = -1;
		int m_setFlagsOffset = -1;

		// What LayOutCharacter lays out a character a font has no glyph of as, in order of preference (packed UTF-8 values).
		std::vector<int32_t> m_missingGlyphSubstitutes;

		// Zero-width glyph for the characters of a shaped cluster after its first.
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

		// Atlases that ran out of room since they were last evicted from, and when each last was.
		std::set<GlyphAtlas*> m_fullAtlases;
		std::map<GlyphAtlas*, uint64_t> m_lastEviction;

		// Placed glyphs made from game glyphs, whose pixels are read from the game's textures at the next upload.
		std::vector<GameGlyph*> m_gameReads;
		GameTextureReader m_gameTextures;
		std::unique_ptr<GlyphRasterizer> m_rasterizer;

		// One atlas per game font family so one family's text can't push another's out; AXIS (most UI text) has its own page size and count.
		std::map<std::string, std::unique_ptr<GlyphAtlas>, LessIgnoringCase> m_atlases;
		FontReplacementAtlasConfig m_atlasConfig;

		// In copies' texture slots without a page; drawn by a glyph that slipped through with a game texture index. Cleared at the first upload.
		std::optional<Host::Texture> m_blankTexture;
		bool m_blankCleared = false;

		std::map<std::pair<GameFont*, int>, GameFont*> m_copiesByKey;
		std::map<GameFont*, CopyInfo> m_copies;
		std::map<std::tuple<ReplacementFace*, int, std::string>, std::unique_ptr<SizedFont>> m_sizedFonts;

		std::unique_ptr<ReplacementFace> m_builtInFace;

		// Face of game fonts no preset covers: their own glyphs, with fallbacks for what they lack.
		std::unique_ptr<ReplacementFace> m_gameFace;
		std::map<std::string, std::unique_ptr<ReplacementFace>, LessIgnoringCase> m_presetFaces;
		std::map<GameFont*, ReplacementFace*> m_faceByFont;
		bool m_enabled = true;

		FontReplacementEdgeConfig m_edge;
		EdgeShader m_edgeShader;

		SubpixelSettings m_subpixel;
		SubpixelText m_subpixelText;

	public:
		// Resolves what it uses of the game and hooks it. Game thread. Throws if this game version can't be worked with.
		FontReplacer();
		FontReplacer(const FontReplacer&) = delete;
		FontReplacer& operator=(const FontReplacer&) = delete;
		~FontReplacer();

		// The shaper also finds line breaks.
		[[nodiscard]] TextShaper& Shaper() const { return *m_shaper; }

		// Game thread, when earlier drawn text no longer matches (replacement switched or glyphs rebuilt), for what keeps drawn text (nameplate bakes).
		std::function<void()> TextInvalidated;

		// System fonts, which draw what faces lack glyph by glyph.
		[[nodiscard]] ReplacementFace& BuiltInFace() const { return *m_builtInFace; }

		// Preset faces for game fonts of their names (game glyphs for the rest, or all if empty); missing characters from system fonts if
		// systemFallback, else the game's. Game thread, between frames; every glyph is made again.
		void SetPreset(const Presets::Faces& preset, bool systemFallback);

		ReplacementFace& GetFace(GameFont* original);

		// Edge outline (FontEdgePS) width per text size: the shader's sample step, one texel of the font's claimed texture width; above 1 px glyph
		// boxes get empty margins, as the edge is only drawn within a glyph's box and one pixel around it.
		[[nodiscard]] const FontReplacementEdgeConfig& Edge() const { return m_edge; }

		// Game thread; glyphs are made again if a size's edge margin changed.
		void SetEdge(FontReplacementEdgeConfig edge);

		[[nodiscard]] const SubpixelSettings& Subpixel() const { return m_subpixel; }

		// Game thread, between frames; switching it on or off remakes the atlases and every glyph.
		void SetSubpixel(const SubpixelSettings& settings);

		[[nodiscard]] const FontReplacementAtlasConfig& Atlas() const { return m_atlasConfig; }

		// Game thread, between frames; a change remakes the atlases and every glyph.
		void SetAtlas(const FontReplacementAtlasConfig& config);

		// Texture width a copy at px claims for an edge (clamped settings): the edge shader's radius is atlasSize over it.
		[[nodiscard]] static uint16_t GetClaimedTextureWidth(const FontReplacementEdgeConfig& edge, int atlasSize, float px);

		// Of a family's faces (name, size), the one text at px is made with alone; nullopt if none is of that size, so glyphs come from all.
		[[nodiscard]] static std::optional<size_t> GetExactFace(std::span<const std::pair<std::string, float>> faces, float px);

		[[nodiscard]] static int GetEdgeMargin(const FontReplacementEdgeConfig& edge, float px);

		// Pads a glyph with empty pixels for a wide edge, which is only drawn within a glyph's box and the pixel around it.
		[[nodiscard]] RasterGlyph FinishGlyph(const SizedFont& sized, RasterGlyph r) const;

		// Re-rasterizes everything with current settings. Game thread, between frames; baked nameplates keep their pixels until the game rebakes them.
		void RebuildGlyphs();

		// Game thread.
		[[nodiscard]] bool Enabled() const { return m_enabled; }
		void SetEnabled(bool value);

		// Fills game glyph cells from the game's textures, then uploads the atlas's changes. Present thread, once a frame.
		void Upload();

		// Pixels go to the atlas when first drawn; for a glyph made from a game glyph they are read from source then (r is blank).
		GameGlyph* PlaceCell(const SizedFont& sized, RasterGlyph r, uint32_t utf8Value, std::optional<GameTextureSource> source = std::nullopt);

		void MarkRealItalic(GameGlyph* cell) { CellOf(cell).RealItalic = true; }

	private:
		void DisposePresetFaces();

		// Shared by the family's fonts at the size, going to its family's atlas.
		SizedFont* GetSized(GameFont* original, int halfPx);

		// The preset's face of the name, else the game's.
		ReplacementFace& FaceOf(const std::string& faceName);

		GlyphAtlas* GetAtlas(const std::string& family);

		void RemakeAtlases();

		static void ForgetPickedFonts();

		// A font cache keeps glyph pointers laid out at the node's unscaled size while draws pick the on-screen size's
		// font; copy glyphs only fit their copy and must not outlive the replacement, so while enabled nodes get no cache and lay out at each draw.
		void BuildFontCacheDetour(uintptr_t node);

		// Text node font caches are freed while enabled and rebuilt (for nodes asking for one) while disabled.
		void UpdateFontCaches();
		void UpdateFontCaches(uintptr_t uld, int depth);

		// The game rebuilt a font in place (reload, lobby/game font switch), freeing the glyph map and glyphs its copies borrow: they retake them.
		int BuildFontDetour(uintptr_t manager, uint16_t index);
		void OnFontBuilt(GameFont* font);

		// The game is about to release its fonts' texture handles: glyph pixels not read from those textures yet are dropped, to be copied again after BuildFont.
		uintptr_t UnloadFontDetour(uintptr_t manager);
		void OnFontsUnloading(const GameFontManager& manager);

		GameFont* PickFontDetour(GameFontSet* set, uint8_t useCache);
		uintptr_t LayOutCharacterDetour(uintptr_t analyzer, const uint8_t** text, uintptr_t state);

		// Italic macros are seen by measuring too; text node italics only by drawing (the bit is in the font set's own flags, before any macro).
		ItalicMode GetItalicMode(GameFontSet* set) const;

		// For a glyph GetGlyph has nothing for, the game takes the geta mark (U+3013), '-' or U+3400 straight from the game font's glyph map, which
		// would be drawn with a game texture index; the copy's own glyph of those is given instead, else an empty one. Never nullptr.
		GameGlyph* GetMissingGlyph(GameFontSet* set, const CopyInfo& info, uint32_t utf8Value);

		GameGlyph* GetGlyphDetour(GameFontSet* set, uint32_t utf8Value, GameFont* font);

		// The copy's glyph, a game glyph copied into the atlas, or nullptr if neither the face nor the game has it.
		GameGlyph* FindCopyGlyph(GameFontSet* set, const CopyInfo& info, uint32_t utf8Value);

		// Drawn from the game's texture, the edge would step by atlas-width texels (a font's textures share one width), so the game glyph is copied
		// into the atlas, scaled; pixels are read at the next upload. nullptr if no source has it readable.
		GameGlyph* GetGameGlyph(SizedFont& sized, uint32_t utf8Value);

		// A game glyph of font (a game font) copied into sized's atlas; nullptr if its texture can't be read.
		GameGlyph* CopyGameGlyph(const SizedFont& sized, GameFont* font, const GameGlyph& game, uint32_t utf8Value);

		// GetGlyph's answer for the copy's game font, copied; an empty glyph of its advance if unreadable.
		GameGlyph* ConvertGameGlyph(SizedFont& sized, GameFont* font, const GameGlyph* game, uint32_t utf8Value);

		static RasterGlyph WithSourcePixels(const RasterGlyph& box, const GameTextureSource& s, std::vector<uint8_t> sourceAlpha);

		// Borrows the game font's glyph map, glyphs and flags, keeping the copy's size, metrics and pages; redone after BuildFont frees them.
		void Sync(GameFont* copy, CopyInfo& info);

		GameFont* GetOrCreateCopy(GameFont* original, int halfPx);

		// Every texture slot: the atlas's page of that index, else the blank texture.
		void ApplyPages(GameFont* copy, CopyInfo& info);

		// Pages may be added on another thread (EvictPlane); slots only change here, on the game thread, before the copy's text is drawn.
		void RefreshPages(GameFont* copy, CopyInfo& info);

		// nullptr to leave it to the game.
		GameGlyph* CreateGlyph(SizedFont& sized, uint32_t utf8Value);

		// A glyph that finds no room draws nothing this time (same advance), and EvictPlane makes room at the next upload.
		GameGlyph* WithPixels(GameGlyph* glyph);

		// false if there is no room; a game glyph's pixels are read at the next upload.
		bool WritePixels(GameGlyph* glyph, Cell& cell, uint64_t drawn);

		// Same advance, draws nothing; for a glyph whose pixels found no room yet.
		GameGlyph* GetBlank(GameGlyph* glyph);

		// Empties the plane where glyphs undrawn for KeepDrawnMs take the most room (if none, evicts least recent until half is free); recent ones are re-placed.
		// Present thread, after this frame's layout: the next frame lays out with the new places and the following upload writes them.
		void EvictPlane(GlyphAtlas* atlas);

		// texture index (page) * planes per page + channel.
		static int PlaneOf(const GameGlyph* glyph);

		static void WriteCell(const GameGlyph* glyph, const Cell& cell);

		// For a glyph whose pixels found no room: draws nothing, advancing as measured.
		static void DropPixels(GameGlyph* glyph);

		GameGlyph* AllocateGlyph();

		static Cell& CellOf(GameGlyph* glyph) { return CONTAINING_RECORD(glyph, GlyphSlot, Glyph)->Cell; }

		void FreeGlyphs();
	};
}
