#pragma once

#include "Sqex_Sqpack.h"
#include "Utils_Win32_Handle.h"
#include "Sqex_Sqpack_EntryProvider.h"

namespace Sqex::Sqpack {
	struct Reader {
		struct SqDataEntry {
			SqIndex::FileSegmentEntry Index;
			SqIndex::FileSegmentEntry2 Index2;
			uint64_t Offset;
			uint32_t Size = UINT32_MAX;
			uint32_t DataFileIndex;
			
			SqDataEntry(const SqIndex::FileSegmentEntry& entry);
			SqDataEntry(const SqIndex::FileSegmentEntry2& entry);
		};

		struct SqIndexType {
			SqpackHeader Header{};
			SqIndex::Header IndexHeader{};
			std::vector<SqIndex::FolderSegmentEntry> Folders;
			std::map<uint32_t, std::vector<SqIndex::FileSegmentEntry>> Files;
			std::vector<char> DataFileSegment;
			std::vector<SqIndex::Segment3Entry> Segment3;

		private:
			friend struct Reader;
			SqIndexType(const Win32::File& hFile, bool strictVerify);
		};

		struct SqIndex2Type {
			SqpackHeader Header{};
			SqIndex::Header IndexHeader{};
			std::vector<SqIndex::FolderSegmentEntry> Folders;
			std::vector<SqIndex::FileSegmentEntry2> Files;
			std::vector<char> DataFileSegment;
			std::vector<SqIndex::Segment3Entry> Segment3;

		private:
			friend struct Reader;
			SqIndex2Type(const Win32::File& hFile, bool strictVerify);
		};

		struct SqDataType {
			SqpackHeader Header{};
			SqData::Header DataHeader{};
			Win32::File FileOnDisk;

		private:
			friend struct Reader;
			SqDataType(Win32::File hFile, uint32_t datIndex, std::vector<SqDataEntry>& dataEntries, bool strictVerify);
		};

		SqIndexType Index;
		SqIndex2Type Index2;
		bool Sorted;
		std::vector<SqDataEntry> Files;
		std::vector<SqDataType> Data;

		Reader(const std::filesystem::path& indexFile, bool strictVerify = false, bool sort = false);

		[[nodiscard]] std::shared_ptr<EntryProvider> GetEntryProvider(const EntryPathSpec& pathSpec, Win32::File handle = {}) const;
		[[nodiscard]] std::shared_ptr<EntryProvider> GetEntryProvider(const SqDataEntry& entry, Win32::File handle = {}) const;
	};
}
