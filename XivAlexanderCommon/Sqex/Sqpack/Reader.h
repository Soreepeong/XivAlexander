#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <ranges>

#include "XivAlexanderCommon/Sqex/Sqpack.h"
#include "EntryProvider.h"
#include "RandomAccessStreamAsEntryProviderView.h"

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

			SqIndexType(const RandomAccessStream& stream, bool strictVerify)
				: Data(stream.ReadStreamIntoVector<uint8_t>(0))
				, Header(*reinterpret_cast<const SqpackHeader*>(&Data[0]))
				, IndexHeader(*reinterpret_cast<const SqIndex::Header*>(&Data[Header.HeaderSize]))
				, HashLocators(span_cast<HashLocatorT>(Data, IndexHeader.HashLocatorSegment.Offset, IndexHeader.HashLocatorSegment.Size, 1))
				, TextLocators(span_cast<TextLocatorT>(Data, IndexHeader.TextLocatorSegment.Offset, IndexHeader.TextLocatorSegment.Size, 1))
				, Segment3(span_cast<SqIndex::Segment3Entry>(Data, IndexHeader.UnknownSegment3.Offset, IndexHeader.UnknownSegment3.Size, 1)) {
				if (strictVerify) {
					Header.VerifySqpackHeader(SqpackType::SqIndex);
					IndexHeader.VerifySqpackIndexHeader(SqIndex::Header::IndexType::Index);
					if (IndexHeader.HashLocatorSegment.Size % sizeof HashLocatorT)
						throw CorruptDataException("HashLocators has an invalid size alignment");
					if (IndexHeader.TextLocatorSegment.Size % sizeof TextLocatorT)
						throw CorruptDataException("TextLocators has an invalid size alignment");
					if (IndexHeader.UnknownSegment3.Size % sizeof SqIndex::Segment3Entry)
						throw CorruptDataException("Segment3 has an invalid size alignment");
					IndexHeader.HashLocatorSegment.Sha1.Verify(HashLocators, "HashLocatorSegment has invalid data SHA-1");
					IndexHeader.TextLocatorSegment.Sha1.Verify(TextLocators, "TextLocatorSegment has invalid data SHA-1");
					IndexHeader.UnknownSegment3.Sha1.Verify(Segment3, "UnknownSegment3 has invalid data SHA-1");
				}
			}
		};

		struct SqIndex1Type : SqIndexType<SqIndex::PairHashLocator, SqIndex::PairHashWithTextLocator> {
			const std::span<const SqIndex::PathHashLocator> PathHashLocators;

			std::span<const SqIndex::PairHashLocator> GetPairHashLocators(uint32_t pathHash) const {
				const auto it = std::lower_bound(PathHashLocators.begin(), PathHashLocators.end(), pathHash, PathSpecComparator());
				if (it == PathHashLocators.end() || it->PathHash != pathHash)
					throw std::out_of_range(std::format("PathHash {:08x} not found", pathHash));

				return span_cast<SqIndex::PairHashLocator>(Data, it->PairHashLocatorOffset, it->PairHashLocatorSize, 1);
			}

			const SqIndex::LEDataLocator& GetLocator(uint32_t pathHash, uint32_t nameHash) const {
				const auto locators = GetPairHashLocators(pathHash);
				const auto it = std::lower_bound(locators.begin(), locators.end(), nameHash, PathSpecComparator());
				if (it == locators.end() || it->NameHash != nameHash)
					throw std::out_of_range(std::format("NameHash {:08x} in PathHash {:08x} not found", nameHash, pathHash));
				return it->Locator;
			}

		protected:
			friend struct Reader;

			SqIndex1Type(const RandomAccessStream& stream, bool strictVerify)
				: SqIndexType<SqIndex::PairHashLocator, SqIndex::PairHashWithTextLocator>(stream, strictVerify)
				, PathHashLocators(span_cast<SqIndex::PathHashLocator>(Data, IndexHeader.PathHashLocatorSegment.Offset, IndexHeader.PathHashLocatorSegment.Size, 1)) {
				if (strictVerify) {
					if (IndexHeader.PathHashLocatorSegment.Size % sizeof SqIndex::PathHashLocator)
						throw CorruptDataException("PathHashLocators has an invalid size alignment");
					IndexHeader.PathHashLocatorSegment.Sha1.Verify(PathHashLocators, "PathHashLocatorSegment has invalid data SHA-1");
				}
			}
		};

		struct SqIndex2Type : SqIndexType<SqIndex::FullHashLocator, SqIndex::FullHashWithTextLocator> {

			const SqIndex::LEDataLocator& GetLocator(uint32_t fullPathHash) const {
				const auto it = std::lower_bound(HashLocators.begin(), HashLocators.end(), fullPathHash, PathSpecComparator());
				if (it == HashLocators.end() || it->FullPathHash != fullPathHash)
					throw std::out_of_range(std::format("FullPathHash {:08x} not found", fullPathHash));
				return it->Locator;
			}

		protected:
			friend struct Reader;

			SqIndex2Type(const RandomAccessStream& stream, bool strictVerify)
				: SqIndexType<SqIndex::FullHashLocator, SqIndex::FullHashWithTextLocator>(stream, strictVerify) {
			}
		};

		struct SqDataType {
			SqpackHeader Header{};
			SqData::Header DataHeader{};
			std::shared_ptr<RandomAccessStream> Stream;

		private:
			friend struct Reader;

			SqDataType(std::shared_ptr<RandomAccessStream> stream, const uint32_t datIndex, bool strictVerify)
				: Stream(std::move(stream)) {

				// The following line loads both Header and DataHeader as they are adjacent to each other
				Stream->ReadStream(0, &Header, sizeof Header + sizeof DataHeader);
				if (strictVerify) {
					if (datIndex == 0) {
						Header.VerifySqpackHeader(SqpackType::SqData);
						DataHeader.Verify(datIndex + 1);
					}
				}

				if (strictVerify) {
					const auto dataFileLength = Stream->StreamSize();
					if (datIndex == 0) {
						if (dataFileLength != 0ULL + Header.HeaderSize + DataHeader.HeaderSize + DataHeader.DataSize)
							throw CorruptDataException("Invalid file size");
					}
				}
			}
		};

		struct EntryInfoType {
			EntryPathSpec PathSpec;
			uint64_t Allocation;
		};

		SqIndex1Type Index1;
		SqIndex2Type Index2;
		std::vector<SqDataType> Data;
		std::vector<std::pair<SqIndex::LEDataLocator, EntryInfoType>> EntryInfo;

		Reader(const std::string& fileName, std::shared_ptr<RandomAccessStream> indexStream1, std::shared_ptr<RandomAccessStream> indexStream2, std::vector<std::shared_ptr<RandomAccessStream>> dataStreams, bool strictVerify = false)
			: Index1(*indexStream1, strictVerify)
			, Index2(*indexStream2, strictVerify) {

			const auto categoryId = std::strtol(fileName.substr(0, 2).c_str(), nullptr, 16);
			const auto expacId = std::strtol(fileName.substr(2, 2).c_str(), nullptr, 16);
			const auto partId = std::strtol(fileName.substr(4, 2).c_str(), nullptr, 16);

			std::vector<std::pair<SqIndex::LEDataLocator, std::tuple<uint32_t, uint32_t, const char*>>> offsets1;
			offsets1.reserve(
				std::max(Index1.HashLocators.size() + Index1.TextLocators.size(), Index2.HashLocators.size() + Index2.TextLocators.size())
				+ Index1.IndexHeader.TextLocatorSegment.Count
			);
			for (const auto& item : Index1.HashLocators)
				if (!item.Locator.IsSynonym)
					offsets1.emplace_back(item.Locator, std::make_tuple(item.PathHash, item.NameHash, static_cast<const char*>(nullptr)));
			for (const auto& item : Index1.TextLocators)
				offsets1.emplace_back(item.Locator, std::make_tuple(item.PathHash, item.NameHash, item.FullPath));

			std::vector<std::pair<SqIndex::LEDataLocator, std::tuple<uint32_t, const char*>>> offsets2;
			for (const auto& item : Index2.HashLocators)
				if (!item.Locator.IsSynonym)
					offsets2.emplace_back(item.Locator, std::make_tuple(item.FullPathHash, static_cast<const char*>(nullptr)));
			for (const auto& item : Index2.TextLocators)
				offsets2.emplace_back(item.Locator, std::make_tuple(item.FullPathHash, item.FullPath));

			if (offsets1.size() != offsets2.size())
				throw CorruptDataException(".index and .index2 do not have the same number of files contained");

			Data.reserve(Index1.IndexHeader.TextLocatorSegment.Count);
			for (uint32_t i = 0; i < Index1.IndexHeader.TextLocatorSegment.Count; ++i) {
				Data.emplace_back(SqDataType{
					dataStreams[i],
					i,
					strictVerify,
					});
				offsets1.emplace_back(SqIndex::LEDataLocator(i, Data[i].Stream->StreamSize()), std::make_tuple(UINT32_MAX, UINT32_MAX, static_cast<const char*>(nullptr)));
				offsets2.emplace_back(SqIndex::LEDataLocator(i, Data[i].Stream->StreamSize()), std::make_tuple(UINT32_MAX, static_cast<const char*>(nullptr)));
			}

			struct Comparator {
				bool operator()(const SqIndex::LEDataLocator& l, const SqIndex::LEDataLocator& r) const {
					if (l.DatFileIndex != r.DatFileIndex)
						return l.DatFileIndex < r.DatFileIndex;
					if (l.DatFileOffset() != r.DatFileOffset())
						return l.DatFileOffset() < r.DatFileOffset();
					return false;
				}

				bool operator()(const std::pair<SqIndex::LEDataLocator, std::tuple<uint32_t, uint32_t, const char*>>& l, const std::pair<SqIndex::LEDataLocator, std::tuple<uint32_t, uint32_t, const char*>>& r) const {
					return (*this)(l.first, r.first);
				}

				bool operator()(const std::pair<SqIndex::LEDataLocator, std::tuple<uint32_t, const char*>>& l, const std::pair<SqIndex::LEDataLocator, std::tuple<uint32_t, const char*>>& r) const {
					return (*this)(l.first, r.first);
				}

				bool operator()(const std::pair<SqIndex::LEDataLocator, EntryInfoType>& l, const std::pair<SqIndex::LEDataLocator, EntryInfoType>& r) const {
					return l.first < r.first;
				}
			};

			std::sort(offsets1.begin(), offsets1.end(), Comparator());
			std::sort(offsets2.begin(), offsets2.end(), Comparator());
			EntryInfo.reserve(offsets1.size());

			if (strictVerify) {
				for (size_t i = 0; i < offsets1.size(); ++i) {
					if (offsets1[i].first != offsets2[i].first)
						throw CorruptDataException(".index and .index2 have items with different locators");
					if (offsets1[i].first.IsSynonym)
						throw CorruptDataException("Synonym remains after conflict resolution");
				}
			}

			EntryPathSpec pathSpec;
			for (size_t curr = 1, prev = 0; curr < offsets1.size(); ++curr, ++prev) {

				// Skip dummy items to mark end of individual .dat file.
				if (offsets1[prev].first.DatFileIndex != offsets1[curr].first.DatFileIndex)
					continue;

				EntryInfo.emplace_back(offsets1[prev].first, EntryInfoType{ .Allocation = offsets1[curr].first.DatFileOffset() - offsets1[prev].first.DatFileOffset() });
				if (std::get<2>(offsets1[prev].second))
					EntryInfo.back().second.PathSpec = EntryPathSpec(std::get<2>(offsets1[prev].second));
				else if (std::get<1>(offsets2[prev].second))
					EntryInfo.back().second.PathSpec = EntryPathSpec(std::get<1>(offsets2[prev].second));
				else
					EntryInfo.back().second.PathSpec = EntryPathSpec(
						std::get<0>(offsets1[prev].second),
						std::get<1>(offsets1[prev].second),
						std::get<0>(offsets2[prev].second),
						categoryId,
						expacId,
						partId);
			}

			std::sort(EntryInfo.begin(), EntryInfo.end(), Comparator());
		}

		static Reader FromPath(const std::filesystem::path& indexFile, bool strictVerify = false) {
			std::vector<std::shared_ptr<RandomAccessStream>> dataStreams;
			for (int i = 0; i < 8; ++i) {
				auto dataPath = std::filesystem::path(indexFile);
				dataPath.replace_extension(std::format(".dat{}", i));
				if (!exists(dataPath))
					break;
				dataStreams.emplace_back(std::make_shared<FileRandomAccessStream>(dataPath));
			}

			return Reader(indexFile.filename().string(),
				std::make_shared<FileRandomAccessStream>(std::filesystem::path(indexFile).replace_extension(".index")),
				std::make_shared<FileRandomAccessStream>(std::filesystem::path(indexFile).replace_extension(".index2")),
				std::move(dataStreams),
				strictVerify);
		}

		[[nodiscard]] const SqIndex::LEDataLocator& GetLocator1(const EntryPathSpec& pathSpec) const {
			try {
				const auto& locator = Index1.GetLocator(pathSpec.PathHash(), pathSpec.NameHash());
				if (locator.IsSynonym)
					return Index1.GetLocatorFromTextLocators(pathSpec.Path().c_str());
				return locator;

			} catch (const std::out_of_range& e) {
				throw std::out_of_range(std::format("Failed to find {}: {}", pathSpec, e.what()));
			}
			throw std::out_of_range(std::format("Path spec is empty"));
		}

		[[nodiscard]] const SqIndex::LEDataLocator& GetLocator2(const EntryPathSpec& pathSpec) const {
			try {
				const auto& locator = Index2.GetLocator(pathSpec.FullPathHash());
				if (locator.IsSynonym)
					return Index2.GetLocatorFromTextLocators(pathSpec.Path().c_str());
				return locator;

			} catch (const std::out_of_range& e) {
				throw std::out_of_range(std::format("Failed to find {}: {}", pathSpec, e.what()));
			}
			throw std::out_of_range(std::format("Path spec is empty"));
		}

		[[nodiscard]] std::unique_ptr<EntryProvider> GetEntryProvider(const EntryPathSpec& pathSpec, SqIndex::LEDataLocator locator, uint64_t allocation) const {
			return std::make_unique<RandomAccessStreamAsEntryProviderView>(pathSpec, Data.at(locator.DatFileIndex).Stream, locator.DatFileOffset(), allocation);
		}

		[[nodiscard]] std::unique_ptr<EntryProvider> GetEntryProvider(const EntryPathSpec& pathSpec) const {
			struct Comparator {
				bool operator()(const std::pair<SqIndex::LEDataLocator, EntryInfoType>& l, const SqIndex::LEDataLocator& r) const {
					return l.first < r;
				}

				bool operator()(const SqIndex::LEDataLocator& l, const std::pair<SqIndex::LEDataLocator, EntryInfoType>& r) const {
					return l < r.first;
				}
			};

			const auto& locator = GetLocator1(pathSpec);
			const auto entryInfo = std::lower_bound(EntryInfo.begin(), EntryInfo.end(), locator, Comparator());
			return GetEntryProvider(pathSpec, locator, entryInfo->second.Allocation);
		}
	};

	class GameReader {
		const std::filesystem::path m_gamePath;
		mutable std::mutex m_readersMtx;
		mutable std::map<uint32_t, std::optional<Reader>> m_readers;

	public:
		GameReader(std::filesystem::path gamePath)
			: m_gamePath(std::move(gamePath)) {
			for (const auto& iter : std::filesystem::recursive_directory_iterator(m_gamePath / "sqpack")) {
				if (iter.is_directory() || !iter.path().wstring().ends_with(L".win32.index"))
					continue;

				auto packFileName = std::filesystem::path{ iter.path() }.replace_extension("").replace_extension("").string();
				if (packFileName.size() < 6)
					continue;

				packFileName.resize(6);

				const auto packFileId = std::strtol(&packFileName[0], nullptr, 16);
				m_readers.emplace(packFileId, nullptr);
			}
		}

		[[nodiscard]] std::unique_ptr<EntryProvider> GetEntryProvider(const EntryPathSpec& pathSpec) const {
			return GetSqpackReader(pathSpec).GetEntryProvider(pathSpec);
		}

		[[nodiscard]] const Reader& GetSqpackReader(const EntryPathSpec& rawPathSpec) const {
			return GetSqpackReader(rawPathSpec.PackNameValue());
		}

		[[nodiscard]] const Reader& GetSqpackReader(uint32_t packId) const {
			auto& item = m_readers[packId];
			if (item)
				return *item;

			const auto lock = std::lock_guard(m_readersMtx);
			if (item)
				return *item;

			const auto categoryId = packId >> 16;
			if (categoryId == 0)
				return item.emplace(m_gamePath / std::format("sqpack/ffxiv/{:0>6x}.win32.index", packId));
			else
				return item.emplace(m_gamePath / std::format("sqpack/ex{}/{:0>6x}.win32.index", categoryId, packId));
		}

		void PreloadAllSqpackFiles() const {
			const auto lock = std::lock_guard(m_readersMtx);
			for (const auto& key : m_readers | std::views::keys)
				void(GetSqpackReader(key));
		}
	};
}
