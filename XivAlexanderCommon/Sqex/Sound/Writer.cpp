#include "pch.h"
#include "XivAlexanderCommon/Sqex/Sound/Writer.h"

Sqex::Sound::ScdWriter::SoundEntry Sqex::Sound::ScdWriter::SoundEntry::FromWave(const std::function<std::span<uint8_t>(size_t len, bool throwOnIncompleteRead)>& reader) {
	struct ExpectedFormat {
		LE<uint32_t> Riff;
		LE<uint32_t> RemainingSize;
		LE<uint32_t> Wave;
		LE<uint32_t> fmt_;
		LE<uint32_t> WaveFormatExSize;
	};
	const auto hdr = *reinterpret_cast<const ExpectedFormat*>(reader(sizeof ExpectedFormat, true).data());
	if (hdr.Riff != 0x46464952U || hdr.Wave != 0x45564157U || hdr.fmt_ != 0x20746D66U)
		throw std::invalid_argument("Bad file header");
		
	std::vector<uint8_t> wfbuf;
	{
		auto r = reader(hdr.WaveFormatExSize, true);
		wfbuf.insert(wfbuf.end(), r.begin(), r.end());
	}
	auto& wfex = *reinterpret_cast<WAVEFORMATEX*>(&wfbuf[0]);

	auto pos = sizeof hdr + hdr.WaveFormatExSize;
	while (pos - 8 < hdr.RemainingSize) {
		struct CodeAndLen {
			LE<uint32_t> Code;
			LE<uint32_t> Len;
		};
		const auto sectionHdr = *reinterpret_cast<const CodeAndLen*>(reader(sizeof CodeAndLen, true).data());
		pos += sizeof sectionHdr;
		if (sectionHdr.Code == 0x61746164U) {  // "data"
			auto r = reader(sectionHdr.Len, true);

			auto res = SoundEntry{
				.Header = {
					.StreamSize = sectionHdr.Len,
					.ChannelCount = wfex.nChannels,
					.SamplingRate = wfex.nSamplesPerSec,
					.Unknown_0x02E = 0,
				},
				.Data = {r.begin(), r.end()},
			};

			switch (wfex.wFormatTag) {
				case WAVE_FORMAT_PCM:
					res.Header.Format = SoundEntryHeader::EntryFormat_WaveFormatPcm;
					break;
				case WAVE_FORMAT_ADPCM:
					res.Header.Format = SoundEntryHeader::EntryFormat_WaveFormatAdpcm;
					res.ExtraData = std::move(wfbuf);
					break;
				default:
					throw std::invalid_argument("wave format not supported");
			}

			return res;
		}
		pos += sectionHdr.Len;
	}
	throw std::invalid_argument("No data section found");
}

Sqex::Sound::ScdWriter::SoundEntry Sqex::Sound::ScdWriter::SoundEntry::EmptyEntry() {
	return {
		.Header = {
			.Format = SoundEntryHeader::EntryFormat_Empty,
		},
	};
}

Sqex::Sound::ScdWriter::SoundEntry Sqex::Sound::ScdWriter::SoundEntry::EmptyEntry(std::chrono::milliseconds duration) {
	const auto blankLength = static_cast<uint32_t>(duration.count() * 2 * 44100 / 1000);
	return {
		.Header = {
			.StreamSize = blankLength,
			.ChannelCount = 1,
			.SamplingRate = 44100,
			.Format = SoundEntryHeader::EntryFormat_WaveFormatPcm,
		},
		.Data = std::vector<uint8_t>(blankLength),
	};
}

size_t Sqex::Sound::ScdWriter::SoundEntry::CalculateEntrySize() const {
	size_t auxLength = 0;
	for (const auto& aux : AuxChunks | std::views::values)
		auxLength += 8 + aux.size();

	return sizeof SoundEntryHeader + auxLength + ExtraData.size() + Data.size();
}

void Sqex::Sound::ScdWriter::SoundEntry::ExportTo(std::vector<uint8_t>& res) const {
	const auto insert = [&res](const auto& v) {
		res.insert(res.end(), reinterpret_cast<const uint8_t*>(&v), reinterpret_cast<const uint8_t*>(&v) + sizeof v);
	};

	const auto entrySize = CalculateEntrySize();
	auto hdr = Header;
	hdr.StreamOffset = static_cast<uint32_t>(entrySize - Data.size() - sizeof hdr);
	hdr.StreamSize = static_cast<uint32_t>(Data.size());
	hdr.AuxChunkCount = static_cast<uint16_t>(AuxChunks.size());

	res.reserve(res.size() + entrySize);
	insert(hdr);
	for (const auto& [name, aux] : AuxChunks) {
		if (name.size() != 4)
			throw std::invalid_argument(std::format("Length of name must be 4, got \"{}\"({})", name, name.size()));
		res.insert(res.end(), name.begin(), name.end());
		insert(static_cast<uint32_t>(8 + aux.size()));
		res.insert(res.end(), aux.begin(), aux.end());
	}
	res.insert(res.end(), ExtraData.begin(), ExtraData.end());
	res.insert(res.end(), Data.begin(), Data.end());
}

void Sqex::Sound::ScdWriter::SetTable1(std::vector<std::vector<uint8_t>> t) {
	m_table1 = std::move(t);
}

void Sqex::Sound::ScdWriter::SetTable2(std::vector<std::vector<uint8_t>> t) {
	m_table2 = std::move(t);
}

void Sqex::Sound::ScdWriter::SetTable4(std::vector<std::vector<uint8_t>> t) {
	m_table4 = std::move(t);
}

void Sqex::Sound::ScdWriter::SetTable5(std::vector<std::vector<uint8_t>> t) {
	// Apparently the game still plays sounds without this table

	m_table5 = std::move(t);
}

void Sqex::Sound::ScdWriter::SetSoundEntry(size_t index, SoundEntry entry) {
	if (m_soundEntries.size() <= index)
		m_soundEntries.resize(index + 1);
	m_soundEntries[index] = std::move(entry);
}

std::vector<uint8_t> Sqex::Sound::ScdWriter::Export() const {
	if (m_table1.size() != m_table4.size())
		throw std::invalid_argument("table1.size != table4.size");

	const auto table1OffsetsOffset = sizeof ScdHeader + sizeof Offsets;
	const auto table2OffsetsOffset = Sqex::Align<size_t>(table1OffsetsOffset + sizeof(uint32_t) * (1 + m_table1.size()), 0x10).Alloc;
	const auto soundEntryOffsetsOffset = Sqex::Align<size_t>(table2OffsetsOffset + sizeof(uint32_t) * (1 + m_table2.size()), 0x10).Alloc;
	const auto table4OffsetsOffset = Sqex::Align<size_t>(soundEntryOffsetsOffset + sizeof(uint32_t) * (1 + m_soundEntries.size()), 0x10).Alloc;
	const auto table5OffsetsOffset = Sqex::Align<size_t>(table4OffsetsOffset + sizeof(uint32_t) * (1 + m_table4.size()), 0x10).Alloc;

	std::vector<uint8_t> res;
	size_t requiredSize = table5OffsetsOffset + sizeof(uint32_t) * 4;
	for (const auto& item : m_table4)
		requiredSize += item.size();
	for (const auto& item : m_table1)
		requiredSize += item.size();
	for (const auto& item : m_table2)
		requiredSize += item.size();
	for (const auto& item : m_table5)
		requiredSize += item.size();
	for (const auto& item : m_soundEntries)
		requiredSize += item.CalculateEntrySize();
	requiredSize = Sqex::Align<size_t>(requiredSize, 0x10).Alloc;
	res.reserve(requiredSize);

	res.resize(table5OffsetsOffset + sizeof(uint32_t) * 4);

	for (size_t i = 0; i < m_table4.size(); ++i) {
		reinterpret_cast<uint32_t*>(&res[table4OffsetsOffset])[i] = static_cast<uint32_t>(res.size());
		res.insert(res.end(), m_table4[i].begin(), m_table4[i].end());
	}
	for (size_t i = 0; i < m_table1.size(); ++i) {
		reinterpret_cast<uint32_t*>(&res[table1OffsetsOffset])[i] = static_cast<uint32_t>(res.size());
		res.insert(res.end(), m_table1[i].begin(), m_table1[i].end());
	}
	for (size_t i = 0; i < m_table2.size(); ++i) {
		reinterpret_cast<uint32_t*>(&res[table2OffsetsOffset])[i] = static_cast<uint32_t>(res.size());
		res.insert(res.end(), m_table2[i].begin(), m_table2[i].end());
	}
	for (size_t i = 0; i < m_table5.size() && i < 3; ++i) {
		if (m_table5[i].empty())
			break;
		reinterpret_cast<uint32_t*>(&res[table5OffsetsOffset])[i] = static_cast<uint32_t>(res.size());
		res.insert(res.end(), m_table5[i].begin(), m_table5[i].end());
	}
	for (size_t i = 0; i < m_soundEntries.size(); ++i) {
		reinterpret_cast<uint32_t*>(&res[soundEntryOffsetsOffset])[i] = static_cast<uint32_t>(res.size());
		m_soundEntries[i].ExportTo(res);
	}

	*reinterpret_cast<ScdHeader*>(&res[0]) = {
		.SedbVersion = ScdHeader::SedbVersion_FFXIV,
		.EndianFlag = ScdHeaderEndiannessFlag::LittleEndian,
		.SscfVersion = ScdHeader::SscfVersion_FFXIV,
		.HeaderSize = sizeof ScdHeader,
		.FileSize = static_cast<uint32_t>(requiredSize),
	};
	memcpy(reinterpret_cast<ScdHeader*>(&res[0])->SedbSignature,
		ScdHeader::SedbSignature_Value,
		sizeof ScdHeader::SedbSignature_Value);
	memcpy(reinterpret_cast<ScdHeader*>(&res[0])->SscfSignature,
		ScdHeader::SscfSignature_Value,
		sizeof ScdHeader::SscfSignature_Value);

	*reinterpret_cast<Offsets*>(&res[sizeof ScdHeader]) = {
		.Table1And4EntryCount = static_cast<uint16_t>(m_table1.size()),
		.Table2EntryCount = static_cast<uint16_t>(m_table2.size()),
		.SoundEntryCount = static_cast<uint16_t>(m_soundEntries.size()),
		.Unknown_0x006 = 0,  // ?
		.Table2Offset = static_cast<uint32_t>(table2OffsetsOffset),
		.SoundEntryOffset = static_cast<uint32_t>(soundEntryOffsetsOffset),
		.Table4Offset = static_cast<uint32_t>(table4OffsetsOffset),
		.Table5Offset = static_cast<uint32_t>(table5OffsetsOffset),
		.Unknown_0x01C = 0,  // ?
	};

	res.resize(requiredSize);

	return res;
}
