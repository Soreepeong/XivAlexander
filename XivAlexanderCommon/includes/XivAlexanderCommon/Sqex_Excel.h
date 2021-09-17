#pragma once

#include "Sqex.h"

namespace Sqex::Excel {
	// https://xiv.dev/game-data/file-formats/excel

	namespace Exh {
		enum ColumnDataType : uint16_t {
			String = 0x0,
			Bool = 0x1,
			Int8 = 0x2,
			UInt8 = 0x3,
			Int16 = 0x4,
			UInt16 = 0x5,
			Int32 = 0x6,
			UInt32 = 0x7,
			Float32 = 0x9,
			Int64 = 0xA,
			UInt64 = 0xB,

			// 0 is read like data & 1, 1 is like data & 2, 2 = data & 4, etc...
			PackedBool0 = 0x19,
			PackedBool1 = 0x1A,
			PackedBool2 = 0x1B,
			PackedBool3 = 0x1C,
			PackedBool4 = 0x1D,
			PackedBool5 = 0x1E,
			PackedBool6 = 0x1F,
			PackedBool7 = 0x20,
		};

		struct Column {
			BE<ColumnDataType> Type;
			BE<uint16_t> Offset;
		};

		struct Pagination {
			BE<uint32_t> StartId;
			BE<uint32_t> RowCount;
		};

		enum Depth : uint8_t {
			Unknown = 0,
			Level2 = 1,
			Level3 = 2,
		};

		struct Header {
			static const char Signature_Value[4];
			static constexpr uint16_t Version_Value = 3;

			char Signature[4]{};
			BE<uint16_t> Version;
			BE<uint16_t> FixedDataSize;
			BE<uint16_t> ColumnCount;
			BE<uint16_t> PageCount;
			BE<uint16_t> LanguageCount;
			BE<uint16_t> SomeSortOfBufferSize;
			BE<uint8_t> Padding_0x010;
			BE<Depth> Depth;
			BE<uint16_t> Padding_0x012;
			BE<uint32_t> RowCount;
			BE<uint64_t> Padding_0x018;
		};
	}

	namespace Exd {
		struct Header {
			static const char Signature_Value[4];
			static constexpr uint16_t Version_Value = 2;

			char Signature[4]{};
			BE<uint16_t> Version;
			BE<uint16_t> Padding_0x006;
			BE<uint32_t> IndexSize;
			BE<uint32_t> DataSize;
			BE<uint32_t> Padding_0x010[4];
		};

		struct RowLocator {
			BE<uint32_t> RowId;
			BE<uint32_t> Offset;
		};

#pragma pack(push, 2)
		struct RowHeader {
			BE<uint32_t> DataSize;
			BE<uint16_t> SubRowCount;
		};
#pragma pack(pop)
	}

	struct ExdColumn {
		Exh::ColumnDataType Type;
		uint8_t ValidSize;

		union {
			uint8_t Buffer[8];
			bool boolean;
			int8_t int8;
			uint8_t uint8;
			int16_t int16;
			uint16_t uint16;
			int32_t int32;
			uint32_t uint32;
			float float32;
			int64_t int64;
			uint64_t uint64;
		};

		std::string String;
	};
}
