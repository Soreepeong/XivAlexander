#pragma once

#include "Sqex_Sqpack_LazyEntryProvider.h"

namespace Sqex::Sqpack {
	class OnTheFlyBinaryEntryProvider : public LazyFileOpeningEntryProvider {
		const std::vector<uint8_t> m_header;

		uint32_t m_padBeforeData = 0;

	public:
		OnTheFlyBinaryEntryProvider(EntryPathSpec, std::filesystem::path, bool openImmediately = false, int compressionLevel = Z_BEST_COMPRESSION);
		OnTheFlyBinaryEntryProvider(EntryPathSpec, std::shared_ptr<const RandomAccessStream>, int compressionLevel = Z_BEST_COMPRESSION);

		using LazyFileOpeningEntryProvider::StreamSize;
		using LazyFileOpeningEntryProvider::ReadStreamPartial;

		[[nodiscard]] SqData::FileEntryType EntryType() const override { return SqData::FileEntryType::Binary; }

		std::string DescribeState() const override { return "OnTheFlyBinaryEntryProvider"; }

	protected:
		[[nodiscard]] uint64_t MaxPossibleStreamSize() const override;
		[[nodiscard]] uint64_t StreamSize(const RandomAccessStream& stream) const override { return MaxPossibleStreamSize(); }
		uint64_t ReadStreamPartial(const RandomAccessStream& stream, uint64_t offset, void* buf, uint64_t length) const override;
	};

	class MemoryBinaryEntryProvider : public LazyFileOpeningEntryProvider {
		std::vector<char> m_data;

	public:
		using LazyFileOpeningEntryProvider::LazyFileOpeningEntryProvider;
		using LazyFileOpeningEntryProvider::StreamSize;
		using LazyFileOpeningEntryProvider::ReadStreamPartial;

		[[nodiscard]] SqData::FileEntryType EntryType() const override { return SqData::FileEntryType::Binary; }

		std::string DescribeState() const override { return "MemoryBinaryEntryProvider"; }

	protected:
		void Initialize(const RandomAccessStream& stream) override;
		[[nodiscard]] uint64_t StreamSize(const RandomAccessStream& stream) const override { return static_cast<uint32_t>(m_data.size()); }
		uint64_t ReadStreamPartial(const RandomAccessStream& stream, uint64_t offset, void* buf, uint64_t length) const override;
	};
}
