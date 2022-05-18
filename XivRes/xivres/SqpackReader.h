#ifndef _XIVRES_SQPACKREADER_H_
#define _XIVRES_SQPACKREADER_H_

#include <map>
#include <mutex>
#include <optional>
#include <ranges>

#include "PackedFileUnpackingStream.h"
#include "Sqpack.h"
#include "StreamAsPackedFileViewStream.h"

namespace XivRes {
	class SqpackReader {
	public:
		template<typename HashLocatorT, typename TextLocatorT> 
		class SqIndexType {
		public:
			const std::vector<uint8_t> Data;

			const SqpackHeader& Header() const {
				return *reinterpret_cast<const SqpackHeader*>(&Data[0]);
			}

			const SqpackIndexHeader& IndexHeader() const {
				return *reinterpret_cast<const SqpackIndexHeader*>(&Data[Header().HeaderSize]);
			}

			std::span<const HashLocatorT> HashLocators() const {
				return Internal::span_cast<HashLocatorT>(Data, IndexHeader().HashLocatorSegment.Offset, IndexHeader().HashLocatorSegment.Size, 1);
			}

			std::span<const TextLocatorT> TextLocators() const {
				return Internal::span_cast<TextLocatorT>(Data, IndexHeader().TextLocatorSegment.Offset, IndexHeader().TextLocatorSegment.Size, 1);
			}

			std::span<const SqpackSegment3Entry> Segment3() const {
				return Internal::span_cast<SqpackSegment3Entry>(Data, IndexHeader().UnknownSegment3.Offset, IndexHeader().UnknownSegment3.Size, 1);
			}

			const SqpackDataLocator& GetLocatorFromTextLocators(const char* fullPath) const {
				const auto it = std::lower_bound(TextLocators().begin(), TextLocators().end(), fullPath, SqpackPathSpec::LocatorComparator());
				if (it == TextLocators().end() || _strcmpi(it->FullPath, fullPath) != 0)
					throw std::out_of_range(std::format("Entry {} not found", fullPath));
				return it->Locator;
			}

		protected:
			friend class SqpackReader;

			SqIndexType(const IStream& stream, bool strictVerify)
				: Data(ReadStreamIntoVector<uint8_t>(stream)) {

				if (strictVerify) {
					Header().VerifySqpackHeader(SqpackType::SqIndex);
					IndexHeader().VerifySqpackIndexHeader(SqpackIndexHeader::IndexType::Index);
					if (IndexHeader().HashLocatorSegment.Size % sizeof HashLocatorT)
						throw CorruptDataException("HashLocators has an invalid size alignment");
					if (IndexHeader().TextLocatorSegment.Size % sizeof TextLocatorT)
						throw CorruptDataException("TextLocators has an invalid size alignment");
					if (IndexHeader().UnknownSegment3.Size % sizeof SqpackSegment3Entry)
						throw CorruptDataException("Segment3 has an invalid size alignment");
					IndexHeader().HashLocatorSegment.Sha1.Verify(HashLocators(), "HashLocatorSegment has invalid data SHA-1");
					IndexHeader().TextLocatorSegment.Sha1.Verify(TextLocators(), "TextLocatorSegment has invalid data SHA-1");
					IndexHeader().UnknownSegment3.Sha1.Verify(Segment3(), "UnknownSegment3 has invalid data SHA-1");
				}
			}
		};

		class SqIndex1Type : public SqIndexType<SqpackPairHashLocator, SqpackPairHashWithTextLocator> {
		public:
			const std::span<const SqpackPathHashLocator> PathHashLocators() const {
				return Internal::span_cast<SqpackPathHashLocator>(Data, IndexHeader().PathHashLocatorSegment.Offset, IndexHeader().PathHashLocatorSegment.Size, 1);
			}

			std::span<const SqpackPairHashLocator> GetPairHashLocators(uint32_t pathHash) const {
				const auto it = std::lower_bound(PathHashLocators().begin(), PathHashLocators().end(), pathHash, SqpackPathSpec::LocatorComparator());
				if (it == PathHashLocators().end() || it->PathHash != pathHash)
					throw std::out_of_range(std::format("PathHash {:08x} not found", pathHash));

				return Internal::span_cast<SqpackPairHashLocator>(Data, it->PairHashLocatorOffset, it->PairHashLocatorSize, 1);
			}

			const SqpackDataLocator& GetLocator(uint32_t pathHash, uint32_t nameHash) const {
				const auto locators = GetPairHashLocators(pathHash);
				const auto it = std::lower_bound(locators.begin(), locators.end(), nameHash, SqpackPathSpec::LocatorComparator());
				if (it == locators.end() || it->NameHash != nameHash)
					throw std::out_of_range(std::format("NameHash {:08x} in PathHash {:08x} not found", nameHash, pathHash));
				return it->Locator;
			}

		protected:
			friend class SqpackReader;

			SqIndex1Type(const IStream& stream, bool strictVerify)
				: SqIndexType<SqpackPairHashLocator, SqpackPairHashWithTextLocator>(stream, strictVerify)  {
				if (strictVerify) {
					if (IndexHeader().PathHashLocatorSegment.Size % sizeof SqpackPathHashLocator)
						throw CorruptDataException("PathHashLocators has an invalid size alignment");
					IndexHeader().PathHashLocatorSegment.Sha1.Verify(PathHashLocators(), "PathHashLocatorSegment has invalid data SHA-1");
				}
			}
		};

		class SqIndex2Type : public SqIndexType<SqpackFullHashLocator, SqpackFullHashWithTextLocator> {
		public:
			const SqpackDataLocator& GetLocator(uint32_t fullPathHash) const {
				const auto it = std::lower_bound(HashLocators().begin(), HashLocators().end(), fullPathHash, SqpackPathSpec::LocatorComparator());
				if (it == HashLocators().end() || it->FullPathHash != fullPathHash)
					throw std::out_of_range(std::format("FullPathHash {:08x} not found", fullPathHash));
				return it->Locator;
			}

		protected:
			friend class SqpackReader;

			SqIndex2Type(const IStream& stream, bool strictVerify)
				: SqIndexType<SqpackFullHashLocator, SqpackFullHashWithTextLocator>(stream, strictVerify) {
			}
		};

		struct SqDataType {
			SqpackHeader Header{};
			SqpackDataHeader DataHeader{};
			std::shared_ptr<IStream> Stream;

		private:
			friend class SqpackReader;

			SqDataType(std::shared_ptr<IStream> stream, const uint32_t datIndex, bool strictVerify)
				: Stream(std::move(stream)) {

				// The following line loads both Header and DataHeader as they are adjacent to each other
				ReadStream(*Stream, 0, &Header, sizeof Header + sizeof DataHeader);
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
			SqpackDataLocator Locator;
			SqpackPathSpec PathSpec;
			uint64_t Allocation;
		};

		SqIndex1Type Index1;
		SqIndex2Type Index2;
		std::vector<SqDataType> Data;
		std::vector<EntryInfoType> EntryInfo;

		uint8_t CategoryId;
		uint8_t ExpacId;
		uint8_t PartId;

		SqpackReader(const std::string& fileName, std::shared_ptr<IStream> indexStream1, std::shared_ptr<IStream> indexStream2, std::vector<std::shared_ptr<IStream>> dataStreams, bool strictVerify = false)
			: Index1(*indexStream1, strictVerify)
			, Index2(*indexStream2, strictVerify)
			, CategoryId(static_cast<uint8_t>(std::strtol(fileName.substr(0, 2).c_str(), nullptr, 16)))
			, ExpacId(static_cast<uint8_t>(std::strtol(fileName.substr(2, 2).c_str(), nullptr, 16)))
			, PartId(static_cast<uint8_t>(std::strtol(fileName.substr(4, 2).c_str(), nullptr, 16))) {

			std::vector<std::pair<SqpackDataLocator, std::tuple<uint32_t, uint32_t, const char*>>> offsets1;
			offsets1.reserve(
				(std::max)(Index1.HashLocators().size() + Index1.TextLocators().size(), Index2.HashLocators().size() + Index2.TextLocators().size())
				+ Index1.IndexHeader().TextLocatorSegment.Count
			);
			for (const auto& item : Index1.HashLocators())
				if (!item.Locator.IsSynonym)
					offsets1.emplace_back(item.Locator, std::make_tuple(item.PathHash, item.NameHash, static_cast<const char*>(nullptr)));
			for (const auto& item : Index1.TextLocators())
				offsets1.emplace_back(item.Locator, std::make_tuple(item.PathHash, item.NameHash, item.FullPath));

			std::vector<std::pair<SqpackDataLocator, std::tuple<uint32_t, const char*>>> offsets2;
			for (const auto& item : Index2.HashLocators())
				if (!item.Locator.IsSynonym)
					offsets2.emplace_back(item.Locator, std::make_tuple(item.FullPathHash, static_cast<const char*>(nullptr)));
			for (const auto& item : Index2.TextLocators())
				offsets2.emplace_back(item.Locator, std::make_tuple(item.FullPathHash, item.FullPath));

			if (offsets1.size() != offsets2.size())
				throw CorruptDataException(".index and .index2 do not have the same number of files contained");

			Data.reserve(Index1.IndexHeader().TextLocatorSegment.Count);
			for (uint32_t i = 0; i < Index1.IndexHeader().TextLocatorSegment.Count; ++i) {
				Data.emplace_back(SqDataType{
					dataStreams[i],
					i,
					strictVerify,
					});
				offsets1.emplace_back(SqpackDataLocator(i, Data[i].Stream->StreamSize()), std::make_tuple(UINT32_MAX, UINT32_MAX, static_cast<const char*>(nullptr)));
				offsets2.emplace_back(SqpackDataLocator(i, Data[i].Stream->StreamSize()), std::make_tuple(UINT32_MAX, static_cast<const char*>(nullptr)));
			}

			struct Comparator {
				bool operator()(const SqpackDataLocator& l, const SqpackDataLocator& r) const {
					if (l.DatFileIndex != r.DatFileIndex)
						return l.DatFileIndex < r.DatFileIndex;
					if (l.DatFileOffset() != r.DatFileOffset())
						return l.DatFileOffset() < r.DatFileOffset();
					return false;
				}

				bool operator()(const std::pair<SqpackDataLocator, std::tuple<uint32_t, uint32_t, const char*>>& l, const std::pair<SqpackDataLocator, std::tuple<uint32_t, uint32_t, const char*>>& r) const {
					return (*this)(l.first, r.first);
				}

				bool operator()(const std::pair<SqpackDataLocator, std::tuple<uint32_t, const char*>>& l, const std::pair<SqpackDataLocator, std::tuple<uint32_t, const char*>>& r) const {
					return (*this)(l.first, r.first);
				}

				bool operator()(const EntryInfoType& l, const EntryInfoType& r) const {
					return l.Locator < r.Locator;
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

			SqpackPathSpec pathSpec;
			for (size_t curr = 1, prev = 0; curr < offsets1.size(); ++curr, ++prev) {

				// Skip dummy items to mark end of individual .dat file.
				if (offsets1[prev].first.DatFileIndex != offsets1[curr].first.DatFileIndex)
					continue;

				EntryInfo.emplace_back(EntryInfoType{ .Locator = offsets1[prev].first, .Allocation = offsets1[curr].first.DatFileOffset() - offsets1[prev].first.DatFileOffset() });
				if (std::get<2>(offsets1[prev].second))
					EntryInfo.back().PathSpec = SqpackPathSpec(std::get<2>(offsets1[prev].second));
				else if (std::get<1>(offsets2[prev].second))
					EntryInfo.back().PathSpec = SqpackPathSpec(std::get<1>(offsets2[prev].second));
				else
					EntryInfo.back().PathSpec = SqpackPathSpec(
						std::get<0>(offsets1[prev].second),
						std::get<1>(offsets1[prev].second),
						std::get<0>(offsets2[prev].second),
						static_cast<uint8_t>(CategoryId),
						static_cast<uint8_t>(ExpacId),
						static_cast<uint8_t>(PartId));
			}

			std::sort(EntryInfo.begin(), EntryInfo.end(), Comparator());
		}

		static SqpackReader FromPath(const std::filesystem::path& indexFile, bool strictVerify = false) {
			std::vector<std::shared_ptr<IStream>> dataStreams;
			for (int i = 0; i < 8; ++i) {
				auto dataPath = std::filesystem::path(indexFile);
				dataPath.replace_extension(std::format(".dat{}", i));
				if (!exists(dataPath))
					break;
				dataStreams.emplace_back(std::make_shared<FileStream>(dataPath));
			}

			return SqpackReader(indexFile.filename().string(),
				std::make_shared<FileStream>(std::filesystem::path(indexFile).replace_extension(".index")),
				std::make_shared<FileStream>(std::filesystem::path(indexFile).replace_extension(".index2")),
				std::move(dataStreams),
				strictVerify);
		}

		[[nodiscard]] const uint32_t PackId() const {
			return (CategoryId << 16) | (ExpacId << 8) | PartId;
		}

		[[nodiscard]] const SqpackDataLocator& GetLocator1(const SqpackPathSpec& pathSpec) const {
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

		[[nodiscard]] const SqpackDataLocator& GetLocator2(const SqpackPathSpec& pathSpec) const {
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

		[[nodiscard]] std::shared_ptr<PackedFileStream> GetPackedFileStream(const EntryInfoType& info) const {
			return std::make_unique<StreamAsPackedFileViewStream>(info.PathSpec, std::make_shared<PartialViewStream>(Data.at(info.Locator.DatFileIndex).Stream, info.Locator.DatFileOffset(), info.Allocation));
		}

		[[nodiscard]] std::shared_ptr<PackedFileStream> GetPackedFileStream(const SqpackPathSpec& pathSpec) const {
			struct Comparator {
				bool operator()(const EntryInfoType& l, const SqpackDataLocator& r) const {
					return l.Locator < r;
				}

				bool operator()(const SqpackDataLocator& l, const EntryInfoType& r) const {
					return l < r.Locator;
				}
			};

			const auto& locator = GetLocator1(pathSpec);
			const auto entryInfo = std::lower_bound(EntryInfo.begin(), EntryInfo.end(), locator, Comparator());
			return GetPackedFileStream(*entryInfo);
		}

		[[nodiscard]] std::shared_ptr<PackedFileUnpackingStream> GetFileStream(const EntryInfoType& info, std::span<uint8_t> obfuscatedHeaderRewrite = {}) const {
			return std::make_shared<PackedFileUnpackingStream>(GetPackedFileStream(info), obfuscatedHeaderRewrite);
		}

		[[nodiscard]] std::shared_ptr<PackedFileUnpackingStream> GetFileStream(const SqpackPathSpec& pathSpec, std::span<uint8_t> obfuscatedHeaderRewrite = {}) const {
			return std::make_shared<PackedFileUnpackingStream>(GetPackedFileStream(pathSpec), obfuscatedHeaderRewrite);
		}
	};
}

#endif
