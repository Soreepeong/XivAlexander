#include "pch.h"
#include "Sqex_FontCsv_Render.h"

#include "Sqex_Texture_Mipmap.h"

struct Sqex::FontCsv::SeFont::Implementation {
	const std::shared_ptr<const ModifiableFontCsvStream> m_stream;
	std::vector<std::shared_ptr<const Texture::MipmapStream>> m_mipmaps;
	std::vector<std::vector<Texture::RGBA4444>> m_mipmapBuffers;

	mutable bool m_kerningDiscovered = false;
	mutable std::map<std::pair<char32_t, char32_t>, int> m_kerningMap;

	mutable bool m_characterListDiscovered = false;
	mutable std::vector<char32_t> m_characterList;

	Implementation(std::shared_ptr<const ModifiableFontCsvStream> stream, std::vector<std::shared_ptr<const Texture::MipmapStream>> mipmaps)
		: m_stream(std::move(stream))
		, m_mipmaps(std::move(mipmaps))
		, m_mipmapBuffers(m_mipmaps.size()) {
	}
};

RECT Sqex::FontCsv::SeCompatibleFont::MaxBoundingBox() const {
	if (m_maxBoundingBox.right != LONG_MAX)
		return m_maxBoundingBox;
	RECT res{ LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN };
	for (const auto& c : GetAllCharacters()) {
		RECT cur = GetBoundingBox(c);
		res.left = std::min(res.left, cur.left);
		res.top = std::min(res.top, cur.top);
		res.right = std::max(res.right, cur.right);
		res.bottom = std::max(res.bottom, cur.bottom);
	}
	m_maxBoundingBox = res;
	return res;
}

int Sqex::FontCsv::SeCompatibleFont::GetKerning(char32_t l, char32_t r, int defaultOffset) const {
	const auto& t = GetKerningTable();
	const auto it = t.find(std::make_pair(l, r));
	if (it == t.end())
		return defaultOffset;
	return it->second;
}

Sqex::FontCsv::SeFont::SeFont(std::shared_ptr<const ModifiableFontCsvStream> stream, std::vector<std::shared_ptr<const Texture::MipmapStream>> mipmaps)
	: m_pImpl(std::make_unique<Implementation>(std::move(stream), std::move(mipmaps))) {
}

Sqex::FontCsv::SeFont::~SeFont() = default;

bool Sqex::FontCsv::SeFont::HasCharacter(char32_t c) const {
	return m_pImpl->m_stream->GetFontEntry(c);
}

RECT Sqex::FontCsv::SeFont::GetBoundingBox(const FontTableEntry & entry, int offsetX, int offsetY) {
	return {
		.left = offsetX,
		.top = offsetY + entry.CurrentOffsetY,
		.right = offsetX + entry.BoundingWidth,
		.bottom = offsetY + entry.CurrentOffsetY + entry.BoundingHeight,
	};
}

RECT Sqex::FontCsv::SeFont::GetBoundingBox(char32_t c, int offsetX, int offsetY) const {
	const auto entry = m_pImpl->m_stream->GetFontEntry(c);
	if (!entry)
		return {offsetX, offsetY, offsetX, offsetY};

	return GetBoundingBox(*entry, offsetX, offsetY);
}

int Sqex::FontCsv::SeFont::GetCharacterWidth(char32_t c) const {
	const auto entry = m_pImpl->m_stream->GetFontEntry(c);
	if (!entry)
		return {};

	return entry->BoundingWidth + entry->NextOffsetX;
}

float Sqex::FontCsv::SeFont::Size() const {
	return m_pImpl->m_stream->Points();
}

const std::vector<char32_t>& Sqex::FontCsv::SeFont::GetAllCharacters() const {
	if (!m_pImpl->m_characterListDiscovered) {
		std::vector<char32_t> result;
		for (const auto& c : m_pImpl->m_stream->GetFontTableEntries())
			result.push_back(c.Char());
		m_pImpl->m_characterList = std::move(result);
		return m_pImpl->m_characterList;
	}
	return m_pImpl->m_characterList;
}

uint32_t Sqex::FontCsv::SeFont::Ascent() const {
	return m_pImpl->m_stream->Ascent();
}

uint32_t Sqex::FontCsv::SeFont::Descent() const {
	return m_pImpl->m_stream->LineHeight() - m_pImpl->m_stream->Ascent();
}

const std::map<std::pair<char32_t, char32_t>, int>& Sqex::FontCsv::SeFont::GetKerningTable() const {
	if (!m_pImpl->m_kerningDiscovered) {
		std::map<std::pair<char32_t, char32_t>, int> result;
		for (const auto& k : m_pImpl->m_stream->GetKerningEntries()) {
			if (k.RightOffset)
				result.emplace(std::make_pair(k.Left(), k.Right()), k.RightOffset);
		}
		m_pImpl->m_kerningMap = std::move(result);
		m_pImpl->m_kerningDiscovered = true;
	}
	return m_pImpl->m_kerningMap;
}

static void AdjustRectBoundaries(RECT & src, RECT & dest, int srcWidth, int srcHeight, int destWidth, int destHeight) {
	if (src.left < 0) {
		dest.left -= src.left;
		src.left = 0;
	}
	if (dest.left < 0) {
		src.left -= dest.left;
		dest.left = 0;
	}
	if (src.top < 0) {
		dest.top -= src.top;
		src.top = 0;
	}
	if (dest.top < 0) {
		src.top -= dest.top;
		dest.top = 0;
	}
	if (src.right >= srcWidth) {
		dest.right -= src.right - srcWidth;
		src.right = srcWidth;
	}
	if (dest.right >= destWidth) {
		src.right -= dest.right - destWidth;
		dest.right = destWidth;
	}
	if (src.bottom >= srcHeight) {
		dest.bottom -= src.bottom - srcHeight;
		src.bottom = srcHeight;
	}
	if (dest.bottom >= destHeight) {
		src.bottom -= dest.bottom - destHeight;
		dest.bottom = destHeight;
	}
}

RECT Sqex::FontCsv::SeFont::MeasureAndDraw(Texture::MemoryBackedMipmap * to, int x, int y, const std::u32string & s, Texture::RGBA8888 color) const {
	if (s.empty())
		return {};

	char32_t lastChar = 0;
	const auto iHeight = static_cast<int>(Height());

	RECT result = { LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN };
	POINT current{ x, y };

	for (const auto currChar : s) {
		if (currChar == u'\r') {
			continue;
		} else if (currChar == u'\n') {
			current.x = 0;
			current.y += iHeight;
			lastChar = 0;
			continue;
		} else if (currChar == u'\u200c') {  // unicode non-joiner
			lastChar = 0;
			continue;
		}

		const auto kerning = GetKerning(lastChar, currChar);
		const auto entry = m_pImpl->m_stream->GetFontEntry(currChar);
		if (!entry)  // skip missing characters
			continue;

		const auto currBbox = GetBoundingBox(*entry, current.x + kerning, current.y);
		if (to) {
			const auto destWidth = static_cast<int>(to->Width());
			const auto destHeight = static_cast<int>(to->Height());
			const auto srcWidth = static_cast<int>(m_pImpl->m_stream->TextureWidth());
			const auto srcHeight = static_cast<int>(m_pImpl->m_stream->TextureHeight());
			if (to->Type() == Texture::CompressionType::ARGB_1 || to->Type() == Texture::CompressionType::ARGB_2) {
				auto destBuf = to->View<Texture::RGBA8888>();

				auto& srcBuf = m_pImpl->m_mipmapBuffers[entry->TextureIndex / 4];
				if (srcBuf.empty())
					srcBuf = m_pImpl->m_mipmaps[entry->TextureIndex / 4]->ReadStreamIntoVector<Texture::RGBA4444>(0);
				const auto channelIndex = entry->TextureIndex % 4;

				RECT src = {
					entry->TextureOffsetX,
					entry->TextureOffsetY,
					entry->TextureOffsetX + entry->BoundingWidth,
					entry->TextureOffsetY + entry->BoundingHeight,
				};
				RECT dest = currBbox;
				AdjustRectBoundaries(src, dest, srcWidth, srcHeight, destWidth, destHeight);

				for (auto srcY = src.top, destY = dest.top; srcY < src.bottom; ++srcY, ++destY) {
					auto destPtr = &destBuf[static_cast<size_t>(1) * destY * destWidth + dest.left];
					auto srcPtr = &srcBuf[static_cast<size_t>(1) * srcY * srcWidth + src.left];
					const auto destPtrBoundary = &destBuf[static_cast<size_t>(1) * destY * destWidth] + dest.right;
					do {
						const auto currentAlpha = color.A * (channelIndex == 0 ? srcPtr->B : channelIndex == 1 ? srcPtr->G : channelIndex == 2 ? srcPtr->R : srcPtr->A) / 15;
						destPtr->R = (destPtr->R * (255 - currentAlpha) + color.R * currentAlpha) / 255;
						destPtr->G = (destPtr->G * (255 - currentAlpha) + color.G * currentAlpha) / 255;
						destPtr->B = (destPtr->B * (255 - currentAlpha) + color.B * currentAlpha) / 255;
						destPtr++;
						srcPtr++;
					} while (destPtr < destPtrBoundary);
				}
			}
		}

		result.left = std::min(result.left, currBbox.left);
		result.top = std::min(result.top, currBbox.top);
		result.right = std::max(result.right, currBbox.right);
		result.bottom = std::max(result.bottom, currBbox.bottom);
		current.x = currBbox.right + entry->NextOffsetX;
		lastChar = currChar;
	}
	if (result.right == LONG_MIN)
		return { x, y, x, y };
	return result;
}

struct Sqex::FontCsv::CascadingFont::Implementation {
	const std::vector<std::shared_ptr<SeCompatibleFont>> m_fontList;
	const float m_size;
	const uint32_t m_ascent;
	const uint32_t m_descent;

	mutable bool m_kerningDiscovered = false;
	mutable std::map<std::pair<char32_t, char32_t>, int> m_kerningMap;

	mutable bool m_characterListDiscovered = false;
	mutable std::vector<char32_t> m_characterList;

	Implementation(std::vector<std::shared_ptr<SeCompatibleFont>> fontList, float normalizedSize, uint32_t ascent, uint32_t descent)
		: m_fontList(std::move(fontList))
		, m_size(static_cast<bool>(normalizedSize) ? normalizedSize : std::ranges::max(m_fontList, {}, [](const auto& r) {return r->Size();  })->Size())
		, m_ascent(ascent != UINT32_MAX ? ascent : std::ranges::max(m_fontList, {}, [](const auto& r) {return r->Ascent();  })->Ascent())
		, m_descent(descent != UINT32_MAX ? descent : std::ranges::max(m_fontList, {}, [](const auto& r) {return r->Descent();  })->Descent()) {
	}

	size_t GetCharacterOwnerIndex(char32_t c) const {
		for (size_t i = 0; i < m_fontList.size(); ++i)
			if (m_fontList[i]->HasCharacter(c))
				return i;
		return SIZE_MAX;
	}
};

Sqex::FontCsv::CascadingFont::CascadingFont(std::vector<std::shared_ptr<SeCompatibleFont>> fontList)
	: m_pImpl(std::make_unique<Implementation>(std::move(fontList), 0.f, UINT32_MAX, UINT32_MAX)) {
}

Sqex::FontCsv::CascadingFont::CascadingFont(std::vector<std::shared_ptr<SeCompatibleFont>> fontList, float normalizedSize, uint32_t ascent, uint32_t descent)
	: m_pImpl(std::make_unique<Implementation>(std::move(fontList), normalizedSize, ascent, descent)) {
}

Sqex::FontCsv::CascadingFont::~CascadingFont() = default;

bool Sqex::FontCsv::CascadingFont::HasCharacter(char32_t c) const {
	return std::ranges::any_of(m_pImpl->m_fontList, [c](const auto& f) { return f->HasCharacter(c); });
}

RECT Sqex::FontCsv::CascadingFont::GetBoundingBox(char32_t c, int offsetX, int offsetY) const {
	for (const auto& f : m_pImpl->m_fontList)
		if (const auto bbox = f->GetBoundingBox(c, offsetX, offsetY); bbox.left || bbox.right || bbox.top || bbox.bottom)
			return bbox;
	return {};
}

int Sqex::FontCsv::CascadingFont::GetCharacterWidth(char32_t c) const {
	for (const auto& f : m_pImpl->m_fontList)
		if (const auto w = f->GetCharacterWidth(c))
			return w;
	return 0;
}

float Sqex::FontCsv::CascadingFont::Size() const {
	return m_pImpl->m_size;
}

const std::vector<char32_t>& Sqex::FontCsv::CascadingFont::GetAllCharacters() const {
	if (!m_pImpl->m_characterListDiscovered) {
		std::set<char32_t> result;
		for (const auto& f : m_pImpl->m_fontList)
			for (const auto& c : f->GetAllCharacters())
				result.insert(c);
		m_pImpl->m_characterList.insert(m_pImpl->m_characterList.end(), result.begin(), result.end());
		return m_pImpl->m_characterList;
	}
	return m_pImpl->m_characterList;
}

uint32_t Sqex::FontCsv::CascadingFont::Ascent() const {
	return m_pImpl->m_ascent;
}

uint32_t Sqex::FontCsv::CascadingFont::Descent() const {
	return m_pImpl->m_descent;
}

const std::map<std::pair<char32_t, char32_t>, int>& Sqex::FontCsv::CascadingFont::GetKerningTable() const {
	if (!m_pImpl->m_kerningDiscovered) {
		std::map<std::pair<char32_t, char32_t>, int> result;
		for (size_t i = 0; i < m_pImpl->m_fontList.size();++i) {
			for (const auto& k : m_pImpl->m_fontList[i]->GetKerningTable()) {
				if (!k.second)
					continue;
				const auto owner1 = m_pImpl->GetCharacterOwnerIndex(k.first.first);
				const auto owner2 = m_pImpl->GetCharacterOwnerIndex(k.first.second);

				if (owner1 == i && owner2 == i) {
					// pass
				} else if (k.first.first == u' ' && owner2 == i) {
					// pass
				} else if (k.first.second == u' ' && owner1 == i) {
					// pass
				} else
					continue;
				
				result.emplace(k);
			}
		}
		m_pImpl->m_kerningMap = std::move(result);
		m_pImpl->m_kerningDiscovered = true;
	}
	return m_pImpl->m_kerningMap;
}

RECT Sqex::FontCsv::CascadingFont::MeasureAndDraw(Texture::MemoryBackedMipmap* to, int x, int y, const std::u32string& s, Texture::RGBA8888 color) const {
	if (s.empty())
		return {};

	char32_t lastChar = 0;
	const auto iHeight = static_cast<int>(Height());

	RECT result = { LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN };
	POINT current{ x, y };

	for (const auto currChar : s) {
		if (currChar == u'\r') {
			continue;
		} else if (currChar == u'\n') {
			current.x = 0;
			current.y += iHeight;
			lastChar = 0;
			continue;
		} else if (currChar == u'\u200c') {  // unicode non-joiner
			lastChar = 0;
			continue;
		}

		const auto kerning = GetKerning(lastChar, currChar);

		for (const auto& f : m_pImpl->m_fontList) {
			if (!f->HasCharacter(currChar))
				continue;

			const auto currBbox = f->MeasureAndDraw(to, current.x + kerning, current.y + m_pImpl->m_ascent - f->Ascent(), std::u32string{ currChar }, color);
			result.left = std::min(result.left, currBbox.left);
			result.top = std::min(result.top, currBbox.top);
			result.right = std::max(result.right, currBbox.right);
			result.bottom = std::max(result.bottom, currBbox.bottom);
			current.x += f->GetCharacterWidth(currChar);
			break;
		}

		lastChar = currChar;
	}
	return result;
}
