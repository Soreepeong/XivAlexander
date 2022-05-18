#ifndef _XIVRES_EXCEL_H_
#define _XIVRES_EXCEL_H_

#include "Internal/ByteOrder.h"

#include "Common.h"
#include "SeString.h"

namespace XivRes {
	// https://xiv.dev/game-data/file-formats/excel

	enum class ExcelCellType : uint16_t {
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

	struct ExhColHeader {
		BE<ExcelCellType> Type;
		BE<uint16_t> Offset;
	};

	struct ExhPagination {
		BE<uint32_t> StartId;
		BE<uint32_t> RowCountWithSkip;
	};

	enum class ExcelDepth : uint8_t {
		Unknown = 0,
		Level2 = 1,
		Level3 = 2,
	};

	struct ExhHeader {
		static constexpr char Signature_Value[4] = { 'E', 'X', 'H', 'F' };

		static constexpr uint16_t Version_Value = 3;

		char Signature[4]{};
		BE<uint16_t> Version;
		BE<uint16_t> FixedDataSize;
		BE<uint16_t> ColumnCount;
		BE<uint16_t> PageCount;
		BE<uint16_t> LanguageCount;
		BE<uint16_t> SomeSortOfBufferSize;
		BE<uint8_t> Padding_0x010;
		BE<ExcelDepth> Depth;
		BE<uint16_t> Padding_0x012;
		BE<uint32_t> RowCountWithoutSkip;
		BE<uint64_t> Padding_0x018;
	};
	struct ExdHeader {
		static constexpr char Signature_Value[4] = { 'E', 'X', 'D', 'F' };
		static constexpr uint16_t Version_Value = 2;

		char Signature[4]{};
		BE<uint16_t> Version;
		BE<uint16_t> Padding_0x006;
		BE<uint32_t> IndexSize;
		BE<uint32_t> DataSize;
		BE<uint32_t> Padding_0x010[4];
	};

	struct ExdRowLocator {
		BE<uint32_t> RowId;
		BE<uint32_t> Offset;
	};

#pragma pack(push, 2)
	struct ExdRowHeader {
		BE<uint32_t> DataSize;
		BE<uint16_t> SubRowCount;
	};
#pragma pack(pop)

	struct ExcelCell {
		ExcelCellType Type{};
		uint8_t ValidSize{};

		union {
			uint8_t Buffer[8]{};
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

		XivRes::SeString String;
	};
}

#endif
