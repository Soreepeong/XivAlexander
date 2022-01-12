#pragma once

#include "XivAlexanderCommon/Sqex/Sqpack/LazyEntryProvider.h"

namespace Sqex::Texture {
	struct Header;
}

namespace Sqex::Sqpack {
	class OnTheFlyTextureEntryProvider : public LazyFileOpeningEntryProvider {
		static constexpr auto MaxMipmapCountPerTexture = 16;
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
		std::vector<SqData::TextureBlockHeaderLocator> m_blockLocators;
		std::vector<uint16_t> m_subBlockSizes;
		std::vector<uint8_t> m_texHeaderBytes;

		std::vector<uint8_t> m_mergedHeader;

		std::vector<uint32_t> m_mipmapSizes;
		size_t m_size = 0;

	public:
		using LazyFileOpeningEntryProvider::LazyFileOpeningEntryProvider;
		using LazyFileOpeningEntryProvider::StreamSize;
		using LazyFileOpeningEntryProvider::ReadStreamPartial;

		[[nodiscard]] SqData::FileEntryType EntryType() const override { return SqData::FileEntryType::Texture; }

		std::string DescribeState() const override { return "OnTheFlyTextureEntryProvider"; }

	protected:
		void Initialize(const RandomAccessStream&) override;
		[[nodiscard]] uint64_t MaxPossibleStreamSize() const override;
		[[nodiscard]] uint64_t StreamSize(const RandomAccessStream&) const override { return static_cast<uint32_t>(m_size); }
		uint64_t ReadStreamPartial(const RandomAccessStream&, uint64_t offset, void* buf, uint64_t length) const override;
	};

	class MemoryTextureEntryProvider : public LazyFileOpeningEntryProvider {
		std::vector<uint8_t> m_data;

	public:
		using LazyFileOpeningEntryProvider::LazyFileOpeningEntryProvider;
		using LazyFileOpeningEntryProvider::StreamSize;
		using LazyFileOpeningEntryProvider::ReadStreamPartial;

		[[nodiscard]] SqData::FileEntryType EntryType() const override { return SqData::FileEntryType::Binary; }

		std::string DescribeState() const override { return "MemoryTextureEntryProvider"; }

	protected:
		void Initialize(const RandomAccessStream& stream) override;
		[[nodiscard]] uint64_t StreamSize(const RandomAccessStream& stream) const override { return static_cast<uint32_t>(m_data.size()); }
		uint64_t ReadStreamPartial(const RandomAccessStream& stream, uint64_t offset, void* buf, uint64_t length) const override;
	};
}
