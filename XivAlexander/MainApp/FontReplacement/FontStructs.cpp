#include "pch.h"
#include "MainApp/FontReplacement/FontStructs.h"

#include "MainApp/FontReplacement/GameCode.h"
#include "MainApp/FontReplacement/GameUi.h"
#include "MainApp/FontReplacement/Host.h"
#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	constexpr int ReadyFontFlag = 2;

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
	const auto manager = ResolveGameCode(Game::Resolved::Fonts::FontManager);
	s_manager.StageFontManager = ResolveGameCode(Game::Resolved::Fonts::Stage).FontManager;
	s_manager.FontSets = 0;  // at the start
	s_manager.Fonts = manager.Fonts;
	s_manager.FontCount = manager.FontCount;
	FontSetCount = manager.FontSetCount;
}

void FontReplacement::ResolveGameFontStructs() {
	GameFontManager::Resolve();
	GameFontSet::Resolve();
	GameFont::Resolve();
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
	StructSize = ResolveGameCode(Game::Resolved::Fonts::FontManager).FontSetSize;
	const auto set = ResolveGameCode(Game::Resolved::Fonts::FontSet);
	s_set.ScaledSizeY = set.ScaledSizeY;
	s_set.NodeScaleX = set.NodeScaleX;
	s_set.NodeScaleY = set.NodeScaleY;
	s_set.CurrentFont = set.CurrentFont;
	s_set.CurrentFontSize = set.CurrentFontSize;
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
	return (At<uint8_t>(this, s_font.Flags) & ReadyFontFlag) != 0;
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
	const auto map = GlyphMap();
	if (!map || codepoint > 0x10FFFF)
		return nullptr;
	const auto node = map->Find(xivres::util::unicode::u32_to_u8uint32(codepoint));
	return node ? node->Value : nullptr;
}

void FontReplacement::GameFont::Resolve() {
	StructSize = ResolveGameCode(Game::Resolved::Fonts::FontManager).FontSize;
	const auto font = ResolveGameCode(Game::Resolved::Fonts::Font);
	MaxTextures = font.MaxTextures;

	// At the start, where the code has no displacement to capture.
	s_font.TextureResourceHandles = 0;
	s_font.Textures = font.Textures;
	s_font.KerningCount = font.KerningCount;
	s_font.TextureWidth = font.TextureWidth;
	s_font.TextureHeight = font.TextureWidth + 2;  // as in the FDT's header
	s_font.Size = font.Size;
	s_font.LineHeight = font.Size + 4;  // copied from the FDT's header along with Size
	s_font.Ascent = font.Ascent;

	s_font.GlyphMap = font.GlyphMap;
	s_font.Secondary = font.Secondary;
	s_font.SecondaryRatio = font.SecondaryRatio;
	s_font.TextureCount = font.TextureCount;
	s_font.Flags = font.Flags;

	// Optional: only changed in copies.
	s_font.ItalicCorrection = font.XShift.value_or(-1);
}
