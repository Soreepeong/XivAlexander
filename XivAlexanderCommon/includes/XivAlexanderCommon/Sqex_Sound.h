#pragma once
#include "XaMisc.h"

namespace Sqex::Sound {
	enum class ScdHeaderEndiannessFlag : uint8_t {
		LittleEndian = 0,
		BigEndian = 1,
	};

	struct ScdHeader {
		static const uint8_t SedbSignature_Value[4];
		static const uint8_t SscfSignature_Value[4];
		static constexpr uint32_t SedbVersion_FFXIV = 3;
		static constexpr uint16_t SscfVersion_FFXIV = 4;

		uint8_t SedbSignature[4]{};
		uint8_t SscfSignature[4]{};
		Utils::LE<uint32_t> SedbVersion;
		ScdHeaderEndiannessFlag EndianFlag{};
		uint8_t SscfVersion{};
		Utils::LE<uint16_t> HeaderSize;
		Utils::LE<uint32_t> FileSize;
		uint8_t Padding_0x014[0x1C]{};
	};

	static_assert(sizeof ScdHeader == 0x30);

	struct Offsets {
		Utils::LE<uint16_t> Table1And4EntryCount;
		Utils::LE<uint16_t> Table2EntryCount;
		Utils::LE<uint16_t> SoundEntryCount;
		Utils::LE<uint16_t> Unknown_0x006;
		Utils::LE<uint32_t> Table2Offset;
		Utils::LE<uint32_t> SoundEntryOffset;
		Utils::LE<uint32_t> Table4Offset;
		Utils::LE<uint32_t> Padding_0x014;
		Utils::LE<uint32_t> Table5Offset;
		Utils::LE<uint32_t> Padding_0x01C;
	};

	struct SoundEntryHeader {
		enum CodecType : uint32_t {
			Codec_Ogg = 0x06,
			Codec_MsAdpcm = 0x0C,
			Codec_None = 0xFFFFFFFF,
		};

		Utils::LE<uint32_t> StreamSize;
		Utils::LE<uint32_t> ChannelCount;
		Utils::LE<uint32_t> SamplingRate;
		Utils::LE<CodecType> Codec;
		Utils::LE<uint32_t> LoopStartOffset;
		Utils::LE<uint32_t> LoopEndOffset;
		Utils::LE<uint32_t> StreamOffset;
		Utils::LE<uint16_t> AuxChunkCount;
		Utils::LE<uint16_t> Unknown_0x02E;
	};

	static_assert(sizeof SoundEntryHeader == 0x20);

	struct SoundEntryAuxChunk {
		static const uint8_t Name_Mark[4];

		uint8_t Name[4]{};
		Utils::LE<uint32_t> ChunkSize;

		union AuxChunkData {
			struct MarkChunkData {
				Utils::LE<uint32_t> Unknown_0x000;
				Utils::LE<uint32_t> Unknown_0x004;
				Utils::LE<uint32_t> Count;
				Utils::LE<uint32_t> Items[1];
			} Mark;
		} Data;
	};

	struct SoundEntryOggHeader {
		static const uint8_t Version3XorTable[256];

		uint8_t Version{};
		uint8_t HeaderSize{};
		uint8_t EncodeByte{};
		uint8_t Padding_0x003{};
		Utils::LE<uint32_t> Unknown_0x004;
		Utils::LE<uint32_t> Unknown_0x008;
		Utils::LE<uint32_t> Unknown_0x00C;
		Utils::LE<uint32_t> SeekTableSize;
		Utils::LE<uint32_t> VorbisHeaderSize;
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
