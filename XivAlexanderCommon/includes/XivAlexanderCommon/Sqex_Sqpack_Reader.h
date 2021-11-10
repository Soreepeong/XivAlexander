#pragma once

#include "Sqex_Sqpack.h"
#include "Utils_Win32_Handle.h"
#include "Sqex_Sqpack_EntryProvider.h"

namespace Sqex::Sqpack {
	struct Reader {
		template<typename HashLocatorT, typename TextLocatorT> 
		struct SqIndexType {
			const std::vector<uint8_t> Data;
			const SqpackHeader& Header{};
			const SqIndex::Header& IndexHeader{};
			const std::span<const HashLocatorT> HashLocators;
			const std::span<const TextLocatorT> TextLocators;
			const std::span<const SqIndex::Segment3Entry> Segment3;

			const SqIndex::LEDataLocator& GetLocatorFromTextLocators(const char* fullPath) const {
				const auto it = std::lower_bound(TextLocators.begin(), TextLocators.end(), fullPath, PathSpecComparator());
				if (it == TextLocators.end() || _strcmpi(it->FullPath, fullPath) != 0)
					throw std::out_of_range(std::format("Entry {} not found", fullPath));
				return it->Locator;
			}

		protected:
			friend struct Reader;
			SqIndexType(const Win32::Handle& hFile, bool strictVerify);
		};

		struct SqIndex1Type : SqIndexType<SqIndex::PairHashLocator, SqIndex::PairHashWithTextLocator> {
			const std::span<const SqIndex::PathHashLocator> PathHashLocators;

			std::span<const SqIndex::PairHashLocator> GetPairHashLocators(uint32_t pathHash) const;
			const SqIndex::LEDataLocator& GetLocator(uint32_t pathHash, uint32_t nameHash) const;

		protected:
			friend struct Reader;
			SqIndex1Type(const Win32::Handle& hFile, bool strictVerify);
		};

		struct SqIndex2Type : SqIndexType<SqIndex::FullHashLocator, SqIndex::FullHashWithTextLocator> {
			const SqIndex::LEDataLocator& GetLocator(uint32_t fullPathHash) const;

		protected:
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

		struct EntryInfoType {
			EntryPathSpec PathSpec;
			uint64_t Allocation;
		};

		SqIndex1Type Index1;
		SqIndex2Type Index2;
		std::vector<SqDataType> Data;
		std::vector<std::pair<SqIndex::LEDataLocator, EntryInfoType>> EntryInfo;

		Reader(const std::filesystem::path& indexFile, bool strictVerify = false);

		[[nodiscard]] const SqIndex::LEDataLocator& GetLocator(const EntryPathSpec& pathSpec) const;
		[[nodiscard]] std::shared_ptr<EntryProvider> GetEntryProvider(const EntryPathSpec& pathSpec, SqIndex::LEDataLocator locator, uint64_t allocation) const;
		[[nodiscard]] std::shared_ptr<EntryProvider> GetEntryProvider(const EntryPathSpec& pathSpec) const;
		[[nodiscard]] std::shared_ptr<Sqex::RandomAccessStream> GetFile(const EntryPathSpec& pathSpec) const;
		std::shared_ptr<RandomAccessStream> operator[](const EntryPathSpec& pathSpec) const;
	};
}
