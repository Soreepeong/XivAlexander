#pragma once

#include "Sqex_Sqpack.h"
#include "Sqex_Texture.h"
#include "Utils_Win32_Handle.h"

namespace Sqex::Sqpack {
	static constexpr uint16_t BlockDataSize = 16000;
	static constexpr uint16_t BlockValidSize = BlockDataSize + sizeof SqData::BlockHeader;
	static constexpr uint16_t BlockPadSize = (EntryAlignment - BlockValidSize) % EntryAlignment;
	static constexpr uint16_t BlockSize = BlockValidSize + BlockPadSize;

	class EntryProvider : public RandomAccessStream {
		EntryPathSpec m_pathSpec;

	public:

		EntryProvider(EntryPathSpec pathSpec)
			: m_pathSpec(std::move(pathSpec)) {
		}
		
		bool UpdatePathSpec(const EntryPathSpec& r) {
			if (m_pathSpec != r)
				return false;
			if (!m_pathSpec.HasFullPathHash())
				m_pathSpec.FullPathHash = r.FullPathHash;
			if (!m_pathSpec.HasComponentHash()) {
				m_pathSpec.PathHash = r.PathHash;
				m_pathSpec.NameHash = r.NameHash;
			}
			return true;
		}

		const EntryPathSpec& PathSpec() const {
			return m_pathSpec;
		}

		[[nodiscard]] virtual SqData::FileEntryType EntryType() const = 0;
	};

	class EmptyEntryProvider : public EntryProvider {
	public:
		using EntryProvider::EntryProvider;

		[[nodiscard]] uint64_t StreamSize() const override {
			return sizeof SqData::FileEntryHeader;
		}

		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override;

		[[nodiscard]] SqData::FileEntryType EntryType() const override {
			return SqData::FileEntryType::Empty;
		}
	};

	class OnTheFlyBinaryEntryProvider : public EntryProvider {
		const std::filesystem::path m_path;
		SqData::FileEntryHeader m_header{};

		uint32_t m_padBeforeData = 0;
		uint32_t m_size;

		mutable Utils::Win32::File m_hFile;

	public:
		OnTheFlyBinaryEntryProvider(EntryPathSpec, std::filesystem::path path);

		[[nodiscard]] uint64_t StreamSize() const override;

		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override;

		[[nodiscard]] SqData::FileEntryType EntryType() const override {
			return SqData::FileEntryType::Binary;
		}
	};

	class MemoryBinaryEntryProvider : public EntryProvider {
		std::vector<char> m_data;

	public:
		MemoryBinaryEntryProvider(EntryPathSpec, const std::filesystem::path& path);

		[[nodiscard]] uint64_t StreamSize() const override {
			return static_cast<uint32_t>(m_data.size());
		}

		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override;

		[[nodiscard]] SqData::FileEntryType EntryType() const override {
			return SqData::FileEntryType::Binary;
		}
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
		OnTheFlyModelEntryProvider(EntryPathSpec, const std::filesystem::path& path);

	private:
		[[nodiscard]] AlignResult<uint32_t> AlignEntry() const {
			return Align(m_header.Entry.HeaderSize + (m_paddedBlockSizes.empty() ? 0 : m_blockOffsets.back() + m_paddedBlockSizes.back()));
		}

		[[nodiscard]] uint32_t NextBlockOffset() const {
			return m_paddedBlockSizes.empty() ? 0U : Align(m_blockOffsets.back() + m_paddedBlockSizes.back());
		}

	public:
		[[nodiscard]] uint64_t StreamSize() const override {
			return AlignEntry();

		}

		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override;

		[[nodiscard]] SqData::FileEntryType EntryType() const override {
			return SqData::FileEntryType::Model;
		}
	};

	class OnTheFlyTextureEntryProvider : public EntryProvider {
		/*
		 * [MergedHeader]
		 * - [FileEntryHeader]
		 * - [TextureBlockHeaderLocator] * FileEntryHeader.BlockCount
		 * - SubBlockSize: [uint16_t] * TextureBlockHeaderLocator.SubBlockCount * FileEntryHeader.BlockCount
		 * - [TextureHeaderBytes]
		 * - - [TextureHeader]
		 * - - MipmapOffset: [uint32_t] * TextureHeader.MipmapCount
		 * - - [ExtraHeader]
		 * [BlockHeader, Data] * TextureBlockHeaderLocator.SubBlockCount * TextureHeader.MipmapCount
		 */
		const Utils::Win32::File m_hFile;

		std::vector<SqData::TextureBlockHeaderLocator> m_blockLocators;
		std::vector<uint16_t> m_subBlockSizes;
		std::vector<uint8_t> m_texHeaderBytes;

		std::vector<uint8_t> m_mergedHeader;

		std::vector<uint32_t> m_mipmapSizes;
		size_t m_size = 0;

		[[nodiscard]] auto AsTexHeader() const { return *reinterpret_cast<const Texture::Header*>(&m_texHeaderBytes[0]); }
		[[nodiscard]] auto AsMipmapOffsets() const { return std::span(reinterpret_cast<const uint32_t*>(&m_texHeaderBytes[sizeof Texture::Header]), AsTexHeader().MipmapCount); }

	public:
		OnTheFlyTextureEntryProvider(EntryPathSpec, const std::filesystem::path& path);

		[[nodiscard]] uint64_t StreamSize() const override {
			return static_cast<uint32_t>(m_size);
		}

		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override;

		[[nodiscard]] SqData::FileEntryType EntryType() const override {
			return SqData::FileEntryType::Texture;
		}
	};

	class PartialFileViewEntryProvider : public EntryProvider {
		const Utils::Win32::File m_file;
		const uint64_t m_offset;
		const uint64_t m_size;

		mutable bool m_entryTypeFetched = false;
		mutable SqData::FileEntryType m_entryType = SqData::FileEntryType::Empty;

	public:
		PartialFileViewEntryProvider(EntryPathSpec pathSpec, Utils::Win32::File file, uint64_t offset, uint64_t length)
			: EntryProvider(std::move(pathSpec))
			, m_file(std::move(file))
			, m_offset(offset)
			, m_size(length) {
		}

		[[nodiscard]] uint64_t StreamSize() const override {
			return m_size;
		}

		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override {
			if (offset >= m_size)
				return 0;

			return m_file.Read(m_offset + offset, buf, static_cast<size_t>(std::min(length, m_size - offset)));
		}

		[[nodiscard]] SqData::FileEntryType EntryType() const override {
			if (!m_entryTypeFetched) {
				m_entryType = m_file.Read<SqData::FileEntryHeader>(m_offset).Type;
				m_entryTypeFetched = true;
			}
			return m_entryType;
		}
	};
}
