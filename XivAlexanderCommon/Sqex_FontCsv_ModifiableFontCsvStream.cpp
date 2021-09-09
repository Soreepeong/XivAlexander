#include "pch.h"
#include "Sqex_FontCsv_ModifiableFontCsvStream.h"

#include "Sqex_Sqpack.h"

Sqex::FontCsv::ModifiableFontCsvStream::ModifiableFontCsvStream() {
	memcpy(m_fcsv.Signature, FontCsvHeader::Signature_Value, sizeof m_fcsv.Signature);
	memcpy(m_fthd.Signature, FontTableHeader::Signature_Value, sizeof m_fthd.Signature);
	memcpy(m_knhd.Signature, KerningHeader::Signature_Value, sizeof m_knhd.Signature);
	m_fcsv.FontTableHeaderOffset = static_cast<uint32_t>(sizeof m_fcsv);
}

Sqex::FontCsv::ModifiableFontCsvStream::ModifiableFontCsvStream(const RandomAccessStream& stream, bool strict)
	: m_fcsv(stream.ReadStream<FontCsvHeader>(0))
	, m_fthd(stream.ReadStream<FontTableHeader>(m_fcsv.FontTableHeaderOffset))
	, m_fontTableEntries(stream.ReadStreamIntoVector<FontTableEntry>(m_fcsv.FontTableHeaderOffset + sizeof m_fthd, m_fthd.FontTableEntryCount, 0x1000000))
	, m_knhd(stream.ReadStream<KerningHeader>(m_fcsv.KerningHeaderOffset))
	, m_kerningEntries(stream.ReadStreamIntoVector<KerningEntry>(m_fcsv.KerningHeaderOffset + sizeof m_knhd, std::min(m_knhd.EntryCount, m_fthd.KerningEntryCount), 0x1000000)) {
	if (strict) {
		if (0 != memcmp(m_fcsv.Signature, FontCsvHeader::Signature_Value, sizeof m_fcsv.Signature))
			throw CorruptDataException("fcsv.Signature != \"fcsv0100\"");
		if (m_fcsv.FontTableHeaderOffset != sizeof FontCsvHeader)
			throw CorruptDataException("FontTableHeaderOffset != sizeof FontCsvHeader");
		if (!IsAllSameValue(m_fcsv.Padding_0x10))
			throw CorruptDataException("fcsv.Padding_0x10 != 0");

		if (0 != memcmp(m_fthd.Signature, FontTableHeader::Signature_Value, sizeof m_fthd.Signature))
			throw CorruptDataException("fthd.Signature != \"fthd\"");
		if (!IsAllSameValue(m_fthd.Padding_0x0C))
			throw CorruptDataException("fthd.Padding_0x0C != 0");

		if (0 != memcmp(m_knhd.Signature, KerningHeader::Signature_Value, sizeof m_knhd.Signature))
			throw CorruptDataException("knhd.Signature != \"knhd\"");
		if (!IsAllSameValue(m_knhd.Padding_0x08))
			throw CorruptDataException("knhd.Padding_0x08 != 0");

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

uint64_t Sqex::FontCsv::ModifiableFontCsvStream::StreamSize() const {
	return static_cast<uint32_t>(
		sizeof m_fcsv
		+ sizeof m_fthd
		+ std::span(m_fontTableEntries).size_bytes()
		+ sizeof m_knhd
		+ std::span(m_kerningEntries).size_bytes()
		);
}

uint64_t Sqex::FontCsv::ModifiableFontCsvStream::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const {
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

	if (relativeOffset < sizeof m_fcsv) {
		const auto src = std::span(reinterpret_cast<const char*>(&m_fcsv), sizeof m_fcsv)
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty()) return length;
	} else
		relativeOffset -= sizeof m_fcsv;

	if (relativeOffset < sizeof m_fthd) {
		const auto src = std::span(reinterpret_cast<const char*>(&m_fthd), sizeof m_fthd)
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty()) return length;
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

		if (out.empty()) return length;
	} else
		relativeOffset -= srcTyped.size_bytes();

	if (relativeOffset < sizeof m_knhd) {
		const auto src = std::span(reinterpret_cast<const char*>(&m_knhd), sizeof m_knhd)
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty()) return length;
	} else
		relativeOffset -= sizeof m_knhd;

	if (const auto srcTyped = std::span(m_kerningEntries);
		relativeOffset < srcTyped.size_bytes()) {
		const auto src = std::span(reinterpret_cast<const char*>(srcTyped.data()), srcTyped.size_bytes())
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);

		if (out.empty()) return length;
	}

	return length - out.size_bytes();
}

const Sqex::FontCsv::FontTableEntry* Sqex::FontCsv::ModifiableFontCsvStream::GetFontEntry(char32_t c) const {
	const auto val = UnicodeCodePointToUtf8Uint32(c);
	const auto it = std::lower_bound(m_fontTableEntries.begin(), m_fontTableEntries.end(), val,
		[](const FontTableEntry& l, uint32_t r) {
		return l.Utf8Value < r;
	});
	if (it == m_fontTableEntries.end() || it->Utf8Value != val)
		return nullptr;
	return &*it;
}

int Sqex::FontCsv::ModifiableFontCsvStream::GetKerningDistance(char32_t l, char32_t r) const {
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

void Sqex::FontCsv::ModifiableFontCsvStream::ReserveStorage(size_t fontEntryCount, size_t kerningEntryCount) {
	m_fontTableEntries.reserve(fontEntryCount);
	m_kerningEntries.reserve(kerningEntryCount);
}

void Sqex::FontCsv::ModifiableFontCsvStream::AddFontEntry(char32_t c, uint16_t textureIndex, uint16_t textureOffsetX, uint16_t textureOffsetY, uint8_t boundingWidth, uint8_t boundingHeight, int8_t nextOffsetX, int8_t currentOffsetY) {
	const auto val = UnicodeCodePointToUtf8Uint32(c);
	const auto lock = std::lock_guard(m_fontEntryMtx);
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

void Sqex::FontCsv::ModifiableFontCsvStream::AddKerning(char32_t l, char32_t r, int rightOffset) {
	auto entry = KerningEntry();
	entry.Left(l);
	entry.Right(r);
	entry.RightOffset = rightOffset;

	const auto lock = std::lock_guard(m_kernEntryMtx);
	const auto it = std::lower_bound(m_kerningEntries.begin(), m_kerningEntries.end(), entry,
		[](const KerningEntry& l, const KerningEntry& r) {
		if (l.LeftUtf8Value == r.LeftUtf8Value)
			return l.RightUtf8Value < r.RightUtf8Value;
		return l.LeftUtf8Value < r.LeftUtf8Value;
	});
	if (it != m_kerningEntries.end() && it->LeftUtf8Value == entry.LeftUtf8Value && it->RightUtf8Value == entry.RightUtf8Value) {
		if (rightOffset)
			it->RightOffset = rightOffset;
		else
			m_kerningEntries.erase(it);
	} else if (rightOffset)
		m_kerningEntries.insert(it, entry);
}
