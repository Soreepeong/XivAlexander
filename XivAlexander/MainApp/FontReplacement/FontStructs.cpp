#include "pch.h"
#include "MainApp/FontReplacement/FontStructs.h"

#include "MainApp/FontReplacement/GameLayout.h"
#include "MainApp/FontReplacement/GameUi.h"
#include "MainApp/FontReplacement/Host.h"
#include "MainApp/FontReplacement/Utilities.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	struct {
		int StageFontManager;
		int FontSets;
		int Fonts;
		int FontCount;
	} s_manager{};

	struct {
		int ScaledSizeY;
		int NodeScaleX;
		int NodeScaleY;
		int CurrentFont;
		int CurrentFontSize;
	} s_set{};

	struct {
		int TextureResourceHandles;
		int Textures;
		int KerningCount;
		int TextureWidth;
		int TextureHeight;
		int Size;
		int LineHeight;
		int Ascent;
		int GlyphMap;
		int Secondary;
		int SecondaryRatio;
		int TextureCount;
		int Flags;
		int ReadyFlag;
		int ItalicCorrection = -1;
	} s_font{};
}

int FontReplacement::GameFontManager::FontSetCount = 0;
int FontReplacement::GameFontSet::StructSize = 0;
int FontReplacement::GameFont::StructSize = 0;
int FontReplacement::GameFont::MaxTextures = 0;

FontReplacement::GameFontManager* FontReplacement::GameFontManager::Instance() {
	const auto stage = GameUi::Stage();
	return stage ? At<GameFontManager*>(reinterpret_cast<void*>(stage), s_manager.StageFontManager) : nullptr;
}

FontReplacement::GameFontSet* FontReplacement::GameFontManager::FontSets() const {
	return At<GameFontSet*>(this, s_manager.FontSets);
}

FontReplacement::GameFont* FontReplacement::GameFontManager::Fonts() const {
	return At<GameFont*>(this, s_manager.Fonts);
}

uint16_t FontReplacement::GameFontManager::FontCount() const {
	return At<uint16_t>(this, s_manager.FontCount);
}

FontReplacement::GameFontSet* FontReplacement::GameFontManager::FontSet(int index) const {
	const auto sets = FontSets();
	return sets ? reinterpret_cast<GameFontSet*>(reinterpret_cast<uint8_t*>(sets) + static_cast<ptrdiff_t>(index) * GameFontSet::StructSize) : nullptr;
}

FontReplacement::GameFont* FontReplacement::GameFontManager::Font(int index) const {
	return reinterpret_cast<GameFont*>(reinterpret_cast<uint8_t*>(Fonts()) + static_cast<ptrdiff_t>(index) * GameFont::StructSize);
}

int FontReplacement::GameFontManager::IndexOf(const GameFont* font) const {
	const auto fonts = Fonts();
	if (!fonts)
		return -1;
	const auto offset = reinterpret_cast<const uint8_t*>(font) - reinterpret_cast<const uint8_t*>(fonts);
	if (offset < 0 || offset % GameFont::StructSize != 0 || offset / GameFont::StructSize >= FontCount())
		return -1;
	return static_cast<int>(offset / GameFont::StructSize);
}

void FontReplacement::GameFontManager::Resolve() {
	GameUi::ResolveStage();
	s_manager.StageFontManager = GameLayout::Get("AtkStage.AtkFontManager");
	s_manager.FontSets = GameLayout::Get("AtkFontManager.FontSets");
	s_manager.Fonts = GameLayout::Get("AtkFontManager.Fonts");
	s_manager.FontCount = GameLayout::Get("AtkFontManager.FontCount");
	FontSetCount = GameLayout::Get("AtkFontManager.FontSetCount");
}

void FontReplacement::ResolveGameFontStructs() {
	GameFontManager::Resolve();
	GameFontSet::Resolve();
	GameFont::Resolve();
	GameLayout::CheckFixed("GameGlyph", sizeof(GameGlyph), {
		{"Utf8Value", offsetof(GameGlyph, Utf8Value)},
		{"Packed", offsetof(GameGlyph, Packed)},
		{"Width", offsetof(GameGlyph, Width)},
		{"Height", offsetof(GameGlyph, Height)},
		{"OffsetX", offsetof(GameGlyph, OffsetX)},
		{"OffsetY", offsetof(GameGlyph, OffsetY)},
	});
	GameLayout::CheckFixed("GameKerningEntry", sizeof(GameKerningEntry), {
		{"Left", offsetof(GameKerningEntry, Left)},
		{"Right", offsetof(GameKerningEntry, Right)},
		{"Adjustment", offsetof(GameKerningEntry, Adjustment)},
	});
	GameLayout::CheckFixed("GlyphMap", sizeof(GameGlyphMap), {
		{"MaxLoadFactor", offsetof(GameGlyphMap, MaxLoadFactor)},
		{"Head", offsetof(GameGlyphMap, Head)},
		{"Count", offsetof(GameGlyphMap, Count)},
		{"Buckets", offsetof(GameGlyphMap, Buckets)},
		{"Mask", offsetof(GameGlyphMap, Mask)},
		{"MaxIndex", offsetof(GameGlyphMap, MaxIndex)},
	});
	GameLayout::CheckFixed("GlyphMap.Node", sizeof(GameGlyphMap::Node), {
		{"Next", offsetof(GameGlyphMap::Node, Next)},
		{"Prev", offsetof(GameGlyphMap::Node, Prev)},
		{"Key", offsetof(GameGlyphMap::Node, Key)},
		{"Value", offsetof(GameGlyphMap::Node, Value)},
	});
}

int FontReplacement::GetMaxGameFontTextureCount() {
	int count = 0, entrySize = 0, textureCount = 0;
	uintptr_t tables[3]{};
	try {
		GameLayout::Resolve("Reading the game's font tables", [&] {
			count = GameLayout::Get("FontTable.Count");
			entrySize = GameLayout::Get("FontTableEntry");
			textureCount = GameLayout::Get("FontTableEntry.TextureCount");
			tables[0] = GameLayout::Address("FontTables", "GameTable");
			tables[1] = GameLayout::Address("FontTables", "LobbyTableA");
			tables[2] = GameLayout::Address("FontTables", "LobbyTableB");
		});
	} catch (const std::exception& e) {
		const auto fallback = GameLayout::TryGet("FontTable.MaxTextureCount");
		if (!fallback)
			throw;
		Host::Warning("The font tables weren't found; taking the global client's texture count: {}", e.what());
		return *fallback;
	}

	auto max = 0;
	for (const auto table : tables) {
		for (auto i = 0; i < count; i++)
			max = (std::max)(max, static_cast<int>(*reinterpret_cast<const uint64_t*>(table + static_cast<size_t>(i) * entrySize + textureCount)));
	}
	return max;
}

float FontReplacement::GameFontSet::ScaledSizeY() const {
	return At<float>(this, s_set.ScaledSizeY);
}

float& FontReplacement::GameFontSet::NodeScaleX() {
	return At<float>(this, s_set.NodeScaleX);
}

float& FontReplacement::GameFontSet::NodeScaleY() {
	return At<float>(this, s_set.NodeScaleY);
}

FontReplacement::GameFont*& FontReplacement::GameFontSet::CurrentFont() {
	return At<GameFont*>(this, s_set.CurrentFont);
}

float& FontReplacement::GameFontSet::CurrentFontSize() {
	return At<float>(this, s_set.CurrentFontSize);
}

void FontReplacement::GameFontSet::Resolve() {
	StructSize = GameLayout::Get("GameFontSet");
	s_set.ScaledSizeY = GameLayout::Get("GameFontSet.ScaledSizeY");
	s_set.NodeScaleX = GameLayout::Get("GameFontSet.NodeScaleX");
	s_set.NodeScaleY = GameLayout::Get("GameFontSet.NodeScaleY");
	s_set.CurrentFont = GameLayout::Get("GameFontSet.CurrentFont");
	s_set.CurrentFontSize = GameLayout::Get("GameFontSet.CurrentFontSize");
}

FontReplacement::GameGlyphMap::Node* FontReplacement::GameGlyphMap::Find(uint32_t key) const {
	auto hash = 0xCBF29CE484222325ull;
	for (auto i = 0; i < 4; i++)
		hash = (hash ^ ((key >> (8 * i)) & 0xFF)) * 0x100000001B3ull;

	const auto bucket = (hash & Mask) * 2;
	const auto first = Buckets[bucket];
	auto node = Buckets[bucket + 1];
	if (node == Head)
		return nullptr;
	while (true) {
		if (node->Key == key)
			return node;
		if (node == first)
			return nullptr;
		node = node->Prev;
	}
}

uint32_t& FontReplacement::GameFont::KerningCount() {
	return At<uint32_t>(this, s_font.KerningCount);
}

uint16_t& FontReplacement::GameFont::TextureWidth() {
	return At<uint16_t>(this, s_font.TextureWidth);
}

uint16_t& FontReplacement::GameFont::TextureHeight() {
	return At<uint16_t>(this, s_font.TextureHeight);
}

float& FontReplacement::GameFont::Size() {
	return At<float>(this, s_font.Size);
}

int& FontReplacement::GameFont::LineHeight() {
	return At<int>(this, s_font.LineHeight);
}

int& FontReplacement::GameFont::Ascent() {
	return At<int>(this, s_font.Ascent);
}

FontReplacement::GameGlyphMap* FontReplacement::GameFont::GlyphMap() const {
	return At<GameGlyphMap*>(this, s_font.GlyphMap);
}

FontReplacement::GameFont*& FontReplacement::GameFont::Secondary() {
	return At<GameFont*>(this, s_font.Secondary);
}

float& FontReplacement::GameFont::SecondaryRatio() {
	return At<float>(this, s_font.SecondaryRatio);
}

uint16_t& FontReplacement::GameFont::TextureCount() {
	return At<uint16_t>(this, s_font.TextureCount);
}

bool FontReplacement::GameFont::IsReady() const {
	return (At<uint8_t>(this, s_font.Flags) & s_font.ReadyFlag) != 0;
}

void FontReplacement::GameFont::SetItalicCorrection(int8_t value) {
	if (s_font.ItalicCorrection >= 0)
		At<int8_t>(this, s_font.ItalicCorrection) = value;
}

uintptr_t FontReplacement::GameFont::GetTextureResourceHandle(int index) const {
	return (&At<uintptr_t>(this, s_font.TextureResourceHandles))[index];
}

uintptr_t FontReplacement::GameFont::GetTexture(int index) const {
	return (&At<uintptr_t>(this, s_font.Textures))[index];
}

void FontReplacement::GameFont::SetTexture(int index, uintptr_t texture) {
	(&At<uintptr_t>(this, s_font.Textures))[index] = texture;
}

void FontReplacement::GameFont::CopyFrom(const GameFont* source) {
	std::memcpy(this, source, StructSize);
}

FontReplacement::GameGlyph* FontReplacement::GameFont::FindGlyph(char32_t codepoint) const {
	// The game keys glyphs by their UTF-8 bytes, big-endian; there are none past U+10FFFF.
	const auto map = GlyphMap();
	if (!map || codepoint > 0x10FFFF)
		return nullptr;
	const auto node = map->Find(xivres::util::unicode::u32_to_u8uint32(codepoint));
	return node ? node->Value : nullptr;
}

void FontReplacement::GameFont::Resolve() {
	StructSize = GameLayout::Get("GameFont");
	MaxTextures = GameLayout::Get("GameFont.MaxTextures");
	s_font.TextureResourceHandles = GameLayout::Get("GameFont.TextureResourceHandles");
	s_font.Textures = GameLayout::Get("GameFont.Textures");
	s_font.KerningCount = GameLayout::Get("GameFont.KerningCount");
	s_font.TextureWidth = GameLayout::Get("GameFont.TextureWidth");
	s_font.TextureHeight = GameLayout::Get("GameFont.TextureHeight");
	s_font.Size = GameLayout::Get("GameFont.Size");
	s_font.LineHeight = GameLayout::Get("GameFont.LineHeight");
	s_font.Ascent = GameLayout::Get("GameFont.Ascent");

	// The glyph map replaced a sorted glyph array in 7.40; only glyphs of the map are looked up.
	s_font.GlyphMap = GameLayout::Get("GameFont.GlyphMap");
	s_font.Secondary = GameLayout::Get("GameFont.Secondary");
	s_font.SecondaryRatio = GameLayout::Get("GameFont.SecondaryRatio");
	s_font.TextureCount = GameLayout::Get("GameFont.TextureCount");
	s_font.Flags = GameLayout::Get("GameFont.Flags");
	s_font.ReadyFlag = GameLayout::Get("GameFont.Flags.Ready");

	// Optional: only changed in copies.
	s_font.ItalicCorrection = GameLayout::TryGet("GameFont.XShift").value_or(-1);
}

int FontReplacement::GameUtf8::Decode(const uint8_t* p, int length) {
	if (length <= 0)
		return -1;
	const auto b = p[0];
	int expected, cp, min;
	if (b < 0x80)
		return length == 1 ? b : -1;
	if (b < 0xC2)
		return -1;
	if (b < 0xE0)
		expected = 2, cp = b & 0x1F, min = 0x80;
	else if (b < 0xF0)
		expected = 3, cp = b & 0x0F, min = 0x800;
	else if (b < 0xF5)
		expected = 4, cp = b & 0x07, min = 0x10000;
	else
		return -1;
	if (length != expected)
		return -1;
	for (auto i = 1; i < expected; i++) {
		if ((p[i] & 0xC0) != 0x80)
			return -1;
		cp = (cp << 6) | (p[i] & 0x3F);
	}
	if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
		return -1;
	return cp;
}

int FontReplacement::GameUtf8::Unpack(uint32_t value) {
	uint8_t buf[4];
	const auto n = value > 0xFFFFFF ? 4 : value > 0xFFFF ? 3 : value > 0xFF ? 2 : 1;
	for (auto i = 0; i < n; i++)
		buf[i] = static_cast<uint8_t>(value >> (8 * (n - 1 - i)));
	return Decode(buf, n);
}
