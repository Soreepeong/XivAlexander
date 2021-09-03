#pragma once

#include "Sqex_Sqpack.h"
#include "Sqex_Texture.h"
#include "Utils_Win32_Handle.h"

namespace Sqex::Sqpack {
	static constexpr uint16_t BlockDataSize = 16000;
	static constexpr uint16_t BlockValidSize = BlockDataSize + sizeof SqData::BlockHeader;
	static constexpr uint16_t BlockPadSize = (EntryAlignment - BlockValidSize) % EntryAlignment;
	static constexpr uint16_t BlockSize = BlockValidSize + BlockPadSize;

	struct EntryPathSpec {
		static constexpr auto EmptyHashValue = 0xFFFFFFFF;

		std::filesystem::path Original;

		uint32_t PathHash;
		uint32_t NameHash;
		uint32_t FullPathHash;

		EntryPathSpec()
			: PathHash(EmptyHashValue)
			, NameHash(EmptyHashValue)
			, FullPathHash(EmptyHashValue) {
		}

		EntryPathSpec(uint32_t pathHash, uint32_t nameHash, uint32_t fullPathHash)
			: PathHash(pathHash)
			, NameHash(nameHash)
			, FullPathHash(fullPathHash) {
		}

		EntryPathSpec(uint32_t pathHash, uint32_t nameHash)
			: PathHash(pathHash)
			, NameHash(nameHash)
			, FullPathHash(EmptyHashValue) {
		}

		EntryPathSpec(uint32_t fullPathHash)
			: PathHash(EmptyHashValue)
			, NameHash(EmptyHashValue)
			, FullPathHash(fullPathHash) {
		}

		EntryPathSpec(const std::filesystem::path& fullPath)
			: Original(fullPath.lexically_normal())
			, PathHash(SqexHash(Original.parent_path()))
			, NameHash(SqexHash(Original.filename()))
			, FullPathHash(SqexHash(Original)) {
		}

		template<class Elem, class Traits = std::char_traits<Elem>, class Alloc = std::allocator<Elem>>
		EntryPathSpec(const std::basic_string<Elem, Traits, Alloc>& fullPath)
			: Original(std::filesystem::path(fullPath).lexically_normal())
			, PathHash(SqexHash(Original.parent_path()))
			, NameHash(SqexHash(Original.filename()))
			, FullPathHash(SqexHash(Original)) {
		}

		EntryPathSpec(const std::filesystem::path& path, const std::filesystem::path& name)
			: Original((path / name).lexically_normal())
			, PathHash(SqexHash(path))
			, NameHash(SqexHash(name))
			, FullPathHash(SqexHash(Original)) {
		}

		EntryPathSpec& operator=(const std::filesystem::path& fullPath) {
			Original = fullPath.lexically_normal();
			PathHash = SqexHash(Original.parent_path());
			NameHash = SqexHash(Original.filename());
			FullPathHash = SqexHash(Original);
			return *this;
		}

		template<class Elem, class Traits = std::char_traits<Elem>, class Alloc = std::allocator<Elem>>
		EntryPathSpec& operator=(const std::basic_string<Elem, Traits, Alloc>& fullPath) {
			Original = std::filesystem::path(fullPath).lexically_normal();
			PathHash = SqexHash(Original.parent_path());
			NameHash = SqexHash(Original.filename());
			FullPathHash = SqexHash(Original);
			return *this;
		}

		EntryPathSpec& operator=(uint32_t fullPathHash) {
			Original.clear();
			PathHash = EmptyHashValue;
			NameHash = EmptyHashValue;
			FullPathHash = fullPathHash;
			return *this;
		}

		[[nodiscard]] bool empty() const {
			return PathHash == EmptyHashValue && NameHash == EmptyHashValue && FullPathHash == EmptyHashValue;
		}

		bool operator==(const EntryPathSpec& r) const {
			return (PathHash == r.PathHash && NameHash == r.NameHash && (NameHash != EmptyHashValue || PathHash != EmptyHashValue))
				|| (FullPathHash == r.FullPathHash && FullPathHash != EmptyHashValue)
				|| (empty() && r.empty());
		}

		bool operator!=(const EntryPathSpec& r) const {
			return !this->operator==(r);
		}
	};

	class EntryProvider : public RandomAccessStream {
	public:
		const EntryPathSpec PathSpec;

		EntryProvider(EntryPathSpec pathSpec)
			: PathSpec(std::move(pathSpec)) {
		}

		[[nodiscard]] virtual SqData::FileEntryType EntryType() const = 0;
	};

	class EmptyEntryProvider : public EntryProvider {
	public:
		using EntryProvider::EntryProvider;

		[[nodiscard]] uint32_t StreamSize() const override {
			return sizeof SqData::FileEntryHeader;
		}

		size_t ReadStreamPartial(uint64_t offset, void* buf, size_t length) const override;

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

		[[nodiscard]] uint32_t StreamSize() const override;

		size_t ReadStreamPartial(uint64_t offset, void* buf, size_t length) const override;

		[[nodiscard]] SqData::FileEntryType EntryType() const override {
			return SqData::FileEntryType::Binary;
		}
	};

	class MemoryBinaryEntryProvider : public EntryProvider {
		std::vector<char> m_data;

	public:
		MemoryBinaryEntryProvider(EntryPathSpec, const std::filesystem::path& path);

		[[nodiscard]] uint32_t StreamSize() const override {
			return static_cast<uint32_t>(m_data.size());
		}

		size_t ReadStreamPartial(uint64_t offset, void* buf, size_t length) const override;

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
		[[nodiscard]] uint32_t StreamSize() const override {
			return AlignEntry();

		}
		size_t ReadStreamPartial(uint64_t offset, void* buf, size_t length) const override;

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

		[[nodiscard]] uint32_t StreamSize() const override {
			return static_cast<uint32_t>(m_size);
		}

		size_t ReadStreamPartial(uint64_t offset, void* buf, size_t length) const override;

		[[nodiscard]] SqData::FileEntryType EntryType() const override {
			return SqData::FileEntryType::Texture;
		}
	};

	class PartialFileViewEntryProvider : public EntryProvider {
		const Utils::Win32::File m_file;
		const uint64_t m_offset;
		const uint32_t m_size;

		mutable bool m_entryTypeFetched = false;
		mutable SqData::FileEntryType m_entryType = SqData::FileEntryType::Empty;

	public:
		PartialFileViewEntryProvider(EntryPathSpec pathSpec, Utils::Win32::File file, uint64_t offset, uint32_t length)
			: EntryProvider(std::move(pathSpec))
			, m_file(std::move(file))
			, m_offset(offset)
			, m_size(length) {
		}

		[[nodiscard]] uint32_t StreamSize() const override {
			return m_size;
		}

		size_t ReadStreamPartial(uint64_t offset, void* buf, size_t length) const override {
			if (offset >= m_size)
				return 0;

			return m_file.Read(m_offset + offset, buf, std::min(length, static_cast<size_t>(m_size - offset)));
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
