#pragma once

#include "Sqex_Sqpack.h"
#include "Sqex_Sqpack_EntryProvider.h"
#include "Utils_ListenerManager.h"
#include "Utils_Win32_Handle.h"

namespace Sqex::ThirdParty::TexTools {
	struct TTMPL;
}

namespace Sqex::Sqpack {
	class Creator {
		const uint64_t m_maxFileSize;

	public:
		const std::string DatExpac;
		const std::string DatName;

	private:
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		Creator(std::string ex, std::string name, uint64_t maxFileSize = SqData::Header::MaxFileSize_MaxValue);
		~Creator();

		ListenerManager<Implementation, void, const std::string&> Log;

		struct AddEntryResult {
			std::vector<EntryProvider*> Added;
			std::vector<EntryProvider*> Replaced;
			std::vector<EntryProvider*> SkippedExisting;
			std::vector<std::pair<EntryPathSpec, std::string>> Error;

			AddEntryResult& operator+=(const AddEntryResult& r);
			AddEntryResult& operator+=(AddEntryResult&& r);
			[[nodiscard]] _Maybenull_ EntryProvider* AnyItem() const;
			[[nodiscard]] std::vector<EntryProvider*> AllSuccessfulEntries() const;
		};
		AddEntryResult AddEntriesFromSqPack(const std::filesystem::path& indexPath, bool overwriteExisting = true, bool overwriteUnknownSegments = false);
		AddEntryResult AddEntryFromFile(EntryPathSpec pathSpec, const std::filesystem::path& path, bool overwriteExisting = true);
		AddEntryResult AddAllEntriesFromSimpleTTMP(const std::filesystem::path& extractedDir, bool overwriteExisting = true);
		void ReserveSpacesFromTTMP(const ThirdParty::TexTools::TTMPL& ttmpl);
		AddEntryResult AddEntry(std::shared_ptr<EntryProvider> provider, bool overwriteExisting = true);
		void ReserveSwappableSpace(EntryPathSpec pathSpec, uint32_t size);

	private:
		class DataView;

	public:
		struct Entry {
			uint32_t DataFileIndex{};
			uint32_t EntrySize{};
			uint32_t PadSize{};
			SqIndex::LEDataLocator Locator{};

			uint32_t EntryReservedSize{};

			uint64_t OffsetAfterHeaders{};
			std::shared_ptr<EntryProvider> Provider;
		};

		struct SqpackViews {
			std::shared_ptr<Sqex::RandomAccessStream> Index1;
			std::shared_ptr<Sqex::RandomAccessStream> Index2;
			std::vector<std::shared_ptr<Sqex::RandomAccessStream>> Data;
			std::vector<Entry*> Entries;
			std::map<EntryPathSpec, std::unique_ptr<Entry>, EntryPathSpec::AllHashComparator> HashOnlyEntries;
			std::map<EntryPathSpec, std::unique_ptr<Entry>, EntryPathSpec::FullPathComparator> FullPathEntries;
		};

		class SqpackViewEntryCache {
			static constexpr auto SmallEntryBufferSize = (INTPTR_MAX == INT64_MAX ? 256 : 8) * 1048576;
			static constexpr auto LargeEntryBufferSizeMax = (INTPTR_MAX == INT64_MAX ? 1024 : 64) * 1048576;

		public:
			class BufferedEntry {
				const DataView* m_view = nullptr;
				const Entry* m_entry = nullptr;
				std::vector<uint8_t> m_bufferPreallocated;
				std::vector<uint8_t> m_bufferTemporary;
				std::span<uint8_t> m_bufferActive;

			public:
				bool Empty() const {
					return m_view == nullptr || m_entry == nullptr;
				}

				bool IsEntry(const DataView* view, const Entry* entry) const {
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

				void SetEntry(const DataView* view, const Entry* entry) {
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
					entry->Provider->ReadStream(0, m_bufferActive);
				}

				const auto& Buffer() const {
					return m_bufferActive;
				}
			};

		private:
			BufferedEntry m_lastActiveEntry;

		public:
			BufferedEntry* GetBuffer(const DataView* view, const Entry* entry);
			void Flush();
		};

		SqpackViews AsViews(bool strict, const std::shared_ptr<SqpackViewEntryCache>& buffer = nullptr);

		std::shared_ptr<RandomAccessStream> operator[](const EntryPathSpec& pathSpec) const;
		std::vector<EntryPathSpec> AllPathSpec() const;
	};

}
