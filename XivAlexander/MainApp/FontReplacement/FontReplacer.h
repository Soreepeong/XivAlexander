#pragma once

#include "Config/FontReplacementConfigs.h"
#include "MainApp/FontReplacement/EdgeShader.h"
#include "MainApp/FontReplacement/FontStructs.h"
#include "MainApp/FontReplacement/GameTextureReader.h"
#include "MainApp/FontReplacement/GlyphAtlas.h"
#include "MainApp/FontReplacement/Presets.h"
#include "MainApp/FontReplacement/ReplacementFace.h"
#include "MainApp/FontReplacement/TextShaper.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// A game glyph's pixels: a rectangle of a plane of a game font texture (a Kernel::Texture), whose top left is at (Left,
	// Top) from the pen in the game font's pixels, scaled by Scale.
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

	// The glyphs of a face at one pixel size, shared by the copies of the game fonts of that face at that size (a font and its
	// lobby version). GameFont is the first of them, for the game's glyphs and metrics.
	struct SizedFont {
		ReplacementFace* Face;
		float Px;
		int Ascent;
		int LineHeight;
		GameFont* Game;

		// The atlas of the font's family, which its glyphs go to.
		GlyphAtlas* Atlas;

		// By packed UTF-8 value; nullptr for a glyph left to the game.
		std::unordered_map<uint32_t, GameGlyph*> Glyphs;
	};

	// Makes the game's text renderer draw glyphs rasterized at the size they are drawn at.
	//
	// Two hooks, both on the game's thread:
	// 1. The font picker (FUN_140651DC0) chooses the game font for the requested size, which already includes the text
	//    node's screen scale. The detour swaps it for a copy whose size is exactly that size (in half pixels), so the
	//    renderer's scale (requested size / font size) is 1. The copy keeps the game font's glyph map and textures, with the
	//    atlas pages added after them.
	// 2. The glyph lookup (FUN_14064FC50) is answered for a copy from its own glyphs, rasterized on first use. A codepoint no
	//    family has, and the private-use icons, fall through to the game's glyph, which the copy can still draw from the
	//    game's textures (at the game font's own size).
	//
	// Text is laid out again every frame, so nothing keeps glyph pointers between frames. What a copy borrows from its game
	// font is taken again when the game builds that font again (BuildFont, the only place it does).
	class FontReplacer {
		enum class CellState : uint8_t {
			// No pixels: an empty glyph, a blank, or one whose pixels couldn't be placed.
			None,

			// Pixels waiting to go to the atlas when the glyph is next drawn.
			Pending,

			// In the atlas, where GameGlyph::Packed says.
			Placed,
		};

		// What is kept of a glyph made here.
		struct Cell {
			CellState State = CellState::None;

			// The atlas its pixels go to.
			GlyphAtlas* Atlas = nullptr;

			// Its coverage while pending (and for a game glyph, until its pixels are read), with the ink's left in the box and
			// its top row in the box.
			RasterGlyph Raster;
			int BoxLeft = 0;
			int InkTop = 0;

			// For a glyph made from a game glyph: where its pixels are read from.
			std::optional<GameTextureSource> Source;

			// When it was last drawn (GetTickCount64 of the frame), once placed.
			uint64_t Drawn = 0;

			// The glyph drawn instead while its pixels find no room; nullptr until one is needed.
			GameGlyph* Blank = nullptr;

			// Whether it is a real italic glyph, which the game must not shear.
			bool RealItalic = false;
		};

		// A glyph made here, where the game reads it (it must not move), and what is kept of it. Glyph pointers handed to the
		// game are of these.
		struct GlyphSlot {
			GameGlyph Glyph;
			FontReplacer::Cell Cell;
		};

		// A copy of a game font at a size in half pixels: its game font, the glyphs of its face at its size, and the glyphs
		// made from the game font's glyphs, by packed UTF-8 value (nullptr for one drawn as it is).
		struct CopyInfo {
			GameFont* Original;
			int HalfPx;
			SizedFont* Sized;
			std::unordered_map<uint32_t, GameGlyph*> GameGlyphs;
		};

		// The character the game is laying out on a thread, for GetGlyph to look up in its shaped run: whether it is laid out by
		// an analyzer that only measures (its glyph needs no pixels yet), its layout state (whose flags have italics), and
		// whether its italic bit was cleared for a real italic glyph, to be set again once the character is laid out.
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
		std::unique_ptr<TextShaper> m_shaper;

		// The italic bit of the layout state's flags, and of GameFontSet.DrawFlags (which the text node's italics go to only
		// when drawing; the italic macro sets the copy's); the offsets of both flags, -1 if unknown (italics are left sheared).
		uint32_t m_stateItalicFlag = 0;
		uint32_t m_setItalicFlag = 0;
		int m_stateFlagsOffset = -1;
		int m_setFlagsOffset = -1;

		// What FUN_1406EEC70 lays out a character a font has no glyph of as, in order of preference (packed UTF-8 values).
		std::vector<int32_t> m_missingGlyphSubstitutes;

		// A glyph of no width: the characters of a shaped cluster after its first.
		std::unique_ptr<GlyphSlot> m_emptyGlyph;
		void(*m_freeFontCache)(uintptr_t) = nullptr;
		uintptr_t m_rendererVtbl = 0;
		uintptr_t m_renderCountVtbl = 0;

		// AtkTextNode: the node's entry in the font manager's caches, 0 for none; the byte of its cache flags, and the flag that
		// asks for a cache (ToggleFontCache).
		int m_fontCacheSlotOffset = 0;
		int m_fontCacheFlagsOffset = 0;
		uint8_t m_useFontCacheFlag = 0;

		// The glyphs made, in the order they were (a deque: they must not move).
		std::deque<GlyphSlot> m_slots;
		uint64_t m_frameTick = 0;

		// The atlases that ran out of room since they were last made room in, and when that was.
		std::set<GlyphAtlas*> m_fullAtlases;
		std::map<GlyphAtlas*, uint64_t> m_lastEviction;

		// Glyphs made from game glyphs, placed in the atlas, whose pixels are read from the game's textures at the next upload.
		std::vector<GameGlyph*> m_gameReads;
		GameTextureReader m_gameTextures;
		std::unique_ptr<GlyphRasterizer> m_rasterizer;

		// The glyphs of each game font family go to an atlas of its own, so that one family's text can't push another's out:
		// AXIS, the family of most of the UI's text, has pages of the game's size, the others smaller ones.
		std::map<std::string, std::unique_ptr<GlyphAtlas>, LessIgnoringCase> m_atlases;

		std::map<std::pair<GameFont*, int>, GameFont*> m_copiesByKey;
		std::map<GameFont*, CopyInfo> m_copies;
		std::map<std::tuple<ReplacementFace*, int, std::string>, std::unique_ptr<SizedFont>> m_sizedFonts;

		// The faces glyphs come from: the preset's by game font name, or the built-in one; and each game font's.
		std::unique_ptr<ReplacementFace> m_builtInFace;

		// The face of game fonts no preset gives one: their own glyphs, with fallbacks for what they lack.
		std::unique_ptr<ReplacementFace> m_gameFace;
		std::map<std::string, std::unique_ptr<ReplacementFace>, LessIgnoringCase> m_presetFaces;
		std::map<GameFont*, ReplacementFace*> m_faceByFont;
		bool m_enabled = true;

		FontReplacementEdgeConfig m_edge;
		EdgeShader m_edgeShader;

	public:
		// Sets the replacement up: resolves what it uses of the game, and hooks it. Game thread. Throws if this version of the
		// game can't be worked with.
		FontReplacer();
		FontReplacer(const FontReplacer&) = delete;
		FontReplacer& operator=(const FontReplacer&) = delete;
		~FontReplacer();

		// Gets the shaper, which also finds line breaks.
		[[nodiscard]] TextShaper& Shaper() const { return *m_shaper; }

		// Called on the game's thread when text drawn earlier no longer matches (the replacement was switched, or the glyphs
		// were rebuilt), for what keeps drawn text around (the nameplate bakes).
		std::function<void()> TextInvalidated;

		// Gets the built-in face: system fonts, which draw what faces lack glyph by glyph.
		[[nodiscard]] ReplacementFace& BuiltInFace() const { return *m_builtInFace; }

		// Uses a preset's faces for the game fonts of their names (the game's glyphs for the others, or for all if empty),
		// drawing characters they lack with system fonts if systemFallback, else the game's. Game thread, between frames.
		// Every glyph is made again.
		void SetPreset(const Presets::Faces& preset, bool systemFallback);

		// Gets the face a game font's glyphs come from.
		ReplacementFace& GetFace(GameFont* original);

		// Gets the edge outline's (FontEdgePS) width at each text size. A font's edge width is the step the shader samples by,
		// one texel of the texture width the font claims; above 1 px, glyph boxes get empty margins for it, as the edge is only
		// drawn within a glyph's box and one pixel around it.
		[[nodiscard]] const FontReplacementEdgeConfig& Edge() const { return m_edge; }

		// Sets the edge outline's width at each text size. Game thread; glyphs are made again.
		void SetEdge(FontReplacementEdgeConfig edge);

		// Gets the sizes of the pages of a game font family's atlas.
		[[nodiscard]] static int GetAtlasSize(std::string_view family);

		// Gets the texture width a copy at a size claims for an edge (clamped settings), with pages of a size: the edge
		// shader's radius is the pages' size over it. The settings' previews draw the edge with the same radius.
		[[nodiscard]] static uint16_t GetClaimedTextureWidth(const FontReplacementEdgeConfig& edge, int atlasSize, float px);

		// Gets the size a copy's glyphs are drawn at for a size asked for: rounded to half pixels, from 4 to 255 px.
		[[nodiscard]] static float GetDrawnPx(float size);

		// Gets the empty pixels around a glyph that an edge at a size needs (FinishGlyph).
		[[nodiscard]] static int GetEdgeMargin(const FontReplacementEdgeConfig& edge, float px);

		// Makes a glyph rasterized for a size (and adjusted as its element says) ready to be placed: surrounded with empty
		// pixels for a wide edge, which is only drawn within a glyph's box and the pixel around it.
		[[nodiscard]] RasterGlyph FinishGlyph(const SizedFont& sized, RasterGlyph r) const;

		// Forgets every glyph and empties the atlases, so they are rasterized again with the current settings. Game thread,
		// between frames. Nameplates already baked keep their pixels until the game bakes them again.
		void RebuildGlyphs();

		// Gets or sets whether the game draws with the replaced fonts. Game thread.
		[[nodiscard]] bool Enabled() const { return m_enabled; }
		void SetEnabled(bool value);

		// Fills the cells of game glyphs from the game's textures, then copies the atlas's changes to the GPU. On the thread
		// that calls Present, once a frame.
		void Upload();

		// Puts a rasterized glyph or cluster into the atlas as a game glyph advancing by r.Advance. Its pixels go to the atlas
		// when it is first drawn; for a glyph made from a game glyph, they are read from source then (r is blank).
		GameGlyph* PlaceCell(const SizedFont& sized, RasterGlyph r, uint32_t utf8Value, std::optional<GameTextureSource> source = std::nullopt);

		// Marks a cell as a real italic glyph, which the game must not shear.
		void MarkRealItalic(GameGlyph* cell) { CellOf(cell).RealItalic = true; }

	private:
		void DisposePresetFaces();

		// Gets the glyphs of a face at a size, made with the metrics they have for a game font. They are shared by the fonts of
		// one name (a font and its lobby version), and go to the atlas of its family.
		SizedFont* GetSized(GameFont* original, int halfPx);

		// Gets the atlas of a game font family, made on its first use.
		GlyphAtlas* GetAtlas(const std::string& family);

		// Makes every font set pick its font again on its next use.
		static void ForgetPickedFonts();

		// A font cache keeps the glyph pointers of the text as laid out when it was built, at the node's unscaled size
		// (FUN_140665600(node, false)), and draws use them while picking the font for the on-screen size. Glyphs of a copy only
		// fit the copy of their size, and a copy's glyphs must not outlive the replacement, so while it is on nodes get no
		// cache: their text is laid out at each draw, as for nodes that never asked for one.
		void BuildFontCacheDetour(uintptr_t node);

		// Brings every loaded text node's font cache in line with Enabled: freed while on, rebuilt (for the nodes that ask for
		// one) while off.
		void UpdateFontCaches();
		void UpdateFontCaches(uintptr_t uld, int depth);

		// The game built a font again in place (a reload, a switch between the lobby's fonts and the game's), freeing the glyph
		// map and glyphs its copies borrow: they take them again, with the face of the font's name now.
		int BuildFontDetour(uintptr_t manager, uint16_t index);
		void OnFontBuilt(GameFont* font);

		GameFont* PickFontDetour(GameFontSet* set, uint8_t useCache);
		uintptr_t LayOutCharacterDetour(uintptr_t analyzer, const uint8_t** text, uintptr_t state);

		// Gets how the character being laid out is in italics: by an italic macro (which measuring sees too), or by its text
		// node (which only drawing sees: the bit is in the font set's own flags, before any macro changed the copy).
		ItalicMode GetItalicMode(GameFontSet* set) const;

		// Gets what a character a copy has no glyph for is laid out as. FUN_1406EEC70 takes the geta mark (U+3013), else '-',
		// else U+3400, from the font's glyph map itself rather than through GetGlyph, which would give the game font's glyph,
		// at its size and with the edge of its texture. The copy's own glyphs of them are given instead.
		GameGlyph* GetMissingGlyph(GameFontSet* set, uint32_t utf8Value, GameFont* font);

		GameGlyph* GetGlyphDetour(GameFontSet* set, uint32_t utf8Value, GameFont* font);

		// Gets a glyph of the atlas made from a game glyph, scaled to the copy's size: drawn from the game's texture, the edge
		// would step by texels of the atlas's width (all of a font's textures share one), not the game texture's. Its pixels
		// are read from the game's texture at the next upload. nullptr to draw the game's glyph as it is.
		GameGlyph* GetGameGlyph(CopyInfo& info, GameGlyph* game, uint32_t utf8Value);

		// Gets a glyph's box filled with its game texture's pixels (read as 8-bit coverage), scaled.
		static RasterGlyph WithSourcePixels(const RasterGlyph& box, const GameTextureSource& s, std::vector<uint8_t> sourceAlpha);

		// Takes what a copy borrows from its game font (glyph map, glyphs, textures, flags), keeping its own size, metrics and
		// pages. When the game builds the font again in place (BuildFont), what was borrowed is freed, and taken again.
		void Sync(GameFont* copy, const CopyInfo& info) const;

		GameFont* GetOrCreateCopy(GameFont* original, int halfPx);

		// Gives a copy the pages of its family's atlas, after the game font's textures.
		static void ApplyPages(GameFont* copy, const GlyphAtlas& atlas);

		void OnPageAdded(GlyphAtlas* atlas);

		// Rasterizes a glyph into the atlas; nullptr to leave it to the game.
		GameGlyph* CreateGlyph(SizedFont& sized, uint32_t utf8Value);

		// Puts a glyph's pixels into the atlas if they aren't yet, for a glyph about to be drawn, and notes that it was drawn.
		// A glyph that finds no room draws nothing this time (the same advance), and room is made at the next upload
		// (EvictPlane).
		GameGlyph* WithPixels(GameGlyph* glyph);

		// Places a pending glyph's pixels in its atlas, drawn last at drawn; false if there is no room. A game glyph's pixels
		// are read at the next upload.
		bool WritePixels(GameGlyph* glyph, Cell& cell, uint64_t drawn);

		// Gets a glyph that advances as a glyph does but draws nothing, for one whose pixels found no room yet.
		GameGlyph* GetBlank(GameGlyph* glyph);

		// Makes room in a full atlas by emptying one of its planes: the one whose glyphs not drawn in the last KeepDrawnMs take
		// the most room. Its glyphs drawn since are placed in it again (the most recently drawn first), from their pixels; the
		// others leave the atlas, keeping their pixels to be placed again when they are next drawn. If every glyph was drawn
		// recently, the plane's least recently drawn leave until half of it is free. On the thread that calls Present, after
		// this frame's text was laid out: the next frame lays out with the new places, and the upload that follows puts them in
		// the texture.
		void EvictPlane(GlyphAtlas* atlas);

		// Gets the atlas plane of a placed glyph (page * planes per page + channel).
		static int PlaneOf(const GameGlyph* glyph);

		// Writes a placed glyph's pixels to its place in the atlas.
		static void WriteCell(const GameGlyph* glyph, const Cell& cell);

		// Makes a glyph whose pixels found no room draw nothing, advancing as measured.
		static void DropPixels(GameGlyph* glyph);

		GameGlyph* AllocateGlyph();

		// Gets what is kept of a glyph made here.
		static Cell& CellOf(GameGlyph* glyph) { return CONTAINING_RECORD(glyph, GlyphSlot, Glyph)->Cell; }

		void FreeGlyphs();
	};
}
