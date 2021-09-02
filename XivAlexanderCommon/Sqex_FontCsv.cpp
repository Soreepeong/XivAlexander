#include "pch.h"
#include "Sqex_FontCsv.h"

#include "Sqex_Sqpack.h"

const char Sqex::FontCsv::FontCsvHeader::Signature_Value[8] = {
	'f', 'c', 's', 'v', '0', '1', '0', '0',
};

const char Sqex::FontCsv::FontTableHeader::Signature_Value[4] = {
	'f', 't', 'h', 'd',
};

const char Sqex::FontCsv::KerningHeader::Signature_Value[4] = {
	'k', 'n', 'h', 'd',
};

static char32_t Utf8Uint32ToUnicodeCodePoint(uint32_t n) {
	if ((n & 0xFFFFFF80) == 0)
		return static_cast<char32_t>(n & 0x7F);
	else if ((n & 0xFFFFE0C0) == 0xC080)
		return static_cast<char32_t>(
			((n >> 0x08) & 0x1F) |
			((n >> 0x00) & 0x3F)
			);
	else if ((n & 0xF0C0C0) == 0xE08080)
		return static_cast<char32_t>(
			((n >> 0x10) & 0x0F) |
			((n >> 0x08) & 0x3F) |
			((n >> 0x00) & 0x3F)
			);
	else if ((n & 0xF8C0C0C0) == 0xF0808080)
		return static_cast<char32_t>(
			((n >> 0x18) & 0x07) |
			((n >> 0x10) & 0x3F) |
			((n >> 0x08) & 0x3F) |
			((n >> 0x00) & 0x3F)
			);
	else
		return 0xFFFF;  // Guaranteed non-unicode
}

static uint32_t UnicodeCodePointToUtf8Uint32(char32_t codepoint) {
	if (codepoint <= 0x7F) {
		return codepoint;
	} else if (codepoint <= 0x7FF) {
		return ((0xC0 | ((codepoint >> 6))) << 8)
			| ((0x80 | ((codepoint >> 0) & 0x3F)) << 0);
	} else if (codepoint <= 0xFFFF) {
		return ((0xE0 | ((codepoint >> 12))) << 16)
			| ((0x80 | ((codepoint >> 6) & 0x3F)) << 8)
			| ((0x80 | ((codepoint >> 0) & 0x3F)) << 0);
	} else if (codepoint <= 0x10FFFF) {
		return ((0xF0 | ((codepoint >> 18))) << 24)
			| ((0x80 | ((codepoint >> 12) & 0x3F)) << 16)
			| ((0x80 | ((codepoint >> 6) & 0x3F)) << 8)
			| ((0x80 | ((codepoint >> 0) & 0x3F)) << 0);
	} else
		throw std::invalid_argument("Unicode code point is currently capped at 0x10FFFF.");
}

static uint16_t UnicodeCodePointToShiftJisUint16(char32_t codepoint) {
	wchar_t utf16[2]{};
	int utf16len = 0;
	if (codepoint <= 0xFFFF) {
		utf16[utf16len++] = static_cast<wchar_t>(codepoint);
	} else if (codepoint <= 0x10FFFF) {
		utf16[utf16len++] = static_cast<wchar_t>(0xD800 | ((codepoint >> 10) & 0x3FF));
		utf16[utf16len++] = static_cast<wchar_t>(0xDC00 | ((codepoint >> 0) & 0x3FF));
	}

	// https://docs.microsoft.com/en-us/windows/win32/intl/code-page-identifiers
	constexpr UINT CodePage_ShiftJis = 932;

	switch (char buf[2]; WideCharToMultiByte(CodePage_ShiftJis, 0,
		utf16, utf16len,
		buf, sizeof buf, nullptr, nullptr)) {
		case 1:
			return buf[0];
		case 2:
			return static_cast<uint16_t>((buf[0] << 8) | buf[1]);
		default:
			return ' ';
	}
}

char32_t Sqex::FontCsv::FontTableEntry::Char() const {
	return Utf8Uint32ToUnicodeCodePoint(Utf8Value);
}

char32_t Sqex::FontCsv::FontTableEntry::Char(char32_t newValue) {
	Utf8Value = UnicodeCodePointToUtf8Uint32(newValue);
	ShiftJisValue = UnicodeCodePointToShiftJisUint16(newValue);
	return newValue;
}

char32_t Sqex::FontCsv::KerningEntry::Left() const {
	return Utf8Uint32ToUnicodeCodePoint(LeftUtf8Value);
}

char32_t Sqex::FontCsv::KerningEntry::Left(char32_t newValue) {
	LeftUtf8Value = UnicodeCodePointToUtf8Uint32(newValue);
	LeftShiftJisValue = UnicodeCodePointToShiftJisUint16(newValue);
	return newValue;
}

char32_t Sqex::FontCsv::KerningEntry::Right() const {
	return Utf8Uint32ToUnicodeCodePoint(RightUtf8Value);
}

char32_t Sqex::FontCsv::KerningEntry::Right(char32_t newValue) {
	RightUtf8Value = UnicodeCodePointToUtf8Uint32(newValue);
	RightShiftJisValue = UnicodeCodePointToShiftJisUint16(newValue);
	return newValue;
}

Sqex::FontCsv::FontCsvData::FontCsvData(float pt, uint16_t textureWidth, uint16_t textureHeight) {
	memcpy(m_fcsv.Signature, FontCsvHeader::Signature_Value, sizeof m_fcsv.Signature);
	memcpy(m_fthd.Signature, FontTableHeader::Signature_Value, sizeof m_fthd.Signature);
	memcpy(m_knhd.Signature, KerningHeader::Signature_Value, sizeof m_knhd.Signature);
	m_fcsv.FontTableHeaderOffset = static_cast<uint32_t>(sizeof m_fcsv);
	m_fthd.Points = pt;
	m_fthd.TextureWidth = textureWidth;
	m_fthd.TextureHeight = textureHeight;
}

Sqex::FontCsv::FontCsvData::FontCsvData(const RandomAccessStream& stream, bool strict)
	: m_fcsv(stream.ReadStream<FontCsvHeader>(0))
	, m_fthd(stream.ReadStream<FontTableHeader>(m_fcsv.FontTableHeaderOffset))
	, m_fontTableEntries(stream.ReadStreamIntoVector<FontTableEntry>(m_fcsv.FontTableHeaderOffset + sizeof m_fthd, m_fthd.FontTableEntryCount, 0x1000000))
	, m_knhd(stream.ReadStream<KerningHeader>(m_fcsv.KerningHeaderOffset))
	, m_kerningEntries(stream.ReadStreamIntoVector<KerningEntry>(m_fcsv.KerningHeaderOffset + sizeof m_knhd, std::min(m_knhd.EntryCount, m_fthd.KerningEntryCount), 0x1000000)) {
	if (strict) {
		if (0 != memcmp(m_fcsv.Signature, FontCsvHeader::Signature_Value, sizeof m_fcsv.Signature))
			throw Sqpack::CorruptDataException("fcsv.Signature != \"fcsv0100\"");
		if (m_fcsv.FontTableHeaderOffset != sizeof FontCsvHeader)
			throw Sqpack::CorruptDataException("FontTableHeaderOffset != sizeof FontCsvHeader");
		if (!IsAllSameValue(m_fcsv.Padding_0x10))
			throw Sqpack::CorruptDataException("fcsv.Padding_0x10 != 0");

		if (0 != memcmp(m_fthd.Signature, FontTableHeader::Signature_Value, sizeof m_fthd.Signature))
			throw Sqpack::CorruptDataException("fthd.Signature != \"fthd\"");
		if (!IsAllSameValue(m_fthd.Padding_0x0C))
			throw Sqpack::CorruptDataException("fthd.Padding_0x0C != 0");

		if (0 != memcmp(m_knhd.Signature, KerningHeader::Signature_Value, sizeof m_knhd.Signature))
			throw Sqpack::CorruptDataException("knhd.Signature != \"knhd\"");
		if (!IsAllSameValue(m_knhd.Padding_0x08))
			throw Sqpack::CorruptDataException("knhd.Padding_0x08 != 0");

		if (m_knhd.EntryCount != m_fthd.KerningEntryCount)
			throw std::runtime_error("knhd.EntryCount != fthd.KerningEntryCount");
	}
	std::sort(m_fontTableEntries.begin(), m_fontTableEntries.end(), [](const FontTableEntry& l, const FontTableEntry& r) {
		return l.Utf8Value < r.Utf8Value;
	});
	std::sort(m_kerningEntries.begin(), m_kerningEntries.end(), [](const KerningEntry& l, const KerningEntry& r) {
		if (l.LeftUtf8Value == r.LeftUtf8Value)
			return l.RightUtf8Value < r.RightUtf8Value;
		return l.LeftUtf8Value < r.LeftUtf8Value;
	});
}

uint32_t Sqex::FontCsv::FontCsvData::StreamSize() const {
	return static_cast<uint32_t>(
		sizeof m_fcsv
		+ sizeof m_fthd
		+ std::span(m_fontTableEntries).size_bytes()
		+ sizeof m_knhd
		+ std::span(m_kerningEntries).size_bytes()
		);
}

size_t Sqex::FontCsv::FontCsvData::ReadStreamPartial(uint64_t offset, void* buf, size_t length) const {
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<char*>(buf), length);

	if (relativeOffset < sizeof m_fcsv) {
		const auto src = std::span(reinterpret_cast<const char*>(&m_fcsv), sizeof m_fcsv)
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length - out.size_bytes();
	} else
		relativeOffset -= sizeof m_fcsv;

	if (relativeOffset < sizeof m_fthd) {
		const auto src = std::span(reinterpret_cast<const char*>(&m_fthd), sizeof m_fthd)
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length - out.size_bytes();
	} else
		relativeOffset -= sizeof m_fthd;

	if (const auto srcTyped = std::span(m_fontTableEntries);
		relativeOffset < srcTyped.size_bytes()) {
		const auto src = std::span(reinterpret_cast<const char*>(srcTyped.data()), srcTyped.size_bytes())
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length - out.size_bytes();
	} else
		relativeOffset -= srcTyped.size_bytes();

	if (relativeOffset < sizeof m_knhd) {
		const auto src = std::span(reinterpret_cast<const char*>(&m_knhd), sizeof m_knhd)
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length - out.size_bytes();
	} else
		relativeOffset -= sizeof m_knhd;

	if (const auto srcTyped = std::span(m_kerningEntries);
		relativeOffset < srcTyped.size_bytes()) {
		const auto src = std::span(reinterpret_cast<const char*>(srcTyped.data()), srcTyped.size_bytes())
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);

		if (out.empty())
			return length - out.size_bytes();
	}

	return length - out.size_bytes();
}

const Sqex::FontCsv::FontTableEntry* Sqex::FontCsv::FontCsvData::GetFontEntry(char32_t c) const {
	const auto val = UnicodeCodePointToUtf8Uint32(c);
	const auto it = std::lower_bound(m_fontTableEntries.begin(), m_fontTableEntries.end(), val,
		[](const FontTableEntry& l, uint32_t r) {
		return l.Utf8Value < r;
	});
	if (it == m_fontTableEntries.end() || it->Utf8Value != val)
		return nullptr;
	return &*it;
}

int Sqex::FontCsv::FontCsvData::GetKerningDistance(char32_t l, char32_t r) const {
	const auto pair = std::make_pair(UnicodeCodePointToUtf8Uint32(l), UnicodeCodePointToUtf8Uint32(r));
	const auto it = std::lower_bound(m_kerningEntries.begin(), m_kerningEntries.end(), pair,
		[](const KerningEntry& l, const std::pair<uint32_t, uint32_t>& r) {
		if (l.LeftUtf8Value == r.first)
			return l.RightUtf8Value < r.second;
		return l.LeftUtf8Value < r.first;
	});
	if (it == m_kerningEntries.end() || it->LeftUtf8Value != pair.first || it->RightUtf8Value != pair.second)
		return 0;
	return it->RightOffset;
}

const std::vector<Sqex::FontCsv::FontTableEntry>& Sqex::FontCsv::FontCsvData::GetFontTableEntries() const {
	return m_fontTableEntries;
}

const std::vector<Sqex::FontCsv::KerningEntry>& Sqex::FontCsv::FontCsvData::GetKerningEntries() const {
	return m_kerningEntries;
}

void Sqex::FontCsv::FontCsvData::AddFontEntry(char32_t c, uint16_t textureIndex, uint16_t textureOffsetX, uint16_t textureOffsetY, uint8_t boundingWidth, uint8_t boundingHeight, uint8_t nextOffsetX, uint8_t currentOffsetY) {
	const auto val = UnicodeCodePointToUtf8Uint32(c);
	auto it = std::lower_bound(m_fontTableEntries.begin(), m_fontTableEntries.end(), val,
		[](const FontTableEntry& l, uint32_t r) {
		return l.Utf8Value < r;
	});
	if (it == m_fontTableEntries.end() || it->Utf8Value != val) {
		auto entry = FontTableEntry();
		entry.Utf8Value = val;
		it = m_fontTableEntries.insert(it, entry);
		m_fcsv.FontTableHeaderOffset += sizeof entry;
	}
	it->TextureIndex = textureIndex;
	it->TextureOffsetX = textureOffsetX;
	it->TextureOffsetY = textureOffsetY;
	it->BoundingWidth = boundingWidth;
	it->BoundingHeight = boundingHeight;
	it->NextOffsetX = nextOffsetX;
	it->CurrentOffsetY = currentOffsetY;
}

void Sqex::FontCsv::FontCsvData::AddKerning(char32_t l, char32_t r, int rightOffset) {
	auto entry = KerningEntry();
	entry.Left(l);
	entry.Right(r);
	entry.RightOffset = rightOffset;

	const auto it = std::lower_bound(m_kerningEntries.begin(), m_kerningEntries.end(), entry,
		[](const KerningEntry& l, const KerningEntry& r) {
		if (l.LeftUtf8Value == r.LeftUtf8Value)
			return l.RightUtf8Value < r.RightUtf8Value;
		return l.LeftUtf8Value < r.LeftUtf8Value;
	});
	if (it->LeftUtf8Value == entry.LeftUtf8Value && it->RightUtf8Value == entry.RightUtf8Value) {
		if (rightOffset)
			it->RightOffset = rightOffset;
		else
			m_kerningEntries.erase(it);
	} else if (rightOffset)
		m_kerningEntries.insert(it, entry);
}
