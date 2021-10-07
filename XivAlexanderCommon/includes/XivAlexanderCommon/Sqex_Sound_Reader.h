#pragma once

#include "Sqex.h"
#include "Sqex_Sound.h"

namespace Sqex::Sound {
	class ScdReader {
		const std::shared_ptr<RandomAccessStream> m_stream;
		const std::vector<uint8_t> m_headerBuffer;
		const ScdHeader& m_header;
		const Offsets& m_offsets;

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

		[[nodiscard]] std::vector<uint8_t> ReadEntry(const std::span<const uint32_t>& offsets, uint32_t endOffset, uint32_t index) const;
		[[nodiscard]] std::vector<std::vector<uint8_t>> ReadEntries(const std::span<const uint32_t>& offsets, uint32_t endOffset) const;

	public:
		ScdReader(std::shared_ptr<RandomAccessStream> stream);

		struct SoundEntry {
			std::vector<uint8_t> Buffer;
			SoundEntryHeader* Header;
			std::vector<SoundEntryAuxChunk*> AuxChunks;
			std::span<uint8_t> ExtraData;
			std::span<uint8_t> Data;

			[[nodiscard]] const ADPCMWAVEFORMAT& GetMsAdpcmHeader() const;
			[[nodiscard]] std::vector<uint8_t> GetMsAdpcmWavFile() const;

			[[nodiscard]] const SoundEntryOggHeader& GetOggSeekTableHeader() const;
			[[nodiscard]] std::span<const uint32_t> GetOggSeekTable() const;
			[[nodiscard]] std::vector<uint8_t> GetOggFile() const;
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

		[[nodiscard]] size_t GetSoundEntryCount() const { return m_soundEntryOffsets.size(); }
		[[nodiscard]] SoundEntry GetSoundEntry(size_t entryIndex) const;
	};
}
