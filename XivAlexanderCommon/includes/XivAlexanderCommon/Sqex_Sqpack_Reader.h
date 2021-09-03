#pragma once

#include "Sqex_Sqpack.h"
#include "Utils_Win32_Handle.h"

namespace Sqex::Sqpack {
	struct FileSystemSqPack {
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
			friend struct FileSystemSqPack;
			SqIndexType(const Utils::Win32::File& hFile, bool strictVerify);
		};

		struct SqIndex2Type {
			SqpackHeader Header{};
			SqIndex::Header IndexHeader{};
			std::vector<SqIndex::FolderSegmentEntry> Folders;
			std::vector<SqIndex::FileSegmentEntry2> Files;
			std::vector<char> DataFileSegment;
			std::vector<SqIndex::Segment3Entry> Segment3;

		private:
			friend struct FileSystemSqPack;
			SqIndex2Type(const Utils::Win32::File& hFile, bool strictVerify);
		};

		struct SqDataType {
			SqpackHeader Header{};
			SqData::Header DataHeader{};
			Utils::Win32::File FileOnDisk;

		private:
			friend struct FileSystemSqPack;
			SqDataType(Utils::Win32::File hFile, uint32_t datIndex, std::vector<SqDataEntry>& dataEntries, bool strictVerify);
		};

		SqIndexType Index;
		SqIndex2Type Index2;
		std::vector<SqDataEntry> Files;
		std::vector<SqDataType> Data;

		FileSystemSqPack(const std::filesystem::path& indexFile, bool strictVerify);
	};
}
