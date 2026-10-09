#pragma once

// Structures that moved over the patches (font manager, font sets, fonts) use GameLayout offsets; the rest are C++ structs checked against the code.
// RE write-up: private-scratch/ffxiv/ui/font_rendering_756h.md and ffxiv/xivalexander/code-notes/font-replacement.md.
namespace XivAlexander::Apps::MainApp::FontReplacement {
	struct GameFont;
	struct GameFontSet;

	// AtkFontManager (embedded in AtkModule; AtkStage points to it). Opaque: only used through pointers.
	struct GameFontManager {
		GameFontManager() = delete;

		// One per font type a text node selects.
		static int FontSetCount;

		// nullptr before the UI sets it up.
		static GameFontManager* Instance();

		// Indexed by font type.
		[[nodiscard]] GameFontSet* FontSets() const;

		// FontCount() of them, GameFont::StructSize apart.
		[[nodiscard]] GameFont* Fonts() const;

		[[nodiscard]] uint16_t FontCount() const;

		// nullptr if there are none yet.
		[[nodiscard]] GameFontSet* FontSet(int index) const;

		[[nodiscard]] GameFont* Font(int index) const;

		// -1 if it isn't one of them.
		[[nodiscard]] int IndexOf(const GameFont* font) const;

		static void Resolve();
	};

	// Called within GameLayout::Resolve.
	void ResolveGameFontStructs();

	// Most textures a font of the FontTables (game's and lobby's two) has, so indices from there on are free; the global client's if not found.
	[[nodiscard]] int GetMaxGameFontTextureCount();

	// The fonts of one font type, and the per-draw state the text analyzers keep in it. Opaque: only used through pointers.
	struct GameFontSet {
		GameFontSet() = delete;

		// The stride of GameFontManager::FontSets.
		static int StructSize;

		// The font size requested for the current run after the node's scale.
		[[nodiscard]] float ScaledSizeY() const;

		// The node's screen scale (lengths of its transform's rows).
		float& NodeScaleX();
		float& NodeScaleY();

		// The font picked last, and the size it was picked for (PickFont reuses it while the size is unchanged).
		GameFont*& CurrentFont();
		float& CurrentFontSize();

		static void Resolve();
	};

	// Converted from the FDT's 16-byte entry. No left bearing: the quad starts at the pen, which advances by Width + OffsetX (+ kerning).
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

	// Sorted by (Left, Right), as the game binary searches it.
	struct GameKerningEntry {
		uint32_t Left;
		uint32_t Right;
		int32_t Adjustment;
	};
	static_assert(sizeof(GameKerningEntry) == 0x0C);

	// MSVC std::unordered_map<uint, GameGlyph*>: a doubly linked list of all nodes, per bucket its run's first and last; FNV-1a of the key (GlyphMapFind).
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

		// Doesn't insert, unlike the game's lookup.
		[[nodiscard]] Node* Find(uint32_t key) const;
	};
	static_assert(sizeof(GameGlyphMap) == 0x40);
	static_assert(sizeof(GameGlyphMap::Node) == 0x20);

	// Built from an FDT by BuildFont, which releases the FDT resource handle afterwards. Opaque: only used through pointers.
	struct GameFont {
		GameFont() = delete;

		// The stride of GameFontManager::Fonts.
		static int StructSize;

		// The text renderer keeps vertex counts and buffers for this many textures.
		static int MaxTextures;

		uint32_t& KerningCount();

		// Also written to every vertex: FontEdgeVS and FontGlareVS take 1 / this as the texel step on both axes.
		uint16_t& TextureWidth();
		uint16_t& TextureHeight();

		// The size the glyphs are drawn at; the renderer scales them by the requested size / this.
		float& Size();
		int& LineHeight();
		int& Ascent();

		// Keyed by GameGlyph::Utf8Value. The game looks up with operator[], which inserts a null entry on a miss.
		[[nodiscard]] GameGlyphMap* GlyphMap() const;

		// A font whose glyph is used instead when it is narrower (times SecondaryRatio).
		GameFont*& Secondary();
		float& SecondaryRatio();
		uint16_t& TextureCount();

		// Whether BuildFont has run; a font that isn't built may have no glyph map, or a freed one.
		[[nodiscard]] bool IsReady() const;

		// How far the glyph after italics moves right, for the sheared overhang; does nothing if the game's code doesn't say where it is (XShift).
		void SetItalicCorrection(int8_t value);

		// MaxTextures of them; the first is of texture 0.
		[[nodiscard]] uintptr_t GetTextureResourceHandle(int index) const;

		// A Kernel::Texture* the renderer binds; glyphs refer to them by index.
		[[nodiscard]] uintptr_t GetTexture(int index) const;
		void SetTexture(int index, uintptr_t texture);

		void CopyFrom(const GameFont* source);

		// Doesn't add a map entry as the game's lookup does.
		[[nodiscard]] GameGlyph* FindGlyph(char32_t codepoint) const;
		[[nodiscard]] bool HasGlyph(char32_t codepoint) const { return FindGlyph(codepoint) != nullptr; }

		static void Resolve();
	};

	namespace GameUtf8 {
		// As the game steps through text (LayOutCharacter).
		[[nodiscard]] inline int SequenceLength(uint8_t b) {
			return b < 0xC0 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : b < 0xF8 ? 4 : 1;
		}

		[[nodiscard]] inline uint32_t PackSequence(const uint8_t* p, int length) {
			uint32_t v = 0;
			for (auto i = 0; i < length; i++)
				v = (v << 8) | p[i];
			return v;
		}

		// -1 if it isn't valid UTF-8 (or is shorter than it says).
		[[nodiscard]] int Decode(const uint8_t* p, int length);

		// -1 if it isn't valid UTF-8.
		[[nodiscard]] int Unpack(uint32_t value);
	}
}
