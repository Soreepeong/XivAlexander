#ifndef _XIVRES_SQPACKGENERATOR_H_
#define _XIVRES_SQPACKGENERATOR_H_

#include "Internal/ListenerManager.h"

#include "BinaryPackedFileStream.h"
#include "EmptyOrObfuscatedPackedFileStream.h"
#include "HotSwappablePackedFileStream.h"
#include "ModelPackedFileStream.h"
#include "PackedFileUnpackingStream.h"
#include "SqpackReader.h"
#include "TexturePackedFileStream.h"

namespace XivRes {
	class SqpackGenerator {
		const uint64_t m_maxFileSize;

		class DataViewStream;

	public:
		struct Entry {
			uint32_t EntrySize{};
			SqpackDataLocator Locator{};

			std::shared_ptr<PackedFileStream> Provider;
		};

		struct AddEntryResult {
			std::vector<PackedFileStream*> Added;
			std::vector<PackedFileStream*> Replaced;
			std::vector<PackedFileStream*> SkippedExisting;
			std::vector<std::pair<SqpackPathSpec, std::string>> Error;

			AddEntryResult& operator+=(const AddEntryResult& r) {
				auto& k = r.Added;
				Added.insert(Added.end(), r.Added.begin(), r.Added.end());
				Replaced.insert(Replaced.end(), r.Replaced.begin(), r.Replaced.end());
				SkippedExisting.insert(SkippedExisting.end(), r.SkippedExisting.begin(), r.SkippedExisting.end());
				Error.insert(Error.end(), r.Error.begin(), r.Error.end());
				return *this;
			}

			AddEntryResult& operator+=(AddEntryResult&& r) {
				auto& k = r.Added;
				Added.insert(Added.end(), r.Added.begin(), r.Added.end());
				Replaced.insert(Replaced.end(), r.Replaced.begin(), r.Replaced.end());
				SkippedExisting.insert(SkippedExisting.end(), r.SkippedExisting.begin(), r.SkippedExisting.end());
				Error.insert(Error.end(), r.Error.begin(), r.Error.end());
				r.Added.clear();
				r.Replaced.clear();
				r.SkippedExisting.clear();
				r.Error.clear();
				return *this;
			}

			[[nodiscard]] PackedFileStream* AnyItem() const {
				if (!Added.empty())
					return Added[0];
				if (!Replaced.empty())
					return Replaced[0];
				if (!SkippedExisting.empty())
					return SkippedExisting[0];
				return nullptr;
			}

			[[nodiscard]] std::vector<PackedFileStream*> AllSuccessfulEntries() const {
				std::vector<PackedFileStream*> res;
				res.insert(res.end(), Added.begin(), Added.end());
				res.insert(res.end(), Replaced.begin(), Replaced.end());
				res.insert(res.end(), SkippedExisting.begin(), SkippedExisting.end());
				return res;
			}
		};

		struct SqpackViews {
			std::shared_ptr<IStream> Index1;
			std::shared_ptr<IStream> Index2;
			std::vector<std::shared_ptr<IStream>> Data;
			std::vector<Entry*> Entries;
			std::map<SqpackPathSpec, std::unique_ptr<Entry>, SqpackPathSpec::AllHashComparator> HashOnlyEntries;
			std::map<SqpackPathSpec, std::unique_ptr<Entry>, SqpackPathSpec::FullPathComparator> FullPathEntries;
		};

		class SqpackViewEntryCache {
			static constexpr auto SmallEntryBufferSize = (INTPTR_MAX == INT64_MAX ? 256 : 8) * 1048576;
			static constexpr auto LargeEntryBufferSizeMax = (INTPTR_MAX == INT64_MAX ? 1024 : 64) * 1048576;

		public:
			class BufferedEntry {
				const DataViewStream* m_view = nullptr;
				const Entry* m_entry = nullptr;
				std::vector<uint8_t> m_bufferPreallocated;
				std::vector<uint8_t> m_bufferTemporary;
				std::span<uint8_t> m_bufferActive;

			public:
				bool Empty() const {
					return m_view == nullptr || m_entry == nullptr;
				}

				bool IsEntry(const DataViewStream* view, const Entry* entry) const {
					return m_view == view && m_entry == entry;
				}

				void ClearEntry() {
					m_view = nullptr;
					m_entry = nullptr;
					if (!m_bufferTemporary.empty())
						std::vector<uint8_t>().swap(m_bufferTemporary);
				}

				auto GetEntry() const {
					return std::make_pair(m_view, m_entry);
				}

				void SetEntry(const DataViewStream* view, const Entry* entry) {
					m_view = view;
					m_entry = entry;

					if (entry->EntrySize <= SmallEntryBufferSize) {
						if (!m_bufferTemporary.empty())
							std::vector<uint8_t>().swap(m_bufferTemporary);
						if (m_bufferPreallocated.size() != SmallEntryBufferSize)
							m_bufferPreallocated.resize(SmallEntryBufferSize);
						m_bufferActive = std::span(m_bufferPreallocated.begin(), entry->EntrySize);
					} else {
						m_bufferTemporary.resize(entry->EntrySize);
						m_bufferActive = std::span(m_bufferTemporary);
					}
					ReadStream(*entry->Provider, 0, m_bufferActive);
				}

				const auto& Buffer() const {
					return m_bufferActive;
				}
			};

		private:
			BufferedEntry m_lastActiveEntry;

		public:
			BufferedEntry* GetBuffer(const DataViewStream* view, const Entry* entry) {
				if (m_lastActiveEntry.IsEntry(view, entry))
					return &m_lastActiveEntry;

				if (entry->EntrySize > LargeEntryBufferSizeMax)
					return nullptr;

				m_lastActiveEntry.SetEntry(view, entry);
				return &m_lastActiveEntry;
			}

			void Flush() {
				m_lastActiveEntry.ClearEntry();
			}
		};

		const std::string DatExpac;
		const std::string DatName;

	private:
		std::map<SqpackPathSpec, std::unique_ptr<Entry>, SqpackPathSpec::AllHashComparator> m_hashOnlyEntries;
		std::map<SqpackPathSpec, std::unique_ptr<Entry>, SqpackPathSpec::FullPathComparator> m_fullEntries;

		std::vector<SqpackSegment3Entry> m_sqpackIndexSegment3;
		std::vector<SqpackSegment3Entry> m_sqpackIndex2Segment3;

	public:
		Internal::ListenerManager<SqpackGenerator, void, const std::string&> Log;

		SqpackGenerator(std::string ex, std::string name, uint64_t maxFileSize = SqpackDataHeader::MaxFileSize_MaxValue) : m_maxFileSize(maxFileSize)
			, DatExpac(std::move(ex))
			, DatName(std::move(name)) {
			if (maxFileSize > SqpackDataHeader::MaxFileSize_MaxValue)
				throw std::invalid_argument("MaxFileSize cannot be more than 32GiB.");
		}

		void AddEntry(AddEntryResult& result, std::shared_ptr<PackedFileStream> provider, bool overwriteExisting) {
			const auto pProvider = provider.get();

			try {
				Entry* pEntry = nullptr;

				if (const auto it = m_hashOnlyEntries.find(provider->PathSpec()); it != m_hashOnlyEntries.end()) {
					pEntry = it->second.get();
					if (!pEntry->Provider->PathSpec().HasOriginal() && provider->PathSpec().HasOriginal()) {
						pEntry->Provider->UpdatePathSpec(provider->PathSpec());
						m_fullEntries.emplace(pProvider->PathSpec(), std::move(it->second));
						m_hashOnlyEntries.erase(it);
					}
				} else if (const auto it = m_fullEntries.find(provider->PathSpec()); it != m_fullEntries.end()) {
					pEntry = it->second.get();
				}

				if (pEntry) {
					if (!overwriteExisting) {
						pEntry->Provider->UpdatePathSpec(provider->PathSpec());
						result.SkippedExisting.emplace_back(pEntry->Provider.get());
						return;
					}
					pEntry->Provider = std::move(provider);
					result.Replaced.emplace_back(pProvider);
					return;
				}

				auto entry = std::make_unique<Entry>(0, SqpackDataLocator{ 0, 0 }, std::move(provider));
				if (pProvider->PathSpec().HasOriginal())
					m_fullEntries.emplace(pProvider->PathSpec(), std::move(entry));
				else
					m_hashOnlyEntries.emplace(pProvider->PathSpec(), std::move(entry));
				result.Added.emplace_back(pProvider);
			} catch (const std::exception& e) {
				result.Error.emplace_back(pProvider->PathSpec(), e.what());
			}
		}

		AddEntryResult AddEntry(std::shared_ptr<PackedFileStream> provider, bool overwriteExisting = true) {
			AddEntryResult result;
			AddEntry(result, std::move(provider), overwriteExisting);
			return result;
		}

		AddEntryResult AddEntriesFromSqPack(const std::filesystem::path& indexPath, bool overwriteExisting = true, bool overwriteUnknownSegments = false) {
			auto reader = SqpackReader::FromPath(indexPath);

			if (overwriteUnknownSegments) {
				m_sqpackIndexSegment3 = { reader.Index1.Segment3().begin(), reader.Index1.Segment3().end() };
				m_sqpackIndex2Segment3 = { reader.Index2.Segment3().begin(), reader.Index2.Segment3().end() };
			}

			AddEntryResult result;
			for (const auto& entryInfo : reader.EntryInfo) {
				try {
					AddEntry(result, reader.GetPackedFileStream(entryInfo), overwriteExisting);
				} catch (const std::exception& e) {
					result.Error.emplace_back(entryInfo.PathSpec, e.what());
				}
			}
			return result;
		}

		AddEntryResult AddEntryFromFile(SqpackPathSpec pathSpec, const std::filesystem::path& path, bool overwriteExisting = true) {
			std::shared_ptr<PackedFileStream> provider;

			auto extensionLower = path.extension().u8string();
			for (auto& c : extensionLower)
				if (u8'A' <= c && c <= u8'Z')
					c += 'a' - 'A';

			if (file_size(path) == 0) {
				provider = std::make_shared<EmptyOrObfuscatedPackedFileStream>(std::move(pathSpec));
			} else if (extensionLower == u8".tex" || extensionLower == u8".atex") {
				provider = std::make_shared<TexturePackedFileViewStream>(std::move(pathSpec), path);
			} else if (extensionLower == u8".mdl") {
				provider = std::make_shared<ModelPackedFileViewStream>(std::move(pathSpec), path);
			} else {
				provider = std::make_shared<BinaryPackedFileViewStream>(std::move(pathSpec), path);
			}

			return AddEntry(provider, overwriteExisting);
		}

		void ReserveSwappableSpace(SqpackPathSpec pathSpec, uint32_t size) {
			if (const auto it = m_hashOnlyEntries.find(pathSpec); it != m_hashOnlyEntries.end()) {
				it->second->EntrySize = (std::max)(it->second->EntrySize, size);
				if (!it->second->Provider->PathSpec().HasOriginal() && pathSpec.HasOriginal()) {
					it->second->Provider->UpdatePathSpec(pathSpec);
					m_fullEntries.emplace(pathSpec, std::move(it->second));
					m_hashOnlyEntries.erase(it);
				}
			} else if (const auto it = m_fullEntries.find(pathSpec); it != m_fullEntries.end()) {
				it->second->EntrySize = (std::max)(it->second->EntrySize, size);
			} else {
				auto entry = std::make_unique<Entry>(size, SqpackDataLocator{ 0, 0 }, std::make_shared<EmptyOrObfuscatedPackedFileStream>(std::move(pathSpec)));
				if (entry->Provider->PathSpec().HasOriginal())
					m_fullEntries.emplace(entry->Provider->PathSpec(), std::move(entry));
				else
					m_hashOnlyEntries.emplace(entry->Provider->PathSpec(), std::move(entry));
			}
		}

		SqpackViews FinishToStreams(bool strict, const std::shared_ptr<SqpackViewEntryCache>& dataBuffer = nullptr) {
			SqpackHeader dataHeader{};
			std::vector<SqpackDataHeader> dataSubheaders;
			std::vector<std::pair<size_t, size_t>> dataEntryRanges;

			auto res = SqpackViews{
				.HashOnlyEntries = std::move(m_hashOnlyEntries),
				.FullPathEntries = std::move(m_fullEntries),
			};

			res.Entries.reserve(m_fullEntries.size() + m_hashOnlyEntries.size());
			for (auto& entry : res.HashOnlyEntries | std::views::values)
				res.Entries.emplace_back(entry.get());
			for (auto& entry : res.FullPathEntries | std::views::values)
				res.Entries.emplace_back(entry.get());

			std::map<std::pair<uint32_t, uint32_t>, std::vector<Entry*>> pairHashes;
			std::map<uint32_t, std::vector<Entry*>> fullHashes;
			for (const auto& entry : res.Entries) {
				const auto& pathSpec = entry->Provider->PathSpec();
				pairHashes[std::make_pair(pathSpec.PathHash(), pathSpec.NameHash())].emplace_back(entry);
				fullHashes[pathSpec.FullPathHash()].emplace_back(entry);
			}

			for (size_t i = 0; i < res.Entries.size(); ++i) {
				auto& entry = res.Entries[i];
				const auto& pathSpec = entry->Provider->PathSpec();
				entry->EntrySize = Align((std::max)(entry->EntrySize, static_cast<uint32_t>(entry->Provider->StreamSize()))).Alloc;
				entry->Provider = std::make_shared<HotSwappablePackedFileStream>(pathSpec, entry->EntrySize, std::move(entry->Provider));

				if (dataSubheaders.empty() ||
					sizeof SqpackHeader + sizeof SqpackDataHeader + dataSubheaders.back().DataSize + entry->EntrySize > dataSubheaders.back().MaxFileSize) {
					if (strict && !dataSubheaders.empty()) {
						Internal::SHA1 sha1;
						for (auto j = dataEntryRanges.back().first, j_ = j + dataEntryRanges.back().second; j < j_; ++j) {
							const auto& entry = res.Entries[j];
							const auto& provider = *entry->Provider;
							const auto length = provider.StreamSize();
							uint8_t buf[4096];
							for (std::streamoff j = 0; j < length; j += sizeof buf) {
								const auto readlen = static_cast<size_t>((std::min<uint64_t>)(sizeof buf, length - j));
								ReadStream(provider, j, buf, readlen);
								sha1.ProcessBytes(buf, readlen);
							}
						}
						sha1.GetDigestBytes(dataSubheaders.back().DataSha1.Value);
						dataSubheaders.back().Sha1.SetFromSpan(reinterpret_cast<char*>(&dataSubheaders.back()), offsetof(SqpackDataHeader, Sha1));
					}
					dataSubheaders.emplace_back(SqpackDataHeader{
						.HeaderSize = sizeof SqpackDataHeader,
						.Unknown1 = SqpackDataHeader::Unknown1_Value,
						.DataSize = 0,
						.SpanIndex = static_cast<uint32_t>(dataSubheaders.size()),
						.MaxFileSize = m_maxFileSize,
						});
					dataEntryRanges.emplace_back(i, 0);
				}

				entry->Locator = { static_cast<uint32_t>(dataSubheaders.size() - 1), sizeof SqpackHeader + sizeof SqpackDataHeader + dataSubheaders.back().DataSize };

				dataSubheaders.back().DataSize = dataSubheaders.back().DataSize + entry->EntrySize;
				dataEntryRanges.back().second++;
			}

			if (strict && !dataSubheaders.empty()) {
				Internal::SHA1 sha1;
				for (auto j = dataEntryRanges.back().first, j_ = j + dataEntryRanges.back().second; j < j_; ++j) {
					const auto& entry = res.Entries[j];
					const auto& provider = *entry->Provider;
					const auto length = provider.StreamSize();
					uint8_t buf[4096];
					for (std::streamoff j = 0; j < length; j += sizeof buf) {
						const auto readlen = static_cast<size_t>((std::min<uint64_t>)(sizeof buf, length - j));
						ReadStream(provider, j, buf, readlen);
						sha1.ProcessBytes(buf, readlen);
					}
				}
				sha1.GetDigestBytes(dataSubheaders.back().DataSha1.Value);
				dataSubheaders.back().Sha1.SetFromSpan(reinterpret_cast<char*>(&dataSubheaders.back()), offsetof(SqpackDataHeader, Sha1));
			}

			std::vector<SqpackPairHashLocator> fileEntries1;
			std::vector<SqpackPairHashWithTextLocator> conflictEntries1;
			for (const auto& [pairHash, correspondingEntries] : pairHashes) {
				if (correspondingEntries.size() == 1) {
					fileEntries1.emplace_back(SqpackPairHashLocator{ pairHash.second, pairHash.first, correspondingEntries.front()->Locator, 0 });
				} else {
					fileEntries1.emplace_back(SqpackPairHashLocator{ pairHash.second, pairHash.first, SqpackDataLocator::Synonym(), 0 });
					uint32_t i = 0;
					for (const auto& entry : correspondingEntries) {
						conflictEntries1.emplace_back(SqpackPairHashWithTextLocator{
							.NameHash = pairHash.second,
							.PathHash = pairHash.first,
							.Locator = entry->Locator,
							.ConflictIndex = i++,
							});
						const auto path = entry->Provider->PathSpec().Path();
						strncpy_s(conflictEntries1.back().FullPath, path.c_str(), path.size());
					}
				}
			}
			conflictEntries1.emplace_back(SqpackPairHashWithTextLocator{
				.NameHash = SqpackPairHashWithTextLocator::EndOfList,
				.PathHash = SqpackPairHashWithTextLocator::EndOfList,
				.Locator = 0,
				.ConflictIndex = SqpackPairHashWithTextLocator::EndOfList,
				});

			std::vector<SqpackFullHashLocator> fileEntries2;
			std::vector<SqpackFullHashWithTextLocator> conflictEntries2;
			for (const auto& [fullHash, correspondingEntries] : fullHashes) {
				if (correspondingEntries.size() == 1) {
					fileEntries2.emplace_back(SqpackFullHashLocator{ fullHash, correspondingEntries.front()->Locator });
				} else {
					fileEntries2.emplace_back(SqpackFullHashLocator{ fullHash, SqpackDataLocator::Synonym() });
					uint32_t i = 0;
					for (const auto& entry : correspondingEntries) {
						conflictEntries2.emplace_back(SqpackFullHashWithTextLocator{
							.FullPathHash = fullHash,
							.UnusedHash = 0,
							.Locator = entry->Locator,
							.ConflictIndex = i++,
							});
						const auto path = entry->Provider->PathSpec().Path();
						strncpy_s(conflictEntries2.back().FullPath, path.c_str(), path.size());
					}
				}
			}
			conflictEntries2.emplace_back(SqpackFullHashWithTextLocator{
				.FullPathHash = SqpackFullHashWithTextLocator::EndOfList,
				.UnusedHash = SqpackFullHashWithTextLocator::EndOfList,
				.Locator = 0,
				.ConflictIndex = SqpackFullHashWithTextLocator::EndOfList,
				});

			memcpy(dataHeader.Signature, SqpackHeader::Signature_Value, sizeof SqpackHeader::Signature_Value);
			dataHeader.HeaderSize = sizeof SqpackHeader;
			dataHeader.Unknown1 = SqpackHeader::Unknown1_Value;
			dataHeader.Type = SqpackType::SqData;
			dataHeader.Unknown2 = SqpackHeader::Unknown2_Value;
			if (strict)
				dataHeader.Sha1.SetFromSpan(reinterpret_cast<char*>(&dataHeader), offsetof(SqpackHeader, Sha1));

			res.Index1 = std::make_shared<XivRes::MemoryStream>(ExportIndexFileData<XivRes::SqpackIndexHeader::IndexType::Index, SqpackPairHashLocator, SqpackPairHashWithTextLocator, true>(
				dataSubheaders.size(), std::move(fileEntries1), std::move(conflictEntries1), m_sqpackIndexSegment3, std::vector<SqpackPathHashLocator>(), strict));
			res.Index2 = std::make_shared<XivRes::MemoryStream>(ExportIndexFileData<XivRes::SqpackIndexHeader::IndexType::Index, SqpackFullHashLocator, SqpackFullHashWithTextLocator, false>(
				dataSubheaders.size(), std::move(fileEntries2), std::move(conflictEntries2), m_sqpackIndex2Segment3, std::vector<SqpackPathHashLocator>(), strict));
			for (size_t i = 0; i < dataSubheaders.size(); ++i)
				res.Data.emplace_back(std::make_shared<DataViewStream>(dataHeader, dataSubheaders[i], std::span(res.Entries).subspan(dataEntryRanges[i].first, dataEntryRanges[i].second), dataBuffer));

			return res;
		}

		void FinishToFiles(const std::filesystem::path& dir, bool strict = false) {
			SqpackHeader dataHeader{};
			memcpy(dataHeader.Signature, SqpackHeader::Signature_Value, sizeof SqpackHeader::Signature_Value);
			dataHeader.HeaderSize = sizeof SqpackHeader;
			dataHeader.Unknown1 = SqpackHeader::Unknown1_Value;
			dataHeader.Type = SqpackType::SqData;
			dataHeader.Unknown2 = SqpackHeader::Unknown2_Value;
			if (strict)
				dataHeader.Sha1.SetFromSpan(reinterpret_cast<char*>(&dataHeader), offsetof(SqpackHeader, Sha1));

			std::vector<SqpackDataHeader> dataSubheaders;

			std::vector<std::unique_ptr<Entry>> entries;
			entries.reserve(m_fullEntries.size() + m_hashOnlyEntries.size());
			for (auto& e : m_fullEntries)
				entries.emplace_back(std::move(e.second));
			for (auto& e : m_hashOnlyEntries)
				entries.emplace_back(std::move(e.second));
			m_fullEntries.clear();
			m_hashOnlyEntries.clear();

			std::map<std::pair<uint32_t, uint32_t>, std::vector<Entry*>> pairHashes;
			std::map<uint32_t, std::vector<Entry*>> fullHashes;
			std::map<Entry*, SqpackPathSpec> entryPathSpecs;
			for (const auto& entry : entries) {
				const auto& pathSpec = entry->Provider->PathSpec();
				entryPathSpecs.emplace(entry.get(), pathSpec);
				pairHashes[std::make_pair(pathSpec.PathHash(), pathSpec.NameHash())].emplace_back(entry.get());
				fullHashes[pathSpec.FullPathHash()].emplace_back(entry.get());
			}

			std::vector<SqpackDataLocator> locators;

			std::fstream dataFile;
			std::vector<char> buf(1024 * 1024);
			for (size_t i = 0; i < entries.size(); ++i) {
				auto& entry = *entries[i];
				const auto provider{ std::move(entry.Provider) };
				const auto entrySize = provider->StreamSize();

				if (dataSubheaders.empty() ||
					sizeof SqpackHeader + sizeof SqpackDataHeader + dataSubheaders.back().DataSize + entrySize > dataSubheaders.back().MaxFileSize) {
					if (!dataSubheaders.empty() && dataFile.is_open()) {
						if (strict) {
							Internal::SHA1 sha1;
							dataFile.seekg(sizeof SqpackHeader + sizeof SqpackDataHeader, std::ios::beg);
							Align<uint64_t>(dataSubheaders.back().DataSize, buf.size()).IterateChunked([&](uint64_t index, uint64_t offset, uint64_t size) {
								dataFile.read(&buf[0], static_cast<size_t>(size));
								if (!dataFile)
									throw std::runtime_error("Failed to read from output data file.");
								sha1.ProcessBytes(&buf[0], static_cast<size_t>(size));
								}, sizeof SqpackHeader + sizeof SqpackDataHeader);

							sha1.GetDigestBytes(dataSubheaders.back().DataSha1.Value);
							dataSubheaders.back().Sha1.SetFromSpan(reinterpret_cast<char*>(&dataSubheaders.back()), offsetof(SqpackDataHeader, Sha1));
						}

						dataFile.seekp(0, std::ios::beg);
						dataFile.write(reinterpret_cast<const char*>(&dataHeader), sizeof dataHeader);
						dataFile.write(reinterpret_cast<const char*>(&dataSubheaders.back()), sizeof dataSubheaders.back());
						dataFile.close();
					}

					dataFile.open(dir / std::format("{}.win32.dat{}", DatName, dataSubheaders.size()), std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
					dataSubheaders.emplace_back(SqpackDataHeader{
						.HeaderSize = sizeof SqpackDataHeader,
						.Unknown1 = SqpackDataHeader::Unknown1_Value,
						.DataSize = 0,
						.SpanIndex = static_cast<uint32_t>(dataSubheaders.size()),
						.MaxFileSize = m_maxFileSize,
						});
				}

				entry.Locator = { static_cast<uint32_t>(dataSubheaders.size() - 1), sizeof SqpackHeader + sizeof SqpackDataHeader + dataSubheaders.back().DataSize };
				dataFile.seekg(entry.Locator.DatFileOffset(), std::ios::beg);
				Align<uint64_t>(entrySize, buf.size()).IterateChunked([&](uint64_t index, uint64_t offset, uint64_t size) {
					const auto bufSpan = std::span(buf).subspan(0, static_cast<size_t>(size));
					ReadStream(*provider, offset, bufSpan);
					dataFile.write(&buf[0], bufSpan.size());
					if (!dataFile)
						throw std::runtime_error("Failed to write to output data file.");
					});

				dataSubheaders.back().DataSize = dataSubheaders.back().DataSize + entrySize;
			}

			if (!dataSubheaders.empty() && dataFile.is_open()) {
				if (strict) {
					Internal::SHA1 sha1;
					dataFile.seekg(sizeof SqpackHeader + sizeof SqpackDataHeader, std::ios::beg);
					Align<uint64_t>(dataSubheaders.back().DataSize, buf.size()).IterateChunked([&](uint64_t index, uint64_t offset, uint64_t size) {
						dataFile.read(&buf[0], static_cast<size_t>(size));
						if (!dataFile)
							throw std::runtime_error("Failed to read from output data file.");
						sha1.ProcessBytes(&buf[0], static_cast<size_t>(size));
						}, sizeof SqpackHeader + sizeof SqpackDataHeader);

					sha1.GetDigestBytes(dataSubheaders.back().DataSha1.Value);
					dataSubheaders.back().Sha1.SetFromSpan(reinterpret_cast<char*>(&dataSubheaders.back()), offsetof(SqpackDataHeader, Sha1));
				}

				dataFile.seekp(0, std::ios::beg);
				dataFile.write(reinterpret_cast<const char*>(&dataHeader), sizeof dataHeader);
				dataFile.write(reinterpret_cast<const char*>(&dataSubheaders.back()), sizeof dataSubheaders.back());
				dataFile.close();
			}

			std::vector<SqpackPairHashLocator> fileEntries1;
			std::vector<SqpackPairHashWithTextLocator> conflictEntries1;
			for (const auto& [pairHash, correspondingEntries] : pairHashes) {
				if (correspondingEntries.size() == 1) {
					fileEntries1.emplace_back(SqpackPairHashLocator{ pairHash.second, pairHash.first, correspondingEntries.front()->Locator, 0 });
				} else {
					fileEntries1.emplace_back(SqpackPairHashLocator{ pairHash.second, pairHash.first, SqpackDataLocator::Synonym(), 0 });
					uint32_t i = 0;
					for (const auto& entry : correspondingEntries) {
						conflictEntries1.emplace_back(SqpackPairHashWithTextLocator{
							.NameHash = pairHash.second,
							.PathHash = pairHash.first,
							.Locator = entry->Locator,
							.ConflictIndex = i++,
							});
						const auto path = entryPathSpecs[entry].Path();
						strncpy_s(conflictEntries1.back().FullPath, path.c_str(), path.size());
					}
				}
			}
			conflictEntries1.emplace_back(SqpackPairHashWithTextLocator{
				.NameHash = SqpackPairHashWithTextLocator::EndOfList,
				.PathHash = SqpackPairHashWithTextLocator::EndOfList,
				.Locator = 0,
				.ConflictIndex = SqpackPairHashWithTextLocator::EndOfList,
				});

			std::vector<SqpackFullHashLocator> fileEntries2;
			std::vector<SqpackFullHashWithTextLocator> conflictEntries2;
			for (const auto& [fullHash, correspondingEntries] : fullHashes) {
				if (correspondingEntries.size() == 1) {
					fileEntries2.emplace_back(SqpackFullHashLocator{ fullHash, correspondingEntries.front()->Locator });
				} else {
					fileEntries2.emplace_back(SqpackFullHashLocator{ fullHash, SqpackDataLocator::Synonym() });
					uint32_t i = 0;
					for (const auto& entry : correspondingEntries) {
						conflictEntries2.emplace_back(SqpackFullHashWithTextLocator{
							.FullPathHash = fullHash,
							.UnusedHash = 0,
							.Locator = entry->Locator,
							.ConflictIndex = i++,
							});
						const auto path = entryPathSpecs[entry].Path();
						strncpy_s(conflictEntries2.back().FullPath, path.c_str(), path.size());
					}
				}
			}
			conflictEntries2.emplace_back(SqpackFullHashWithTextLocator{
				.FullPathHash = SqpackFullHashWithTextLocator::EndOfList,
				.UnusedHash = SqpackFullHashWithTextLocator::EndOfList,
				.Locator = 0,
				.ConflictIndex = SqpackFullHashWithTextLocator::EndOfList,
				});

			auto indexData = ExportIndexFileData<XivRes::SqpackIndexHeader::IndexType::Index, SqpackPairHashLocator, SqpackPairHashWithTextLocator, true>(
				dataSubheaders.size(), std::move(fileEntries1), std::move(conflictEntries1), m_sqpackIndexSegment3, std::vector<SqpackPathHashLocator>(), strict);
			std::ofstream(dir / std::format("{}.win32.index", DatName), std::ios::binary).write(reinterpret_cast<const char*>(&indexData[0]), indexData.size());

			indexData = ExportIndexFileData<XivRes::SqpackIndexHeader::IndexType::Index, SqpackFullHashLocator, SqpackFullHashWithTextLocator, false>(
				dataSubheaders.size(), std::move(fileEntries2), std::move(conflictEntries2), m_sqpackIndex2Segment3, std::vector<SqpackPathHashLocator>(), strict);
			std::ofstream(dir / std::format("{}.win32.index2", DatName), std::ios::binary).write(reinterpret_cast<const char*>(&indexData[0]), indexData.size());
		}

		[[nodiscard]] std::unique_ptr<DefaultAbstractStream> GetFile(const SqpackPathSpec& pathSpec) const {
			if (const auto it = m_hashOnlyEntries.find(pathSpec); it != m_hashOnlyEntries.end())
				return std::make_unique<PackedFileUnpackingStream>(it->second->Provider);
			if (const auto it = m_fullEntries.find(pathSpec); it != m_fullEntries.end())
				return std::make_unique<PackedFileUnpackingStream>(it->second->Provider);
			throw std::out_of_range(std::format("PathSpec({}) not found", pathSpec));
		}
		
		std::vector<SqpackPathSpec> AllPathSpec() const {
			std::vector<SqpackPathSpec> res;
			res.reserve(m_hashOnlyEntries.size() + m_fullEntries.size());
			for (const auto& entry : m_hashOnlyEntries | std::views::keys)
				res.emplace_back(entry);
			for (const auto& entry : m_fullEntries | std::views::keys)
				res.emplace_back(entry);
			return res;
		}

	private:
		template<XivRes::SqpackIndexHeader::IndexType IndexType, typename PackedFileType, typename ConflictEntryType, bool UseFolders>
		static std::vector<uint8_t> ExportIndexFileData(
			size_t dataFilesCount,
			std::vector<PackedFileType> fileSegment,
			const std::vector<ConflictEntryType>& conflictSegment,
			const std::vector<XivRes::SqpackSegment3Entry>& segment3,
			std::vector<XivRes::SqpackPathHashLocator> folderSegment = {},
			bool strict = false
		) {
			std::vector<uint8_t> data;
			data.reserve(sizeof SqpackHeader
				+ sizeof SqpackIndexHeader
				+ std::span(fileSegment).size_bytes()
				+ std::span(conflictSegment).size_bytes()
				+ std::span(segment3).size_bytes()
				+ std::span(folderSegment).size_bytes());

			data.resize(sizeof SqpackHeader + sizeof SqpackIndexHeader);
			auto& header1 = *reinterpret_cast<SqpackHeader*>(&data[0]);
			memcpy(header1.Signature, SqpackHeader::Signature_Value, sizeof SqpackHeader::Signature_Value);
			header1.HeaderSize = sizeof SqpackHeader;
			header1.Unknown1 = SqpackHeader::Unknown1_Value;
			header1.Type = SqpackType::SqIndex;
			header1.Unknown2 = SqpackHeader::Unknown2_Value;
			if (strict)
				header1.Sha1.SetFromSpan(reinterpret_cast<char*>(&header1), offsetof(SqpackHeader, Sha1));

			auto& header2 = *reinterpret_cast<SqpackIndexHeader*>(&data[sizeof SqpackHeader]);
			std::sort(fileSegment.begin(), fileSegment.end());
			header2.HeaderSize = sizeof SqpackIndexHeader;
			header2.Type = IndexType;
			header2.HashLocatorSegment.Count = 1;
			header2.HashLocatorSegment.Offset = header1.HeaderSize + header2.HeaderSize;
			header2.HashLocatorSegment.Size = static_cast<uint32_t>(std::span(fileSegment).size_bytes());
			header2.TextLocatorSegment.Count = static_cast<uint32_t>(dataFilesCount);
			header2.TextLocatorSegment.Offset = header2.HashLocatorSegment.Offset + header2.HashLocatorSegment.Size;
			header2.TextLocatorSegment.Size = static_cast<uint32_t>(std::span(conflictSegment).size_bytes());
			header2.UnknownSegment3.Count = 0;
			header2.UnknownSegment3.Offset = header2.TextLocatorSegment.Offset + header2.TextLocatorSegment.Size;
			header2.UnknownSegment3.Size = static_cast<uint32_t>(std::span(segment3).size_bytes());
			header2.PathHashLocatorSegment.Count = 0;
			header2.PathHashLocatorSegment.Offset = header2.UnknownSegment3.Offset + header2.UnknownSegment3.Size;
			if constexpr (UseFolders) {
				for (size_t i = 0; i < fileSegment.size(); ++i) {
					const auto& entry = fileSegment[i];
					if (folderSegment.empty() || folderSegment.back().PathHash != entry.PathHash) {
						folderSegment.emplace_back(
							entry.PathHash,
							static_cast<uint32_t>(header2.HashLocatorSegment.Offset + i * sizeof entry),
							static_cast<uint32_t>(sizeof entry),
							0);
					} else {
						folderSegment.back().PairHashLocatorSize = folderSegment.back().PairHashLocatorSize + sizeof entry;
					}
				}
				header2.PathHashLocatorSegment.Size = static_cast<uint32_t>(std::span(folderSegment).size_bytes());
			}

			if (strict) {
				header2.Sha1.SetFromSpan(reinterpret_cast<char*>(&header2), offsetof(SqpackIndexHeader, Sha1));
				if (!fileSegment.empty())
					header2.HashLocatorSegment.Sha1.SetFromSpan(reinterpret_cast<const uint8_t*>(&fileSegment.front()), header2.HashLocatorSegment.Size);
				if (!conflictSegment.empty())
					header2.TextLocatorSegment.Sha1.SetFromSpan(reinterpret_cast<const uint8_t*>(&conflictSegment.front()), header2.TextLocatorSegment.Size);
				if (!segment3.empty())
					header2.UnknownSegment3.Sha1.SetFromSpan(reinterpret_cast<const uint8_t*>(&segment3.front()), header2.UnknownSegment3.Size);
				if constexpr (UseFolders) {
					if (!folderSegment.empty())
						header2.PathHashLocatorSegment.Sha1.SetFromSpan(reinterpret_cast<const uint8_t*>(&folderSegment.front()), header2.PathHashLocatorSegment.Size);
				}

			}
			if (!fileSegment.empty())
				data.insert(data.end(), reinterpret_cast<const uint8_t*>(&fileSegment.front()), reinterpret_cast<const uint8_t*>(&fileSegment.back() + 1));
			if (!conflictSegment.empty())
				data.insert(data.end(), reinterpret_cast<const uint8_t*>(&conflictSegment.front()), reinterpret_cast<const uint8_t*>(&conflictSegment.back() + 1));
			if (!segment3.empty())
				data.insert(data.end(), reinterpret_cast<const uint8_t*>(&segment3.front()), reinterpret_cast<const uint8_t*>(&segment3.back() + 1));

			if constexpr (UseFolders) {
				if (!folderSegment.empty())
					data.insert(data.end(), reinterpret_cast<const uint8_t*>(&folderSegment.front()), reinterpret_cast<const uint8_t*>(&folderSegment.back() + 1));
			}

			return data;
		}
	};

	class SqpackGenerator::DataViewStream : public DefaultAbstractStream {
		const std::vector<uint8_t> m_header;
		const std::span<Entry*> m_entries;

		const SqpackDataHeader& SubHeader() const {
			return *reinterpret_cast<const SqpackDataHeader*>(&m_header[sizeof SqpackHeader]);
		}

		static std::vector<uint8_t> Concat(const SqpackHeader& header, const SqpackDataHeader& subheader) {
			std::vector<uint8_t> buffer;
			buffer.reserve(sizeof header + sizeof subheader);
			buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&header), reinterpret_cast<const uint8_t*>(&header + 1));
			buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&subheader), reinterpret_cast<const uint8_t*>(&subheader + 1));
			return buffer;
		}

		mutable size_t m_lastAccessedEntryIndex = SIZE_MAX;
		const std::shared_ptr<SqpackViewEntryCache> m_buffer;

	public:
		DataViewStream(const SqpackHeader& header, const SqpackDataHeader& subheader, std::span<Entry*> entries, std::shared_ptr<SqpackViewEntryCache> buffer)
			: m_header(Concat(header, subheader))
			, m_entries(std::move(entries))
			, m_buffer(std::move(buffer)) {
		}

		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const override {
			if (!length)
				return 0;

			auto relativeOffset = static_cast<uint64_t>(offset);
			auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

			if (relativeOffset < m_header.size()) {
				const auto src = std::span(m_header).subspan(static_cast<size_t>(relativeOffset));
				const auto available = (std::min)(out.size_bytes(), src.size_bytes());
				std::copy_n(src.begin(), available, out.begin());
				out = out.subspan(available);
				relativeOffset = 0;
			} else
				relativeOffset -= m_header.size();

			if (out.empty())
				return length;

			auto it = m_lastAccessedEntryIndex != SIZE_MAX ? m_entries.begin() + m_lastAccessedEntryIndex : m_entries.begin();
			if (const auto absoluteOffset = relativeOffset + m_header.size();
				(*it)->Locator.DatFileOffset() > absoluteOffset || absoluteOffset >= (*it)->Locator.DatFileOffset() + (*it)->EntrySize) {
				it = std::ranges::lower_bound(m_entries, nullptr, [&](Entry* l, Entry* r) {
					const auto lo = l ? l->Locator.DatFileOffset() : absoluteOffset;
					const auto ro = r ? r->Locator.DatFileOffset() : absoluteOffset;
					return lo < ro;
					});
				if (it != m_entries.begin() && (it == m_entries.end() || (*it)->Locator.DatFileOffset() > absoluteOffset))
					--it;
			}

			if (it != m_entries.end()) {
				relativeOffset -= (*it)->Locator.DatFileOffset() - m_header.size();

				for (; it < m_entries.end(); ++it) {
					const auto& entry = **it;
					m_lastAccessedEntryIndex = it - m_entries.begin();

					const auto buf = m_buffer ? m_buffer->GetBuffer(this, &entry) : nullptr;

					if (relativeOffset < entry.EntrySize) {
						const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(entry.EntrySize - relativeOffset));
						if (buf)
							std::copy_n(&buf->Buffer()[static_cast<size_t>(relativeOffset)], available, &out[0]);
						else
							ReadStream(*entry.Provider, relativeOffset, out.data(), available);
						out = out.subspan(available);
						relativeOffset = 0;

						if (out.empty())
							break;
					} else
						relativeOffset -= entry.EntrySize;
				}
			}

			return length - out.size_bytes();
		}

		std::streamsize StreamSize() const override {
			return m_header.size() + SubHeader().DataSize;
		}

		void Flush() const override {
			if (m_buffer)
				m_buffer->Flush();
		}
	};
}

#endif
