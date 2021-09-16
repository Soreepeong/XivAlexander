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

		enum Variant : uint8_t {
			Unknown = 0,
			Default = 1,
			SubRows = 2,
		};

		struct Header {
			static const char Signature_Value[4];

			char Signature[4]{};
			BE<uint16_t> Unknown1;
			BE<uint16_t> DataOffset;
			BE<uint16_t> ColumnCount;
			BE<uint16_t> PageCount;
			BE<uint16_t> LanguageCount;
			BE<uint16_t> Unknown2;
			BE<uint8_t> Unknown3;
			BE<Variant> Variant;
			BE<uint16_t> Unknown4;
			BE<uint32_t> RowCount;
			BE<uint32_t> Unknown5;
			BE<uint32_t> Unknown6;
		};
	}

	namespace Exd {
		struct Header {
			static const char Signature_Value[4];

			char Signature[4]{};
			BE<uint16_t> Version;
			BE<uint16_t> Unknown1;
			BE<uint32_t> IndexSize;
			BE<uint32_t> Unknown2[5];
		};

		struct RowLocator {
			uint32_t RowId;
			uint32_t Offset;
		};

		struct RowHeader {
			uint32_t DataSize;
			uint16_t RowCount;
		};
	}
}
