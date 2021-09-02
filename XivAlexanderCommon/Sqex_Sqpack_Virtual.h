#pragma once

#include <span>

#include "Sqex_Sqpack.h"
#include "Utils_Win32_Handle.h"

namespace Sqex::Sqpack {

	class VirtualSqPack {
		static constexpr uint16_t BlockDataSize = 16000;
		static constexpr uint16_t BlockValidSize = BlockDataSize + sizeof SqData::BlockHeader;
		static constexpr uint16_t BlockPadSize = (EntryAlignment - BlockValidSize) % EntryAlignment;
		static constexpr uint16_t BlockSize = BlockValidSize + BlockPadSize;

		class Implementation;

		class EntryProvider : public RandomAccessStream {
		public:
			[[nodiscard]] virtual SqData::FileEntryType EntryType() const = 0;
		};

		class EmptyEntryProvider : public EntryProvider {
		public:
			[[nodiscard]] uint32_t StreamSize() const override;
			size_t ReadStreamPartial(uint64_t offset, void* buf, size_t length) const override;
			[[nodiscard]] SqData::FileEntryType EntryType() const override;
		};

		class OnTheFlyBinaryEntryProvider : public EntryProvider {
			const std::filesystem::path m_path;
			SqData::FileEntryHeader m_header{};

			uint32_t m_padBeforeData = 0;

			mutable Utils::Win32::File m_hFile;

		public:
			OnTheFlyBinaryEntryProvider(std::filesystem::path path);
			~OnTheFlyBinaryEntryProvider() override;

			[[nodiscard]] uint32_t StreamSize() const override;
			size_t ReadStreamPartial(uint64_t offset, void* buf, size_t length) const override;
			[[nodiscard]] SqData::FileEntryType EntryType() const override;
		};

		class MemoryBinaryEntryProvider : public EntryProvider {
			std::vector<char> m_data;

		public:
			MemoryBinaryEntryProvider(const std::filesystem::path& path);
			~MemoryBinaryEntryProvider() override;

			[[nodiscard]] uint32_t StreamSize() const override;
			size_t ReadStreamPartial(uint64_t offset, void* buf, size_t length) const override;
			[[nodiscard]] SqData::FileEntryType EntryType() const override;
		};

		class OnTheFlyModelEntryProvider : public EntryProvider {
			const Utils::Win32::File m_hFile;

			struct ModelEntryHeader {
				SqData::FileEntryHeader Entry;
				SqData::ModelBlockLocator Model;
			};

			ModelEntryHeader m_header{};
			std::vector<uint32_t> m_blockOffsets;
			std::vector<uint16_t> m_blockDataSizes;
			std::vector<uint16_t> m_paddedBlockSizes;
			std::vector<uint32_t> m_actualFileOffsets;

		public:
			OnTheFlyModelEntryProvider(const std::filesystem::path& path);
			~OnTheFlyModelEntryProvider() override;

		private:
			[[nodiscard]] AlignResult<uint32_t> AlignEntry() const;
			[[nodiscard]] uint32_t NextBlockOffset() const;

		public:
			[[nodiscard]] uint32_t StreamSize() const override;
			size_t ReadStreamPartial(uint64_t offset, void* buf, size_t length) const override;
			[[nodiscard]] SqData::FileEntryType EntryType() const override;
		};

		class OnTheFlyTextureEntryProvider : public EntryProvider {
			/*
			 * [MergedHeader]
			 * - [FileEntryHeader]
			 * - [TextureBlockHeaderLocator] * FileEntryHeader.BlockCount
			 * - SubBlockSize: [uint16_t] * TextureBlockHeaderLocator.SubBlockCount * FileEntryHeader.BlockCount
			 * - [TexHeaderBytes]
			 * - - [TexHeader]
			 * - - MipmapOffset: [uint32_t] * TexHeader.MipmapCount
			 * - - [ExtraHeader]
			 * [BlockHeader, Data] * TextureBlockHeaderLocator.SubBlockCount * TexHeader.MipmapCount
			 */
			const Utils::Win32::File m_hFile;

			std::vector<SqData::TextureBlockHeaderLocator> m_blockLocators;
			std::vector<uint16_t> m_subBlockSizes;
			std::vector<uint8_t> m_texHeaderBytes;

			std::vector<uint8_t> m_mergedHeader;

			std::vector<uint32_t> m_mipmapSizes;
			size_t m_size = 0;

			[[nodiscard]] const SqData::TexHeader& AsTexHeader() const;
			[[nodiscard]] std::span<const uint32_t> AsMipmapOffsets() const;

		public:
			OnTheFlyTextureEntryProvider(std::filesystem::path path);
			~OnTheFlyTextureEntryProvider() override;

			[[nodiscard]] uint32_t StreamSize() const override;
			size_t ReadStreamPartial(uint64_t offset, void* buf, size_t length) const override;
			[[nodiscard]] SqData::FileEntryType EntryType() const override;
		};

		class PartialFileViewEntryProvider : public EntryProvider {
			const Utils::Win32::File m_file;
			const uint64_t m_offset;
			const uint32_t m_size;

			mutable bool m_entryTypeFetched = false;
			mutable SqData::FileEntryType m_entryType = SqData::FileEntryType::Empty;

		public:
			PartialFileViewEntryProvider(Utils::Win32::File file, uint64_t offset, uint32_t length);
			~PartialFileViewEntryProvider() override;

			[[nodiscard]] uint32_t StreamSize() const override;
			size_t ReadStreamPartial(uint64_t offset, void* buf, size_t length) const override;
			[[nodiscard]] SqData::FileEntryType EntryType() const override;
		};

		struct Entry {
			static constexpr auto NoEntryHash = 0xFFFFFFFF;

			uint32_t PathHash;
			uint32_t NameHash;
			uint32_t FullPathHash;

			uint32_t DataFileIndex;
			uint32_t BlockSize;
			uint32_t PadSize;
			uint64_t OffsetAfterHeaders;

			SqIndex::LEDataLocator Locator;

			std::unique_ptr<EntryProvider> Provider;
		};

		std::vector<std::unique_ptr<Entry>> m_entries;
		std::map<std::pair<uint32_t, uint32_t>, Entry*> m_pathNameTupleEntryPointerMap;
		std::map<uint32_t, Entry*> m_fullPathEntryPointerMap;

		std::vector<SqIndex::FileSegmentEntry> m_fileEntries1;
		std::vector<SqIndex::FileSegmentEntry2> m_fileEntries2;
		std::vector<SqIndex::FolderSegmentEntry> m_folderEntries;

		bool m_frozen = false;

		std::vector<Utils::Win32::File> m_openFiles;

		SqpackHeader m_sqpackIndexHeader{};
		SqIndex::Header m_sqpackIndexSubHeader{};
		std::vector<char> m_sqpackIndexSegment2;
		std::vector<SqIndex::Segment3Entry> m_sqpackIndexSegment3;

		SqpackHeader m_sqpackIndex2Header{};
		SqIndex::Header m_sqpackIndex2SubHeader{};
		std::vector<char> m_sqpackIndex2Segment2;
		std::vector<SqIndex::Segment3Entry> m_sqpackIndex2Segment3;

		SqpackHeader m_sqpackDataHeader{};
		std::vector<SqData::Header> m_sqpackDataSubHeaders;

	public:
		VirtualSqPack();
		~VirtualSqPack() = default;

		struct AddEntryResult {
			size_t AddedCount;
			size_t ReplacedCount;
			size_t SkippedExistCount;

			AddEntryResult& operator+=(const AddEntryResult& r);
		};

	private:
		AddEntryResult AddEntry(uint32_t PathHash, uint32_t NameHash, uint32_t FullPathHash, std::unique_ptr<EntryProvider> provider, bool overwriteExisting = true);

	public:
		AddEntryResult AddEntriesFromSqPack(const std::filesystem::path& indexPath, bool overwriteExisting = true, bool overwriteUnknownSegments = false);
		AddEntryResult AddEntryFromFile(uint32_t PathHash, uint32_t NameHash, uint32_t FullPathHash, const std::filesystem::path& path, bool overwriteExisting = true);

		[[nodiscard]]
		size_t NumOfDataFiles() const;

	private:
		SqData::Header& AllocateDataSpace(size_t length, bool strict);

	public:
		void Freeze(bool strict);

		size_t ReadIndex1(uint64_t offset, void* buf, size_t length) const;
		size_t ReadIndex2(uint64_t offset, void* buf, size_t length) const;
		size_t ReadData(uint32_t datIndex, uint64_t offset, void* buf, size_t length) const;

		[[nodiscard]] uint64_t SizeIndex1() const;
		[[nodiscard]] uint64_t SizeIndex2() const;
		[[nodiscard]] uint64_t SizeData(uint32_t datIndex) const;
	};

}
