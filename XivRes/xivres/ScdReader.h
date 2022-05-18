#ifndef _XIVRES_SCDREADER_H_
#define _XIVRES_SCDREADER_H_

#include <set>
#include <vector>

#include "Common.h"
#include "Scd.h"
#include "IStream.h"

#include "Internal/SpanCast.h"

namespace XivRes {
	class ScdReader {
		const std::shared_ptr<IStream> m_stream;
		const std::vector<uint8_t> m_headerBuffer;
		const ScdHeader& m_header;
		const ScdOffsets& m_offsets;

		const std::span<const uint32_t> m_offsetsTable1;
		const std::span<const uint32_t> m_offsetsTable2;
		const std::span<const uint32_t> m_soundEntryOffsets;
		const std::span<const uint32_t> m_offsetsTable4;
		const std::span<const uint32_t> m_offsetsTable5;

		const uint32_t m_endOfSoundEntries;
		const uint32_t m_endOfTable5;
		const uint32_t m_endOfTable2;
		const uint32_t m_endOfTable1;
		const uint32_t m_endOfTable4;

		[[nodiscard]] std::vector<uint8_t> ReadEntry(const std::span<const uint32_t>& offsets, uint32_t endOffset, size_t index) const {
			if (!offsets[index])
				return {};
			const auto next = index == offsets.size() - 1 || offsets[index + 1] == 0 ? endOffset : offsets[index + 1];
			return ReadStreamIntoVector<uint8_t>(*m_stream, offsets[index], next - offsets[index]);
		}

		[[nodiscard]] std::vector<std::vector<uint8_t>> ReadEntries(const std::span<const uint32_t>& offsets, uint32_t endOffset) const {
			std::vector<std::vector<uint8_t>> res;
			for (uint32_t i = 0; i < offsets.size(); ++i)
				res.emplace_back(ReadEntry(offsets, endOffset, i));
			return res;
		}

		[[nodiscard]] static std::vector<uint8_t> GetHeaderBytes(const IStream& stream) {
			constexpr auto InitialBufferSize = 8192ULL;
			std::vector<uint8_t> res;
			res.resize(static_cast<size_t>((std::min<uint64_t>)(InitialBufferSize, stream.StreamSize())));
			ReadStream(stream, 0, std::span(res));

			const auto& header = *reinterpret_cast<ScdHeader*>(&res[0]);
			if (header.HeaderSize != sizeof header)
				throw std::invalid_argument("invalid HeaderSize");

			const auto& offsets = *reinterpret_cast<ScdOffsets*>(&res[header.HeaderSize]);
			res.resize(static_cast<size_t>(0) + offsets.Table5Offset + 16);
			if (res.size() > InitialBufferSize)
				ReadStream(stream, InitialBufferSize, std::span(res).subspan(InitialBufferSize));
			return res;
		}

	public:
		ScdReader(std::shared_ptr<IStream> stream)
			: m_stream(std::move(stream))
			, m_headerBuffer(GetHeaderBytes(*m_stream))
			, m_header(*reinterpret_cast<const ScdHeader*>(&m_headerBuffer[0]))
			, m_offsets(*reinterpret_cast<const ScdOffsets*>(&m_headerBuffer[m_header.HeaderSize]))
			, m_offsetsTable1(Internal::span_cast<uint32_t>(m_headerBuffer, m_header.HeaderSize + sizeof m_offsets, m_offsets.Table1And4EntryCount))
			, m_offsetsTable2(Internal::span_cast<uint32_t>(m_headerBuffer, m_offsets.Table2Offset, m_offsets.Table2EntryCount))
			, m_soundEntryOffsets(Internal::span_cast<uint32_t>(m_headerBuffer, m_offsets.SoundEntryOffset, m_offsets.SoundEntryCount))
			, m_offsetsTable4(Internal::span_cast<uint32_t>(m_headerBuffer, m_offsets.Table4Offset, m_offsets.Table1And4EntryCount))
			, m_offsetsTable5(Internal::span_cast<uint32_t>(m_headerBuffer, m_offsets.Table5Offset, (m_headerBuffer.size() - m_offsets.Table5Offset), 1))
			, m_endOfSoundEntries(m_header.FileSize)
			, m_endOfTable5(!m_soundEntryOffsets.empty() && m_soundEntryOffsets.front() ? m_soundEntryOffsets.front() : m_endOfSoundEntries)
			, m_endOfTable2(!m_offsetsTable5.empty() && m_offsetsTable5.front() ? m_offsetsTable5.front() : m_endOfTable5)
			, m_endOfTable1(!m_offsetsTable2.empty() && m_offsetsTable2.front() ? m_offsetsTable2.front() : m_endOfTable2)
			, m_endOfTable4(!m_offsetsTable1.empty() && m_offsetsTable1.front() ? m_offsetsTable1.front() : m_endOfTable1) {
		}

		struct SoundEntry {
			std::vector<uint8_t> Buffer;
			ScdSoundEntryHeader* Header;
			std::vector<ScdSoundEntryAuxChunk*> AuxChunks;
			std::span<uint8_t> ExtraData;
			std::span<uint8_t> Data;

			[[nodiscard]] std::set<uint32_t> GetMarkedSampleBlockIndices() const {
				std::set<uint32_t> res;
				for (const auto& chunk : AuxChunks) {
					if (memcmp(chunk->Name, ScdSoundEntryAuxChunk::Name_Mark, sizeof chunk->Name) != 0)
						continue;

					for (uint32_t i = 0, i_ = *chunk->Data.Mark.Count; i < i_; i++)
						res.insert(*chunk->Data.Mark.SampleBlockIndices[i]);
				}
				return res;
			}

			[[nodiscard]] const AdpcmWaveFormat& GetMsAdpcmHeader() const {
				if (Header->Format != ScdSoundEntryFormat::WaveFormatAdpcm)
					throw std::invalid_argument("Not MS-ADPCM");
				if (ExtraData.size_bytes() < sizeof ScdSoundEntryOggHeader)
					throw std::invalid_argument("ExtraData too small to fit MsAdpcmHeader");
				const auto& header = *reinterpret_cast<const AdpcmWaveFormat*>(&ExtraData[0]);
				if (sizeof header.wfx + header.wfx.cbSize != ExtraData.size_bytes())
					throw std::invalid_argument("invalid OggSeekTableHeader size");
				return header;
			}

			template<typename T = uint8_t, typename = std::enable_if_t<sizeof T == 1>>
			[[nodiscard]] std::vector<T> GetMsAdpcmWavFile() const {
				const auto& hdr = GetMsAdpcmHeader();
				const auto headerSpan = ExtraData.subspan(0, sizeof hdr.wfx + hdr.wfx.cbSize);
				std::vector<uint8_t> res;
				const auto insert = [&res](const auto& v) {
					res.insert(res.end(), reinterpret_cast<const uint8_t*>(&v), reinterpret_cast<const uint8_t*>(&v) + sizeof v);
				};
				const auto totalLength = static_cast<uint32_t>(size_t()
					+ 12  // "RIFF"####"WAVE"
					+ 8 + headerSpan.size() // "fmt "####<header>
					+ 8 + Data.size()  // "data"####<data>
					);
				res.reserve(totalLength);
				insert(LE<uint32_t>(0x46464952U));  // "RIFF"
				insert(LE<uint32_t>(totalLength - 8));
				insert(LE<uint32_t>(0x45564157U));  // "WAVE"
				insert(LE<uint32_t>(0x20746D66U));  // "fmt "
				insert(LE<uint32_t>(static_cast<uint32_t>(headerSpan.size())));
				res.insert(res.end(), headerSpan.begin(), headerSpan.end());
				insert(LE<uint32_t>(0x61746164U));  // "data"
				insert(LE<uint32_t>(static_cast<uint32_t>(Data.size())));
				res.insert(res.end(), Data.begin(), Data.end());
				return std::move(*reinterpret_cast<std::vector<T>*>(&res));;
			}

			[[nodiscard]] const ScdSoundEntryOggHeader& GetOggSeekTableHeader() const {
				if (Header->Format != ScdSoundEntryFormat::Ogg)
					throw std::invalid_argument("Not ogg");
				if (ExtraData.size_bytes() < sizeof ScdSoundEntryOggHeader)
					throw std::invalid_argument("ExtraData too small to fit OggSeekTableHeader");
				const auto& header = *reinterpret_cast<ScdSoundEntryOggHeader*>(&ExtraData[0]);
				if (header.HeaderSize != sizeof header)
					throw std::invalid_argument("invalid OggSeekTableHeader size");
				return header;
			}

			[[nodiscard]] std::span<const uint32_t> GetOggSeekTable() const {
				const auto& tbl = GetOggSeekTableHeader();
				const auto span = ExtraData.subspan(tbl.HeaderSize, tbl.SeekTableSize);
				return Internal::span_cast<uint32_t>(span);
			}

			template<typename T = uint8_t, typename = std::enable_if_t<sizeof T == 1>>
			[[nodiscard]] std::vector<T> GetOggFile() const {
				const auto& tbl = GetOggSeekTableHeader();
				const auto header = ExtraData.subspan(tbl.HeaderSize + tbl.SeekTableSize, tbl.VorbisHeaderSize);
				std::vector<uint8_t> res;
				res.reserve(header.size() + Data.size());
				res.insert(res.end(), header.begin(), header.end());
				res.insert(res.end(), Data.begin(), Data.end());

				if (tbl.Version == 0x2) {
					if (tbl.EncodeByte) {
						for (auto& c : std::span(res).subspan(0, header.size()))
							c ^= tbl.EncodeByte;
					}
				} else if (tbl.Version == 0x3) {
					const auto byte1 = static_cast<uint8_t>(Data.size() & 0x7F);
					const auto byte2 = static_cast<uint8_t>(Data.size() & 0x3F);
					for (size_t i = 0; i < res.size(); i++)
						res[i] ^= ScdSoundEntryOggHeader::Version3XorTable[(byte2 + i) & 0xFF] ^ byte1;
				} else {
					throw CorruptDataException(std::format("Unsupported scd ogg header version: {}", tbl.Version));
				}
				return std::move(*reinterpret_cast<std::vector<T>*>(&res));
			}
		};

		[[nodiscard]] std::vector<std::vector<uint8_t>> ReadTable1Entries() const {
			return ReadEntries(m_offsetsTable1, m_endOfTable1);
		}

		[[nodiscard]] std::vector<std::vector<uint8_t>> ReadTable2Entries() const {
			return ReadEntries(m_offsetsTable2, m_endOfTable2);
		}

		[[nodiscard]] std::vector<SoundEntry> ReadSoundEntries() const {
			std::vector<SoundEntry> res;
			for (size_t i = 0; i < m_soundEntryOffsets.size(); ++i)
				res.push_back(GetSoundEntry(i));
			return res;
		}

		[[nodiscard]] std::vector<std::vector<uint8_t>> ReadTable4Entries() const {
			return ReadEntries(m_offsetsTable4, m_endOfTable4);
		}

		[[nodiscard]] std::vector<std::vector<uint8_t>> ReadTable5Entries() const {
			return ReadEntries(m_offsetsTable5, m_endOfTable5);
		}

		[[nodiscard]] size_t GetSoundEntryCount() const {
			return m_soundEntryOffsets.size();
		}

		[[nodiscard]] SoundEntry GetSoundEntry(size_t entryIndex) const {
			if (entryIndex >= m_soundEntryOffsets.size())
				throw std::out_of_range("entry index >= sound entry count");

			SoundEntry res{
				.Buffer = ReadEntry(m_soundEntryOffsets, m_endOfSoundEntries, static_cast<uint32_t>(entryIndex)),
				.Header = reinterpret_cast<ScdSoundEntryHeader*>(&res.Buffer[0]),
			};
			auto pos = sizeof * res.Header;
			for (size_t i = 0; i < res.Header->AuxChunkCount; ++i) {
				res.AuxChunks.emplace_back(reinterpret_cast<ScdSoundEntryAuxChunk*>(&res.Buffer[pos]));
				pos += res.AuxChunks.back()->ChunkSize;
			}
			res.ExtraData = std::span(res.Buffer).subspan(pos, res.Header->StreamOffset + sizeof * res.Header - pos);
			res.Data = std::span(res.Buffer).subspan(sizeof * res.Header + res.Header->StreamOffset, res.Header->StreamSize);
			return res;
		}
	};
}

#endif
