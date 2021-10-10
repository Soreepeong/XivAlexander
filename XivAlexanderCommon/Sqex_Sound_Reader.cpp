#include "pch.h"
#include "Sqex_Sound_Reader.h"

std::vector<uint8_t> Sqex::Sound::ScdReader::ReadEntry(const std::span<const uint32_t>& offsets, uint32_t endOffset, uint32_t index) const {
	if (!offsets[index])
		return {};
	const auto next = index == offsets.size() - 1 || offsets[index + 1] == 0 ? endOffset : offsets[index + 1];
	return m_stream->ReadStreamIntoVector<uint8_t>(offsets[index], next - offsets[index]);
}

std::vector<std::vector<uint8_t>> Sqex::Sound::ScdReader::ReadEntries(const std::span<const uint32_t>& offsets, uint32_t endOffset) const {
	std::vector<std::vector<uint8_t>> res;
	for (uint32_t i = 0; i < offsets.size(); ++i)
		res.emplace_back(ReadEntry(offsets, endOffset, i));
	return res;
}

Sqex::Sound::ScdReader::ScdReader(std::shared_ptr<RandomAccessStream> stream)
	: m_stream(std::move(stream))
	, m_headerBuffer([this] {
		constexpr auto InitialBufferSize = 8192ULL;
		std::vector<uint8_t> res;
		res.resize(static_cast<size_t>(std::min(InitialBufferSize, m_stream->StreamSize())));
		m_stream->ReadStream(0, std::span(res));

		const auto& header = *reinterpret_cast<ScdHeader*>(&res[0]);
		if (header.HeaderSize != sizeof header)
			throw std::invalid_argument("invalid HeaderSize");

		const auto& offsets = *reinterpret_cast<Offsets*>(&res[header.HeaderSize]);
		res.resize(static_cast<size_t>(0) + offsets.Table5Offset + 16);
		if (res.size() > InitialBufferSize)
			m_stream->ReadStream(InitialBufferSize, std::span(res).subspan(InitialBufferSize));
		return res;
	}())
	, m_header(*reinterpret_cast<const ScdHeader*>(&m_headerBuffer[0]))
	, m_offsets(*reinterpret_cast<const Offsets*>(&m_headerBuffer[m_header.HeaderSize]))
	, m_offsetsTable1(reinterpret_cast<const uint32_t*>(&m_headerBuffer[m_header.HeaderSize + sizeof m_offsets]), m_offsets.Table1And4EntryCount)
	, m_offsetsTable2(reinterpret_cast<const uint32_t*>(&m_headerBuffer[m_offsets.Table2Offset]), m_offsets.Table2EntryCount)
	, m_soundEntryOffsets(reinterpret_cast<const uint32_t*>(&m_headerBuffer[m_offsets.SoundEntryOffset]), m_offsets.SoundEntryCount)
	, m_offsetsTable4(reinterpret_cast<const uint32_t*>(&m_headerBuffer[m_offsets.Table4Offset]), m_offsets.Table1And4EntryCount)
	, m_offsetsTable5(reinterpret_cast<const uint32_t*>(&m_headerBuffer[m_offsets.Table5Offset]), (m_headerBuffer.size() - m_offsets.Table5Offset) / 4)
	, m_endOfSoundEntries(m_header.FileSize)
	, m_endOfTable5(!m_soundEntryOffsets.empty() && m_soundEntryOffsets.front() ? m_soundEntryOffsets.front() : m_endOfSoundEntries)
	, m_endOfTable2(!m_offsetsTable5.empty() && m_offsetsTable5.front() ? m_offsetsTable5.front() : m_endOfTable5)
	, m_endOfTable1(!m_offsetsTable2.empty() && m_offsetsTable2.front() ? m_offsetsTable2.front() : m_endOfTable2)
	, m_endOfTable4(!m_offsetsTable1.empty() && m_offsetsTable1.front() ? m_offsetsTable1.front() : m_endOfTable1) {
}

const Sqex::Sound::ADPCMWAVEFORMAT& Sqex::Sound::ScdReader::SoundEntry::GetMsAdpcmHeader() const {
	if (Header->Format != SoundEntryHeader::EntryFormat_WaveFormatAdpcm)
		throw std::invalid_argument("Not MS-ADPCM");
	if (ExtraData.size_bytes() < sizeof SoundEntryOggHeader)
		throw std::invalid_argument("ExtraData too small to fit MsAdpcmHeader");
	const auto& header = *reinterpret_cast<const ADPCMWAVEFORMAT*>(&ExtraData[0]);
	if (sizeof header.wfx + header.wfx.cbSize != ExtraData.size_bytes())
		throw std::invalid_argument("invalid OggSeekTableHeader size");
	return header;
}

std::vector<uint8_t> Sqex::Sound::ScdReader::SoundEntry::GetMsAdpcmWavFile() const {
	const auto& hdr = GetMsAdpcmHeader();
	const auto header = std::span(reinterpret_cast<const uint8_t*>(&hdr), sizeof hdr.wfx + hdr.wfx.cbSize);
	std::vector<uint8_t> res;
	const auto insert = [&res](const auto& v) {
		res.insert(res.end(), reinterpret_cast<const uint8_t*>(&v), reinterpret_cast<const uint8_t*>(&v) + sizeof v);
	};
	const auto totalLength = static_cast<uint32_t>(0
		+ 12  // "RIFF"####"WAVE"
		+ 8 + header.size() // "fmt "####<header>
		+ 8 + Data.size()  // "data"####<data>
	);
	res.reserve(totalLength);
	insert(LE(0x46464952U));  // "RIFF"
	insert(LE(totalLength - 8));
	insert(LE(0x45564157U));  // "WAVE"
	insert(LE(0x20746D66U));  // "fmt "
	insert(LE(static_cast<uint32_t>(header.size())));
	res.insert(res.end(), header.begin(), header.end());
	insert(LE(0x61746164U));  // "data"
	insert(LE(static_cast<uint32_t>(Data.size())));
	res.insert(res.end(), Data.begin(), Data.end());
	return res;
}

const Sqex::Sound::SoundEntryOggHeader& Sqex::Sound::ScdReader::SoundEntry::GetOggSeekTableHeader() const {
	if (Header->Format != SoundEntryHeader::EntryFormat_Ogg)
		throw std::invalid_argument("Not ogg");
	if (ExtraData.size_bytes() < sizeof SoundEntryOggHeader)
		throw std::invalid_argument("ExtraData too small to fit OggSeekTableHeader");
	const auto& header = *reinterpret_cast<SoundEntryOggHeader*>(&ExtraData[0]);
	if (header.HeaderSize != sizeof header)
		throw std::invalid_argument("invalid OggSeekTableHeader size");
	return header;
}

std::span<const uint32_t> Sqex::Sound::ScdReader::SoundEntry::GetOggSeekTable() const {
	const auto& tbl = GetOggSeekTableHeader();
	const auto span = ExtraData.subspan(tbl.HeaderSize, tbl.SeekTableSize);
	return std::span(reinterpret_cast<uint32_t*>(&span[0]), span.size() / sizeof uint32_t);
}

std::vector<uint8_t> Sqex::Sound::ScdReader::SoundEntry::GetOggFile() const {
	const auto& tbl = GetOggSeekTableHeader();
	const auto header = ExtraData.subspan(tbl.HeaderSize + tbl.SeekTableSize, tbl.VorbisHeaderSize);
	std::vector<uint8_t> res;
	res.reserve(header.size() + Data.size());
	res.insert(res.end(), header.begin(), header.end());
	res.insert(res.end(), Data.begin(), Data.end());

	if (tbl.Version == 0x2 && tbl.EncodeByte) {
		for (auto& c : std::span(res).subspan(0, header.size()))
			c ^= tbl.EncodeByte;
	} else if (tbl.Version == 0x3) {
		const auto byte1 = static_cast<uint8_t>(Data.size() & 0x7F);
		const auto byte2 = static_cast<uint8_t>(Data.size() & 0x3F);
		for (size_t i = 0; i < res.size(); i++)
			res[i] ^= SoundEntryOggHeader::Version3XorTable[(byte2 + i) & 0xFF] ^ byte1;
	}
	return res;
}

Sqex::Sound::ScdReader::SoundEntry Sqex::Sound::ScdReader::GetSoundEntry(size_t entryIndex) const {
	if (entryIndex >= m_soundEntryOffsets.size())
		throw std::out_of_range("entry index >= sound entry count");

	SoundEntry res{
		.Buffer = ReadEntry(m_soundEntryOffsets, m_endOfSoundEntries, static_cast<uint32_t>(entryIndex)),
		.Header = reinterpret_cast<SoundEntryHeader*>(&res.Buffer[0]),
	};
	auto pos = sizeof *res.Header;
	for (size_t i = 0; i < res.Header->AuxChunkCount; ++i) {
		res.AuxChunks.emplace_back(reinterpret_cast<SoundEntryAuxChunk*>(&res.Buffer[pos]));
		pos += res.AuxChunks.back()->ChunkSize;
	}
	res.ExtraData = std::span(res.Buffer).subspan(pos, res.Header->StreamOffset + sizeof *res.Header - pos);
	res.Data = std::span(res.Buffer).subspan(sizeof *res.Header + res.Header->StreamOffset, res.Header->StreamSize);
	return res;
}
