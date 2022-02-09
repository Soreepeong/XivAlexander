#include "pch.h"
#include "Sqex/FontCsv/FdtFont.h"

struct Sqex::FontCsv::FdtFont::Implementation {
	const std::shared_ptr<const ModifiableFontCsvStream> Stream;
	SSIZE_T LeftSideBearing;

	mutable bool KerningDiscovered = false;
	mutable std::map<std::pair<char32_t, char32_t>, SSIZE_T> KerningMap;

	mutable bool CharacterListDiscovered = false;
	mutable std::vector<char32_t> CharacterList;

	Implementation(std::shared_ptr<const ModifiableFontCsvStream> stream, SSIZE_T offsetX)
		: Stream(std::move(stream))
		, LeftSideBearing(offsetX) {
	}
};

Sqex::FontCsv::FdtFont::FdtFont(std::shared_ptr<const ModifiableFontCsvStream> stream, SSIZE_T leftSideBearing)
	: m_pImpl(std::make_unique<Implementation>(std::move(stream), leftSideBearing)) {
}

Sqex::FontCsv::FdtFont::~FdtFont() = default;

void Sqex::FontCsv::FdtFont::SetLeftSideBearing(SSIZE_T leftSideBearing) {
	m_pImpl->LeftSideBearing = leftSideBearing;
}

SSIZE_T Sqex::FontCsv::FdtFont::GetLeftSideBearing() const {
	return m_pImpl->LeftSideBearing;
}

bool Sqex::FontCsv::FdtFont::HasCharacter(char32_t c) const {
	return m_pImpl->Stream->GetFontEntry(c);
}

float Sqex::FontCsv::FdtFont::Size() const {
	return m_pImpl->Stream->Points();
}

const std::vector<char32_t>& Sqex::FontCsv::FdtFont::GetAllCharacters() const {
	if (!m_pImpl->CharacterListDiscovered) {
		std::vector<char32_t> result;
		for (const auto& c : m_pImpl->Stream->GetFontTableEntries())
			result.push_back(c.Char());
		m_pImpl->CharacterList = std::move(result);
		m_pImpl->CharacterListDiscovered = true;
	}
	return m_pImpl->CharacterList;
}

uint32_t Sqex::FontCsv::FdtFont::Ascent() const {
	return m_pImpl->Stream->Ascent();
}

uint32_t Sqex::FontCsv::FdtFont::LineHeight() const {
	return m_pImpl->Stream->LineHeight();
}

const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& Sqex::FontCsv::FdtFont::GetKerningTable() const {
	if (!m_pImpl->KerningDiscovered) {
		std::map<std::pair<char32_t, char32_t>, SSIZE_T> result;
		for (const auto& k : m_pImpl->Stream->GetKerningEntries()) {
			if (k.RightOffset)
				result.emplace(std::make_pair(k.Left(), k.Right()), k.RightOffset);
		}
		m_pImpl->KerningMap = std::move(result);
		m_pImpl->KerningDiscovered = true;
	}
	return m_pImpl->KerningMap;
}

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::FdtFont::Measure(SSIZE_T x, SSIZE_T y, const FontTableEntry & entry) const {
	return {
		.empty = false,
		.left = x - m_pImpl->LeftSideBearing,
		.top = y + entry.CurrentOffsetY,
		.right = x - m_pImpl->LeftSideBearing + entry.BoundingWidth,
		.bottom = y + entry.CurrentOffsetY + entry.BoundingHeight,
		.advanceX = entry.BoundingWidth + entry.NextOffsetX + m_advanceWidthDelta,
	};
}

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::FdtFont::Measure(SSIZE_T x, SSIZE_T y, const char32_t c) const {
	const auto entry = GetStream().GetFontEntry(c);
	if (!entry) // skip missing characters
		return {};
	return this->Measure(x, y, *entry);
}

const Sqex::FontCsv::ModifiableFontCsvStream& Sqex::FontCsv::FdtFont::GetStream() const {
	return *m_pImpl->Stream;
}

struct Sqex::FontCsv::CascadingFont::Implementation {
	const std::vector<std::shared_ptr<BaseFont>> m_fontList;
	const float m_size;
	const uint32_t m_ascent;
	const uint32_t m_lineHeight;

	mutable bool m_kerningDiscovered = false;
	mutable std::map<std::pair<char32_t, char32_t>, SSIZE_T> m_kerningMap;

	mutable bool m_characterListDiscovered = false;
	mutable std::vector<char32_t> m_characterList;

	Implementation(std::vector<std::shared_ptr<BaseFont>> fontList, float normalizedSize, uint32_t ascent, uint32_t lineHeight)
		: m_fontList(std::move(fontList))
		, m_size(static_cast<bool>(normalizedSize) ? normalizedSize : std::ranges::max(m_fontList, {}, [](const auto& r) { return r->Size(); })->Size())
		, m_ascent(ascent != UINT32_MAX ? ascent : std::ranges::max(m_fontList, {}, [](const auto& r) { return r->Ascent(); })->Ascent())
		, m_lineHeight(lineHeight != UINT32_MAX ? lineHeight : std::ranges::max(m_fontList, {}, [](const auto& r) { return r->LineHeight(); })->LineHeight()) {
	}

	size_t GetCharacterOwnerIndex(char32_t c) const {
		for (size_t i = 0; i < m_fontList.size(); ++i)
			if (m_fontList[i]->HasCharacter(c))
				return i;
		return SIZE_MAX;
	}
};

Sqex::FontCsv::CascadingFont::CascadingFont(std::vector<std::shared_ptr<BaseFont>> fontList)
	: m_pImpl(std::make_unique<Implementation>(std::move(fontList), 0.f, UINT32_MAX, UINT32_MAX)) {
}

Sqex::FontCsv::CascadingFont::CascadingFont(std::vector<std::shared_ptr<BaseFont>> fontList, float normalizedSize, uint32_t ascent, uint32_t lineHeight)
	: m_pImpl(std::make_unique<Implementation>(std::move(fontList), normalizedSize, ascent, lineHeight)) {
}

Sqex::FontCsv::CascadingFont::~CascadingFont() = default;

bool Sqex::FontCsv::CascadingFont::HasCharacter(char32_t c) const {
	return std::ranges::any_of(m_pImpl->m_fontList, [c](const auto& f) { return f->HasCharacter(c); });
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
		m_pImpl->m_characterListDiscovered = true;
		return m_pImpl->m_characterList;
	}
	return m_pImpl->m_characterList;
}

uint32_t Sqex::FontCsv::CascadingFont::Ascent() const {
	return m_pImpl->m_ascent;
}

uint32_t Sqex::FontCsv::CascadingFont::LineHeight() const {
	return m_pImpl->m_lineHeight;
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

				if ((owner1 == i && owner2 == i)
					|| (k.first.first == u' ' && owner2 == i)
					|| (k.first.second == u' ' && owner1 == i)
					) {
					// pass
				}
				else
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
		auto currBbox = f->Measure(x, y + Ascent() - f->Ascent(), c);
		if (!currBbox.empty) {
			currBbox.advanceX += m_advanceWidthDelta;
			return currBbox;
		}
	}
	return { true };
}

const std::vector<std::shared_ptr<Sqex::FontCsv::BaseFont>>& Sqex::FontCsv::CascadingFont::GetFontList() const {
	return m_pImpl->m_fontList;
}
