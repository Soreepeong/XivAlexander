#pragma once

#include <mmreg.h>

#include "Sqex.h"
#include "Sqex_Sound.h"

namespace Sqex::Sound {
	class ScdWriter {
	public:
		struct SoundEntry {
			SoundEntryHeader Header;
			std::map<std::array<char, 4>, std::vector<uint8_t>> AuxChunks;
			std::vector<uint8_t> ExtraData;
			std::vector<uint8_t> Data;

			[[nodiscard]] WAVEFORMATEX& AsWaveFormatEx() {
				return *reinterpret_cast<WAVEFORMATEX*>(&ExtraData[0]);
			}

			[[nodiscard]] const WAVEFORMATEX& AsWaveFormatEx() const {
				return *reinterpret_cast<const WAVEFORMATEX*>(&ExtraData[0]);
			}

			static SoundEntry FromWave(const RandomAccessStream& stream);
			static SoundEntry EmptyEntry();

			[[nodiscard]] size_t CalculateEntrySize() const;
			void ExportTo(std::vector<uint8_t>& res) const;
		};

	private:
		std::vector<std::vector<uint8_t>> m_table1;
		std::vector<std::vector<uint8_t>> m_table2;
		std::vector<SoundEntry> m_soundEntries;
		std::vector<std::vector<uint8_t>> m_table4;
		std::vector<std::vector<uint8_t>> m_table5;

	public:
		void SetTable1(std::vector<std::vector<uint8_t>> t);
		void SetTable2(std::vector<std::vector<uint8_t>> t);
		void SetTable4(std::vector<std::vector<uint8_t>> t);
		void SetTable5(std::vector<std::vector<uint8_t>> t);
		void SetSoundEntry(size_t index, SoundEntry entry);

		[[nodiscard]] std::vector<uint8_t> Export() const;
	};

}
