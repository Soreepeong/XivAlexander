#pragma once

// Font structures of the game's UI text renderer. Those whose fields moved over the patches (the font manager, font sets
// and fonts) are read at the offsets the game's code says (GameLayout, from game_font_signatures.json); those that kept
// their layout since 2020 are C++ structures, checked against the code. Addresses in comments are of 7.56h (image base
// 0x140000000). RE write-up: private-scratch/ffxiv/ui/font_rendering_756h.md.
namespace XivAlexander::Apps::MainApp::FontReplacement {
	struct GameFont;
	struct GameFontSet;

	// The UI's font manager (AtkFontManager, embedded in AtkModule; AtkStage points to it): the fonts and the font sets made
	// of them. Opaque: only used through pointers.
	struct GameFontManager {
		GameFontManager() = delete;

		// Gets the number of font sets: one per font type a text node selects.
		static int FontSetCount;

		// Gets the font manager of the UI, or nullptr before it is set up.
		static GameFontManager* Instance();

		// The font sets, indexed by font type.
		[[nodiscard]] GameFontSet* FontSets() const;

		// The fonts, FontCount() of them, GameFont::StructSize apart.
		[[nodiscard]] GameFont* Fonts() const;

		[[nodiscard]] uint16_t FontCount() const;

		// Gets a font set; nullptr if there are none yet.
		[[nodiscard]] GameFontSet* FontSet(int index) const;

		// Gets a font of Fonts().
		[[nodiscard]] GameFont* Font(int index) const;

		// Gets the index of a font in Fonts(); -1 if it isn't one of them.
		[[nodiscard]] int IndexOf(const GameFont* font) const;

		static void Resolve();
	};

	// Resolves the font structures' layouts, within GameLayout::Resolve.
	void ResolveGameFontStructs();

	// The fixed tables the game loads its fonts from (FontTables): the game's, and the lobby's two. Gets the most textures a
	// font of the tables has (the indices from there on are free in every font); the global client's if the tables aren't
	// found.
	[[nodiscard]] int GetMaxGameFontTextureCount();

	// The fonts of one font type, and the per-draw state the text analyzers keep in it. Opaque: only used through pointers.
	struct GameFontSet {
		GameFontSet() = delete;

		// Gets the size of a font set, the stride of GameFontManager::FontSets.
		static int StructSize;

		// The font size requested for the current run after the node's scale.
		[[nodiscard]] float ScaledSizeY() const;

		// The node's screen scale (lengths of its transform's rows; AtkTextNodeRenderer vf3).
		float& NodeScaleX();
		float& NodeScaleY();

		// The font picked last, and the size it was picked for (PickFont reuses it while the size is unchanged).
		GameFont*& CurrentFont();
		float& CurrentFontSize();

		static void Resolve();
	};

	// A glyph as the renderer reads it (12 bytes), converted from the FDT's 16-byte entry. There is no left bearing: the quad
	// starts at the pen, and the pen advances by Width + OffsetX (+ kerning).
	struct GameGlyph {
		// The codepoint's UTF-8 bytes, big-endian packed (U+3013 is 0xE38093).
		uint32_t Utf8Value;

		// X (12 bits), Y (12 bits), channel (2 bits: R, G, B, A of a B8G8R8A8 texel) and texture index (6 bits).
		uint32_t Packed;

		uint8_t Width;
		uint8_t Height;
		int8_t OffsetX;

		// From the top of the line.
		int8_t OffsetY;

		[[nodiscard]] int X() const { return static_cast<int>(Packed & 0xFFF); }
		[[nodiscard]] int Y() const { return static_cast<int>((Packed >> 12) & 0xFFF); }
		[[nodiscard]] int Channel() const { return static_cast<int>((Packed >> 24) & 3); }
		[[nodiscard]] int TextureIndex() const { return static_cast<int>(Packed >> 26); }

		static uint32_t Pack(int x, int y, int channel, int textureIndex) {
			return static_cast<uint32_t>(x & 0xFFF) | (static_cast<uint32_t>(y & 0xFFF) << 12) | (static_cast<uint32_t>(channel & 3) << 24) | (static_cast<uint32_t>(textureIndex) << 26);
		}
	};
	static_assert(sizeof(GameGlyph) == 0x0C);

	// A kerning pair (12 bytes), sorted by (Left, Right); binary searched by FUN_14064FD10.
	struct GameKerningEntry {
		uint32_t Left;
		uint32_t Right;
		int32_t Adjustment;
	};
	static_assert(sizeof(GameKerningEntry) == 0x0C);

	// MSVC std::unordered_map<uint, GameGlyph*> (0x40 bytes): a doubly linked list of all nodes, and per bucket the first and
	// last node of its run. FNV-1a over the key's 4 bytes (GlyphMapFind).
	struct GameGlyphMap {
		struct Node {
			Node* Next;
			Node* Prev;
			uint32_t Key;
			GameGlyph* Value;
		};

		float MaxLoadFactor;

		// The list's sentinel node.
		Node* Head;
		uint64_t Count;

		// Pairs of (first, last) node per bucket; both are Head for an empty bucket.
		Node** Buckets;
		uint64_t Unknown20;
		uint64_t Unknown28;
		uint64_t Mask;
		uint64_t MaxIndex;

		// Finds a key's node without inserting (unlike the game's lookup), or returns nullptr.
		[[nodiscard]] Node* Find(uint32_t key) const;
	};
	static_assert(sizeof(GameGlyphMap) == 0x40);
	static_assert(sizeof(GameGlyphMap::Node) == 0x20);

	// A font: an FDT's metrics, its glyphs converted to GameGlyph, and its atlas textures. Built from the FDT by BuildFont;
	// the FDT resource handle is released afterwards. Opaque: only used through pointers; copied with CopyFrom.
	struct GameFont {
		GameFont() = delete;

		// Gets the size of a font, the stride of GameFontManager::Fonts.
		static int StructSize;

		// Gets the most textures a font can have: the text renderer keeps vertex counts and buffers for that many.
		static int MaxTextures;

		uint32_t& KerningCount();

		// Also written to every vertex: FontEdgeVS and FontGlareVS take 1 / this as the texel step on both axes.
		uint16_t& TextureWidth();
		uint16_t& TextureHeight();

		// The size the glyphs are drawn at; the renderer scales them by the requested size / this.
		float& Size();
		int& LineHeight();
		int& Ascent();

		// std::unordered_map<uint, GameGlyph*>* keyed by GameGlyph::Utf8Value. The game looks up with operator[], which
		// inserts a null entry on a miss.
		[[nodiscard]] GameGlyphMap* GlyphMap() const;

		// A font whose glyph is used instead when it is narrower (times SecondaryRatio).
		GameFont*& Secondary();
		float& SecondaryRatio();
		uint16_t& TextureCount();

		// Gets whether the font is built (BuildFont); a font that isn't may have no glyph map, or a freed one.
		[[nodiscard]] bool IsReady() const;

		// Sets how far the glyph after italics moves right, for the overhang of the sheared glyphs before it; does nothing
		// if the game's code doesn't say where it is (XShift).
		void SetItalicCorrection(int8_t value);

		// Gets a texture resource handle (MaxTextures of them; the first is of texture 0).
		[[nodiscard]] uintptr_t GetTextureResourceHandle(int index) const;

		// Gets an atlas texture the renderer binds (a Kernel::Texture*); glyphs refer to them by index.
		[[nodiscard]] uintptr_t GetTexture(int index) const;
		void SetTexture(int index, uintptr_t texture);

		// Copies a whole font.
		void CopyFrom(const GameFont* source);

		// Gets the glyph of a codepoint, without adding a map entry as the game's lookup does; nullptr if none.
		[[nodiscard]] GameGlyph* FindGlyph(char32_t codepoint) const;
		[[nodiscard]] bool HasGlyph(char32_t codepoint) const { return FindGlyph(codepoint) != nullptr; }

		static void Resolve();
	};

	// Conversions between codepoints and the game's packed UTF-8 values.
	namespace GameUtf8 {
		// The length of a UTF-8 sequence by its first byte, as the game steps through text (LayOutCharacter).
		[[nodiscard]] inline int SequenceLength(uint8_t b) {
			return b < 0xC0 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : b < 0xF8 ? 4 : 1;
		}

		// Packs a UTF-8 sequence's bytes big-endian, the way the game keys glyphs.
		[[nodiscard]] inline uint32_t PackSequence(const uint8_t* p, int length) {
			uint32_t v = 0;
			for (auto i = 0; i < length; i++)
				v = (v << 8) | p[i];
			return v;
		}

		// Decodes a UTF-8 sequence; -1 if it isn't valid UTF-8 (or is shorter than it says).
		[[nodiscard]] int Decode(const uint8_t* p, int length);

		// Gets the codepoint of a packed UTF-8 value, or -1 if it isn't valid UTF-8.
		[[nodiscard]] int Unpack(uint32_t value);
	}
}
