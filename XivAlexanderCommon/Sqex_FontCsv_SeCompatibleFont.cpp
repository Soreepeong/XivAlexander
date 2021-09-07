#include "pch.h"
#include "Sqex_FontCsv_SeCompatibleFont.h"

#include "Sqex_Texture_Mipmap.h"

void Sqex::FontCsv::GlyphMeasurement::AdjustToIntersection(GlyphMeasurement& r, SSIZE_T srcWidth, SSIZE_T srcHeight, SSIZE_T destWidth, SSIZE_T destHeight) {
	if (left < 0) {
		r.left -= left;
		left = 0;
	}
	if (r.left < 0) {
		left -= r.left;
		r.left = 0;
	}
	if (top < 0) {
		r.top -= top;
		top = 0;
	}
	if (r.top < 0) {
		top -= r.top;
		r.top = 0;
	}
	if (right >= srcWidth) {
		r.right -= right - srcWidth;
		right = srcWidth;
	}
	if (r.right >= destWidth) {
		right -= r.right - destWidth;
		r.right = destWidth;
	}
	if (bottom >= srcHeight) {
		r.bottom -= bottom - srcHeight;
		bottom = srcHeight;
	}
	if (r.bottom >= destHeight) {
		bottom -= r.bottom - destHeight;
		r.bottom = destHeight;
	}

	if (left >= right || r.left >= r.right || top >= bottom || r.top >= r.bottom)
		*this = r = {};
}

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::SeCompatibleFont::MaxBoundingBox() const {
	if (!m_maxBoundingBox.empty)
		return m_maxBoundingBox;
	GlyphMeasurement res{ false, INT_MAX, INT_MAX, INT_MIN, INT_MIN, INT_MIN };
	for (const auto& c : GetAllCharacters()) {
		GlyphMeasurement cur = GetBoundingBox(c);
		if (cur.empty)
			throw std::runtime_error("Character found from GetAllCharacters but GetBoundingBox returned empty");
		res.left = std::min(res.left, cur.left);
		res.top = std::min(res.top, cur.top);
		res.right = std::max(res.right, cur.right);
		res.bottom = std::max(res.bottom, cur.bottom);
		res.offsetX = std::max(res.offsetX, cur.offsetX);
	}
	m_maxBoundingBox = res;
	return res;
}

SSIZE_T Sqex::FontCsv::SeCompatibleFont::GetKerning(char32_t l, char32_t r, SSIZE_T defaultOffset) const {
	if (!l || !r)
		return defaultOffset;

	const auto& t = GetKerningTable();
	const auto it = t.find(std::make_pair(l, r));
	if (it == t.end())
		return defaultOffset;
	return it->second;
}

struct Sqex::FontCsv::SeFont::Implementation {
	const std::shared_ptr<const ModifiableFontCsvStream> m_stream;

	mutable bool m_kerningDiscovered = false;
	mutable std::map<std::pair<char32_t, char32_t>, SSIZE_T> m_kerningMap;

	mutable bool m_characterListDiscovered = false;
	mutable std::vector<char32_t> m_characterList;

	Implementation(std::shared_ptr<const ModifiableFontCsvStream> stream)
		: m_stream(std::move(stream)) {
	}
};

Sqex::FontCsv::SeFont::SeFont(std::shared_ptr<const ModifiableFontCsvStream> stream)
	: m_pImpl(std::make_unique<Implementation>(std::move(stream))) {
}

Sqex::FontCsv::SeFont::~SeFont() = default;

bool Sqex::FontCsv::SeFont::HasCharacter(char32_t c) const {
	return m_pImpl->m_stream->GetFontEntry(c);
}

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::SeFont::GetBoundingBox(const FontTableEntry & entry, SSIZE_T offsetX, SSIZE_T offsetY) {
	return {
		.empty = false,
		.left = offsetX,
		.top = offsetY + entry.CurrentOffsetY,
		.right = offsetX + entry.BoundingWidth,
		.bottom = offsetY + entry.CurrentOffsetY + entry.BoundingHeight,
		.offsetX = entry.NextOffsetX,
	};
}

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::SeFont::GetBoundingBox(char32_t c, SSIZE_T offsetX, SSIZE_T offsetY) const {
	const auto entry = m_pImpl->m_stream->GetFontEntry(c);
	if (!entry)
		return { true };

	return GetBoundingBox(*entry, offsetX, offsetY);
}

SSIZE_T Sqex::FontCsv::SeFont::GetCharacterWidth(char32_t c) const {
	const auto entry = m_pImpl->m_stream->GetFontEntry(c);
	if (!entry)
		return {};

	return static_cast<SSIZE_T>(0) + entry->BoundingWidth + entry->NextOffsetX;
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

const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& Sqex::FontCsv::SeFont::GetKerningTable() const {
	if (!m_pImpl->m_kerningDiscovered) {
		std::map<std::pair<char32_t, char32_t>, SSIZE_T> result;
		for (const auto& k : m_pImpl->m_stream->GetKerningEntries()) {
			if (k.RightOffset)
				result.emplace(std::make_pair(k.Left(), k.Right()), k.RightOffset);
		}
		m_pImpl->m_kerningMap = std::move(result);
		m_pImpl->m_kerningDiscovered = true;
	}
	return m_pImpl->m_kerningMap;
}

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::SeFont::Measure(SSIZE_T x, SSIZE_T y, const FontTableEntry& entry) const {
	return GetBoundingBox(entry, x, y);
}

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::SeFont::Measure(SSIZE_T x, SSIZE_T y, const char32_t c) const {
	const auto entry = GetStream().GetFontEntry(c);
	if (!entry)  // skip missing characters
		return {};
	return this->Measure(x, y, *entry);
}

const Sqex::FontCsv::ModifiableFontCsvStream& Sqex::FontCsv::SeFont::GetStream() const {
	return *m_pImpl->m_stream;
}

struct Sqex::FontCsv::CascadingFont::Implementation {
	const std::vector<std::shared_ptr<SeCompatibleFont>> m_fontList;
	const float m_size;
	const uint32_t m_ascent;
	const uint32_t m_descent;

	mutable bool m_kerningDiscovered = false;
	mutable std::map<std::pair<char32_t, char32_t>, SSIZE_T> m_kerningMap;

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

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::CascadingFont::GetBoundingBox(char32_t c, SSIZE_T offsetX, SSIZE_T offsetY) const {
	for (const auto& f : m_pImpl->m_fontList)
		if (const auto bbox = f->GetBoundingBox(c, offsetX, offsetY); bbox.left || bbox.right || bbox.top || bbox.bottom)
			return bbox;
	return {};
}

SSIZE_T Sqex::FontCsv::CascadingFont::GetCharacterWidth(char32_t c) const {
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

const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& Sqex::FontCsv::CascadingFont::GetKerningTable() const {
	if (!m_pImpl->m_kerningDiscovered) {
		std::map<std::pair<char32_t, char32_t>, SSIZE_T> result;
		for (size_t i = 0; i < m_pImpl->m_fontList.size(); ++i) {
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

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::CascadingFont::Measure(SSIZE_T x, SSIZE_T y, char32_t c) const {
	for (const auto& f : GetFontList()) {
		const auto currBbox = f->Measure(x, y + Ascent() - f->Ascent(), c);
		if (!currBbox.empty)
			return currBbox;
	}
	return { true };
}

const std::vector<std::shared_ptr<Sqex::FontCsv::SeCompatibleFont>>& Sqex::FontCsv::CascadingFont::GetFontList() const {
	return m_pImpl->m_fontList;
}
