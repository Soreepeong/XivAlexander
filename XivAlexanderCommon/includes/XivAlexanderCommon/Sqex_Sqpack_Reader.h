#pragma once

#include "Sqex_Sqpack.h"
#include "Utils_Win32_Handle.h"
#include "Sqex_Sqpack_EntryProvider.h"

namespace Sqex::Sqpack {
	struct Reader {
		struct SqIndexType {
			SqpackHeader Header{};
			SqIndex::Header IndexHeader{};
			std::vector<SqIndex::FolderSegmentEntry> Folders;
			std::vector<SqIndex::FileSegmentEntry> Files;
			std::vector<SqIndex::HashConflictSegmentEntry> HashConflictSegment;
			std::vector<SqIndex::Segment3Entry> Segment3;

		private:
			friend struct Reader;
			SqIndexType(const Win32::Handle& hFile, bool strictVerify);
		};

		struct SqIndex2Type {
			SqpackHeader Header{};
			SqIndex::Header IndexHeader{};
			std::vector<SqIndex::FolderSegmentEntry> Folders;
			std::vector<SqIndex::FileSegmentEntry2> Files;
			std::vector<SqIndex::HashConflictSegmentEntry2> HashConflictSegment;
			std::vector<SqIndex::Segment3Entry> Segment3;

		private:
			friend struct Reader;
			SqIndex2Type(const Win32::Handle& hFile, bool strictVerify);
		};

		struct SqDataType {
			SqpackHeader Header{};
			SqData::Header DataHeader{};
			std::shared_ptr<FileRandomAccessStream> Stream;

		private:
			friend struct Reader;
			SqDataType(Win32::Handle hFile, uint32_t datIndex, bool strictVerify);
		};

		SqIndexType Index;
		SqIndex2Type Index2;
		std::vector<SqDataType> Data;
		std::map<uint32_t, uint64_t> FileSizes;

		Reader(const std::filesystem::path& indexFile, bool strictVerify = false);

		[[nodiscard]] std::shared_ptr<EntryProvider> GetEntryProvider(const EntryPathSpec& pathSpec, SqIndex::LEDataLocator locator = {}) const;

		std::shared_ptr<RandomAccessStream> operator[](const EntryPathSpec& pathSpec) const;
	};
}
