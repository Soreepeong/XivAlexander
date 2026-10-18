#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/Nominal/GameFontDataFaceSource.h"

#include <FontChanger.FixedSizeFont/fontdata_fixed_size_font.h>
#include <xivres/fontdata.h>
#include <xivres/installation.h>
#include <xivres/texture.mipmap_stream.h>
#include <xivres/texture.stream.h>

#include "MainApp/FontReplacement/Host.h"
#include "Utils/Win32/Process.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// The columns that the game drops from the left of a glyph when it loads a font (AtkFontManager.BuildFont): in fonts
	// larger than 35 px, the first of the IME indicators (U+E020-E027) and of U+E0BB, which are then drawn a pixel left and
	// advance a pixel less than their files say.
	int GetDroppedColumns(const xivres::fontdata::glyph_entry* g, float fontSize) {
		const auto c = g->codepoint();
		return fontSize > 35 && (c >= 0xE020 && c <= 0xE027 || c == 0xE0BB) && *g->BoundingWidth > 0 ? 1 : 0;
	}
}

struct FontReplacement::GameFontDataFaceSource::Shared {
	xivres::installation Installation;
	std::mutex Mutex;
	std::map<std::string, std::shared_ptr<xivres::texture::memory_mipmap_stream>> Textures;

	explicit Shared(const std::filesystem::path& gamePath)
		: Installation(gamePath) {}

	// nullptr if it can't be read.
	std::shared_ptr<xivres::texture::memory_mipmap_stream> GetTexture(const std::string& path) {
		const auto lock = std::scoped_lock(Mutex);
		if (const auto it = Textures.find(path); it != Textures.end())
			return it->second;

		std::shared_ptr<xivres::texture::memory_mipmap_stream> texture;
		try {
			texture = xivres::texture::memory_mipmap_stream::as_argb8888(*xivres::texture::stream(Installation.get_file(path)).mipmap_at(0, 0));
		} catch (const std::exception& e) {
			Host::Warning("The game font texture {} can't be read: {}", path, e.what());
		}
		return Textures.emplace(path, std::move(texture)).first->second;
	}
};

std::mutex FontReplacement::GameFontDataFaceSource::s_sharedMutex;
std::weak_ptr<FontReplacement::GameFontDataFaceSource::Shared> FontReplacement::GameFontDataFaceSource::s_shared;

FontReplacement::GameFontDataFaceSource::GameFontDataFaceSource(std::shared_ptr<Shared> shared, std::shared_ptr<const xivres::fontdata::stream> fdt, std::string textures, float size)
	: m_shared(std::move(shared))
	, m_fdt(std::move(fdt))
	, m_textures(std::move(textures))
	, m_size(size) {
	for (const auto& g : m_fdt->get_glyphs())
		m_codepoints.insert(g.codepoint());
}

FontReplacement::GameFontDataFaceSource::~GameFontDataFaceSource() = default;

std::unique_ptr<FontReplacement::GameFontDataFaceSource> FontReplacement::GameFontDataFaceSource::Create(const FontChanger::Structs::FontElement& def, const std::string& fontName) {
	using namespace FontChanger::FixedSizeFont;

	// On a tie, the smaller: fonts are listed smallest first.
	const game_fontdata_definition* gameFont = nullptr;
	for (const auto type : get_font_types()) {
		for (const auto& d : get_fontdata_definition(type)) {
			if (d.Family == def.Lookup.FamilyName && (!gameFont || std::abs(d.Size - def.Size) < std::abs(gameFont->Size - def.Size)))
				gameFont = &d;
		}
		if (gameFont)
			break;
	}
	if (!gameFont) {
		Host::Warning("{}: {} isn't a game font family", fontName, def.Lookup.FamilyName);
		return nullptr;
	}

	try {
		auto shared = GetShared();
		auto fdt = std::make_shared<xivres::fontdata::stream>(*shared->Installation.get_file(gameFont->Path));
		const auto format = get_font_tex_filename_format(gameFont->FontType);
		if (!format)
			throw std::runtime_error("The font type's textures are unknown.");
		return std::unique_ptr<GameFontDataFaceSource>(new GameFontDataFaceSource(std::move(shared), std::move(fdt), format, def.Size));
	} catch (const std::exception& e) {
		Host::Warning("{}: the game font {} can't be read: {}", fontName, gameFont->Name, e.what());
		return nullptr;
	}
}

std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> FontReplacement::GameFontDataFaceSource::OpenMetrics(const std::string& fontName) {
	using namespace FontChanger::FixedSizeFont;

	const auto def = find_fontdata_definition(fontName);
	if (!def)
		return nullptr;

	try {
		const auto shared = GetShared();
		return std::make_shared<fontdata_fixed_size_font>(
			std::make_shared<xivres::fontdata::stream>(*shared->Installation.get_file(def->Path)),
			std::vector<std::shared_ptr<xivres::texture::memory_mipmap_stream>>{},
			def->Family,
			def->Subfamily);
	} catch (const std::exception& e) {
		Host::Warning("{}: the game font can't be read: {}", fontName, e.what());
		return nullptr;
	}
}

std::shared_ptr<FontReplacement::GameFontDataFaceSource::Shared> FontReplacement::GameFontDataFaceSource::GetShared() {
	const auto lock = std::scoped_lock(s_sharedMutex);
	auto shared = s_shared.lock();
	if (!shared) {
		shared = std::make_shared<Shared>(Utils::Win32::Process::Current().PathOf().parent_path());
		s_shared = shared;
	}
	return shared;
}

uint32_t FontReplacement::GameFontDataFaceSource::GetGlyphId(char32_t codepoint) {
	return m_fdt->get_glyph(codepoint) ? static_cast<uint32_t>(codepoint) : 0u;
}

float FontReplacement::GameFontDataFaceSource::GetAdvance(float px, uint32_t glyphId) {
	const auto g = m_fdt->get_glyph(static_cast<char32_t>(glyphId));
	return g ? static_cast<float>(*g->BoundingWidth - GetDroppedColumns(g, m_fdt->font_size()) + *g->NextOffsetX) * GetScale(px) : 0.f;
}

FontReplacement::LineMetrics FontReplacement::GameFontDataFaceSource::GetLineMetrics(float px) {
	const auto scale = GetScale(px);
	return {
		static_cast<int>(std::round(static_cast<float>(m_fdt->ascent()) * scale)),
		static_cast<int>(std::round(static_cast<float>(m_fdt->line_height()) * scale)),
	};
}

std::optional<FontReplacement::RasterGlyph> FontReplacement::GameFontDataFaceSource::RasterizeGlyph(float px, uint32_t glyphId) {
	const auto g = m_fdt->get_glyph(static_cast<char32_t>(glyphId));
	if (!g)
		return std::nullopt;
	const auto number = static_cast<int>(g->texture_file_index()) + 1;
	const auto texture = m_shared->GetTexture(std::vformat(m_textures, std::make_format_args(number)));
	if (!texture)
		return std::nullopt;

	const auto dropped = GetDroppedColumns(g, m_fdt->font_size());
	const int width = *g->BoundingWidth - dropped;
	const int height = *g->BoundingHeight;
	const int textureX = *g->TextureOffsetX + dropped;
	const int textureY = *g->TextureOffsetY;
	const auto channel = xivres::fontdata::glyph_entry::ChannelMap[g->texture_plane_index()];
	const auto pixels = texture->as_span<uint8_t>();
	RasterGlyph glyph{width + *g->NextOffsetX, 0, *g->CurrentOffsetY - static_cast<int>(m_fdt->ascent()), width, height, {}};
	glyph.Alpha.resize(static_cast<size_t>(width) * height);
	for (auto y = 0; y < height && textureY + y < texture->Height; y++) {
		for (auto x = 0; x < width && textureX + x < texture->Width; x++)
			glyph.Alpha[static_cast<size_t>(y) * width + x] = pixels[(static_cast<size_t>(textureY + y) * texture->Width + textureX + x) * 4 + channel];
	}

	const auto scale = GetScale(px);
	if (scale == 1)
		return glyph;
	const auto advance = static_cast<int>(std::round(static_cast<float>(glyph.Advance) * scale));
	glyph = glyph.Scaled(scale, 0, 0);
	glyph.Advance = advance;
	return glyph;
}

float FontReplacement::GameFontDataFaceSource::GetScale(float px) const {
	const auto scale = m_size > 0 ? px / m_size : 1.f;
	const auto size = m_fdt->font_size();
	return std::abs(scale * size - size) <= 0.25f ? 1.f : scale;
}
