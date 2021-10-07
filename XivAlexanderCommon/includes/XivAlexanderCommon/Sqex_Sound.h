#pragma once
#include "XaMisc.h"

namespace Sqex::Sound {
	using namespace Utils;

	enum class ScdHeaderEndiannessFlag : uint8_t {
		LittleEndian = 0,
		BigEndian = 1,
	};

	struct ScdHeader {
		static const char SedbSignature_Value[4];
		static const char SscfSignature_Value[4];
		static constexpr uint32_t SedbVersion_FFXIV = 3;
		static constexpr uint16_t SscfVersion_FFXIV = 4;

		char SedbSignature[4]{};
		char SscfSignature[4]{};
		LE<uint32_t> SedbVersion;
		ScdHeaderEndiannessFlag EndianFlag{};
		uint8_t SscfVersion{};
		LE<uint16_t> HeaderSize;
		LE<uint32_t> FileSize;
		uint8_t Padding_0x014[0x1C]{};
	};

	static_assert(sizeof ScdHeader == 0x30);

	struct Offsets {
		LE<uint16_t> Table1And4EntryCount;
		LE<uint16_t> Table2EntryCount;
		LE<uint16_t> SoundEntryCount;
		LE<uint16_t> Unknown_0x006;
		LE<uint32_t> Table2Offset;
		LE<uint32_t> SoundEntryOffset;
		LE<uint32_t> Table4Offset;
		LE<uint32_t> Padding_0x014;
		LE<uint32_t> Table5Offset;
		LE<uint32_t> Unknown_0x01C;
	};
	static_assert(sizeof Offsets == 0x20);

	struct SoundEntryHeader {
		enum EntryFormat : uint32_t {
			EntryFormat_WaveFormatPcm = 0x01,
			EntryFormat_Ogg = 0x06,
			EntryFormat_WaveFormatAdpcm = 0x0C,
			EntryFormat_Empty = 0xFFFFFFFF,
		};

		LE<uint32_t> StreamSize;
		LE<uint32_t> ChannelCount;
		LE<uint32_t> SamplingRate;
		LE<EntryFormat> Format;
		LE<uint32_t> LoopStartOffset;
		LE<uint32_t> LoopEndOffset;
		LE<uint32_t> StreamOffset;
		LE<uint16_t> AuxChunkCount;
		LE<uint16_t> Unknown_0x02E;
	};

	static_assert(sizeof SoundEntryHeader == 0x20);

	struct SoundEntryAuxChunk {
		static const char Name_Mark[4];

		char Name[4]{};
		LE<uint32_t> ChunkSize;

		union AuxChunkData {
			struct MarkChunkData {
				LE<uint32_t> Unknown_0x000;
				LE<uint32_t> Unknown_0x004;
				LE<uint32_t> Count;
				LE<uint32_t> Items[1];
			} Mark;
		} Data;
	};

	struct SoundEntryOggHeader {
		static const uint8_t Version3XorTable[256];

		uint8_t Version{};
		uint8_t HeaderSize{};
		uint8_t EncodeByte{};
		uint8_t Padding_0x003{};
		LE<uint32_t> Unknown_0x004;
		LE<uint32_t> Unknown_0x008;
		LE<uint32_t> Unknown_0x00C;
		LE<uint32_t> SeekTableSize;
		LE<uint32_t> VorbisHeaderSize;
		uint32_t Unknown_0x018{};
		uint8_t Padding_0x01C[4]{};
	};

	struct ADPCMCOEFSET {
		short iCoef1;
		short iCoef2;
	};

	struct ADPCMWAVEFORMAT {
		WAVEFORMATEX wfx;
		short wSamplesPerBlock;
		short wNumCoef;
		ADPCMCOEFSET aCoef[32];
	};
}
