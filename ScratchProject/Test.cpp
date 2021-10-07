#include "pch.h"

#include <XivAlexanderCommon/Sqex_Sound_Reader.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>

class ScdWriter {
public:
	struct SoundEntry {
		Sqex::Sound::SoundEntryHeader Header;
		std::vector<uint8_t> ExtraData;
		std::vector<uint8_t> Data;

		[[nodiscard]] WAVEFORMATEX& AsWaveFormatEx() {
			return *reinterpret_cast<WAVEFORMATEX*>(&ExtraData[0]);
		}

		[[nodiscard]] const WAVEFORMATEX& AsWaveFormatEx() const {
			return *reinterpret_cast<const WAVEFORMATEX*>(&ExtraData[0]);
		}

		static SoundEntry FromWave(const Sqex::RandomAccessStream& stream) {
			struct ExpectedFormat {
				Utils::LE<uint32_t> Riff;
				Utils::LE<uint32_t> RemainingSize;
				Utils::LE<uint32_t> Wave;
				Utils::LE<uint32_t> fmt_;
				Utils::LE<uint32_t> WaveFormatExSize;
			};
			const auto hdr = stream.ReadStream<ExpectedFormat>(0);
			if (hdr.Riff != 0x46464952U || hdr.Wave != 0x45564157U || hdr.fmt_ != 0x20746D66U)
				throw std::invalid_argument("Bad file header");
			if (hdr.RemainingSize + 8 > stream.StreamSize())
				throw std::invalid_argument("Truncated file?");

			auto pos = sizeof hdr + hdr.WaveFormatExSize;
			while (pos < hdr.RemainingSize + 8) {
				struct CodeAndLen {
					Utils::LE<uint32_t> Code;
					Utils::LE<uint32_t> Len;
				};
				const auto sectionHdr = stream.ReadStream<CodeAndLen>(pos);
				pos += sizeof sectionHdr;
				if (sectionHdr.Code == 0x61746164U) {  // "data"
					auto wfbuf = stream.ReadStreamIntoVector<uint8_t>(sizeof hdr, hdr.WaveFormatExSize);
					const auto& wfex = *reinterpret_cast<const WAVEFORMATEX*>(&wfbuf[0]);
					return {
						.Header = {
							.StreamSize = sectionHdr.Len,
							.ChannelCount = wfex.nChannels,
							.SamplingRate = wfex.nSamplesPerSec,
							.Codec = Sqex::Sound::SoundEntryHeader::Codec_MsAdpcm,
						},
						.ExtraData = std::move(wfbuf),
						.Data = stream.ReadStreamIntoVector<uint8_t>(pos, sectionHdr.Len),
					};
				}
				pos += sectionHdr.Len;
			}
			throw std::invalid_argument("No data section found");
		}
	};

private:
	std::vector<std::vector<uint8_t>> m_table1;
	std::vector<std::vector<uint8_t>> m_table2;
	std::vector<SoundEntry> m_soundEntries;
	std::vector<std::vector<uint8_t>> m_table4;
	std::vector<std::vector<uint8_t>> m_table5;

public:
	void SetTable1(std::vector<std::vector<uint8_t>> t) {
		m_table1 = std::move(t);
	}

	void SetTable2(std::vector<std::vector<uint8_t>> t) {
		m_table2 = std::move(t);
	}

	void SetTable4(std::vector<std::vector<uint8_t>> t) {
		m_table4 = std::move(t);
	}

	void SetTable5(std::vector<std::vector<uint8_t>> t) {
		m_table5 = std::move(t);
	}

	void SetSoundEntry(size_t index, SoundEntry entry) {
		if (m_soundEntries.size() <= index)
			m_soundEntries.resize(index + 1);
		m_soundEntries[index] = std::move(entry);
	}
};

int main() {
	const Sqex::Sqpack::Reader readerC(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\0c0000.win32.index)");
	const auto rc_1 = Sqex::Sound::ScdReader(readerC["music/ffxiv/bgm_pvp_battle.scd"]);
	const auto rc_2 = Sqex::Sound::ScdReader(readerC["music/ffxiv/bgm_ban_odin.scd"]);
	const Sqex::Sqpack::Reader reader7(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\070000.win32.index)");
	const auto r7_1 = Sqex::Sound::ScdReader(reader7["sound/foot/dev/6999.scd"]);
	const auto r7_2 = Sqex::Sound::ScdReader(reader7["sound/system/se_emj.scd"]);
	const auto r7_3 = Sqex::Sound::ScdReader(reader7["sound/zingle/zingle_titlegamestart.scd"]);
	const auto r7_4 = Sqex::Sound::ScdReader(reader7["sound/system/se_ui.scd"]);

	// const auto v = r7_3.GetSoundEntry(0).GetMsAdpcmWavFile();
	// Utils::Win32::File::Create(L"Z:\\test.wav", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0).Write(0, std::span(v));

	const auto v = rc_1.GetSoundEntry(0).GetOggFile();
	Utils::Win32::File::Create(L"Z:\\test.ogg", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0).Write(0, std::span(v));

	return 0;
}
