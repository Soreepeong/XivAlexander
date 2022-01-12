#pragma once

#include "XivAlexanderCommon/Sqex/Sqpack/LazyEntryProvider.h"

namespace Sqex::Sqpack {
	class OnTheFlyModelEntryProvider : public LazyFileOpeningEntryProvider {
		struct ModelEntryHeader {
			SqData::FileEntryHeader Entry;
			SqData::ModelBlockLocator Model;
		} m_header{};
		std::vector<uint32_t> m_blockOffsets;
		std::vector<uint16_t> m_blockDataSizes;
		std::vector<uint16_t> m_paddedBlockSizes;
		std::vector<uint32_t> m_actualFileOffsets;

	public:
		using LazyFileOpeningEntryProvider::LazyFileOpeningEntryProvider;
		using LazyFileOpeningEntryProvider::StreamSize;
		using LazyFileOpeningEntryProvider::ReadStreamPartial;

		[[nodiscard]] SqData::FileEntryType EntryType() const override { return SqData::FileEntryType::Model; }

		std::string DescribeState() const override { return "OnTheFlyModelEntryProvider"; }

	protected:
		void Initialize(const RandomAccessStream&) override;
		[[nodiscard]] uint64_t MaxPossibleStreamSize() const override;
		[[nodiscard]] uint64_t StreamSize(const RandomAccessStream&) const override { return MaxPossibleStreamSize(); }
		uint64_t ReadStreamPartial(const RandomAccessStream&, uint64_t offset, void* buf, uint64_t length) const override;
	};

	class MemoryModelEntryProvider : public LazyFileOpeningEntryProvider {
		std::vector<uint8_t> m_data;

	public:
		using LazyFileOpeningEntryProvider::LazyFileOpeningEntryProvider;
		using LazyFileOpeningEntryProvider::StreamSize;
		using LazyFileOpeningEntryProvider::ReadStreamPartial;

		[[nodiscard]] SqData::FileEntryType EntryType() const override { return SqData::FileEntryType::Model; }

		std::string DescribeState() const override { return "MemoryModelEntryProvider"; }

	protected:
		void Initialize(const RandomAccessStream& stream) override;
		[[nodiscard]] uint64_t StreamSize(const RandomAccessStream& stream) const override { return static_cast<uint32_t>(m_data.size()); }
		uint64_t ReadStreamPartial(const RandomAccessStream& stream, uint64_t offset, void* buf, uint64_t length) const override;
	};

}
