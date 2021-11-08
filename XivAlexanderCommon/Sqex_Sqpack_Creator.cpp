#include "pch.h"
#include "Sqex_Sqpack_Creator.h"

#include "Sqex_Model.h"
#include "Sqex_Sqpack_EntryProvider.h"
#include "Sqex_Sqpack_EntryRawStream.h"
#include "Sqex_Sqpack_Reader.h"
#include "Sqex_ThirdParty_TexTools.h"

struct Sqex::Sqpack::Creator::Implementation {
	AddEntryResult AddEntry(std::shared_ptr<EntryProvider> provider, bool overwriteExisting = true);

	struct Entry {
		uint32_t DataFileIndex{};
		uint32_t EntrySize{};
		uint32_t PadSize{};
		SqIndex::LEDataLocator Locator{};

		uint32_t EntryReservedSize{};

		uint64_t OffsetAfterHeaders{};
		std::shared_ptr<EntryProvider> Provider;
	};

	Creator* const this_;

	std::map<EntryPathSpec, std::unique_ptr<Entry>, EntryPathSpec::AllHashComparator> m_hashOnlyEntries;
	std::map<EntryPathSpec, std::unique_ptr<Entry>, EntryPathSpec::FullPathComparator> m_fullEntries;

	std::vector<SqIndex::Segment3Entry> m_sqpackIndexSegment3;
	std::vector<SqIndex::Segment3Entry> m_sqpackIndex2Segment3;

	Implementation(Creator* this_)
		: this_(this_) {
	}

	virtual ~Implementation() = default;

	template<typename...Args>
	void Log(Args ...args) {
		if (this_->Log.Empty())
			return;

		this_->Log(std::format(std::forward<Args>(args)...));
	}
};

Sqex::Sqpack::Creator::Creator(std::string ex, std::string name, uint64_t maxFileSize)
	: m_maxFileSize(maxFileSize)
	, DatExpac(std::move(ex))
	, DatName(std::move(name))
	, m_pImpl(std::make_unique<Implementation>(this)) {
	if (maxFileSize > SqData::Header::MaxFileSize_MaxValue)
		throw std::invalid_argument("MaxFileSize cannot be more than 32GiB.");
}

Sqex::Sqpack::Creator::~Creator() = default;

Sqex::Sqpack::Creator::AddEntryResult& Sqex::Sqpack::Creator::AddEntryResult::operator+=(const AddEntryResult & r) {
	auto& k = r.Added;
	Added.insert(Added.end(), r.Added.begin(), r.Added.end());
	Replaced.insert(Replaced.end(), r.Replaced.begin(), r.Replaced.end());
	SkippedExisting.insert(SkippedExisting.end(), r.SkippedExisting.begin(), r.SkippedExisting.end());
	Error.insert(r.Error.begin(), r.Error.end());
	return *this;
}

Sqex::Sqpack::Creator::AddEntryResult& Sqex::Sqpack::Creator::AddEntryResult::operator+=(AddEntryResult && r) {
	auto& k = r.Added;
	Added.insert(Added.end(), r.Added.begin(), r.Added.end());
	Replaced.insert(Replaced.end(), r.Replaced.begin(), r.Replaced.end());
	SkippedExisting.insert(SkippedExisting.end(), r.SkippedExisting.begin(), r.SkippedExisting.end());
	Error.merge(r.Error);
	return *this;
}

_Maybenull_ Sqex::Sqpack::EntryProvider* Sqex::Sqpack::Creator::AddEntryResult::AnyItem() const {
	if (!Added.empty())
		return Added[0];
	if (!Replaced.empty())
		return Replaced[0];
	if (!SkippedExisting.empty())
		return SkippedExisting[0];
	return nullptr;
}

std::vector<Sqex::Sqpack::EntryProvider*> Sqex::Sqpack::Creator::AddEntryResult::AllSuccessfulEntries() const {
	std::vector<EntryProvider*> res;
	res.insert(res.end(), Added.begin(), Added.end());
	res.insert(res.end(), Replaced.begin(), Replaced.end());
	res.insert(res.end(), SkippedExisting.begin(), SkippedExisting.end());
	return res;
}

Sqex::Sqpack::Creator::AddEntryResult Sqex::Sqpack::Creator::Implementation::AddEntry(std::shared_ptr<EntryProvider> provider, bool overwriteExisting) {
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
				return { .SkippedExisting = { pEntry->Provider.get() } };
			}
			pEntry->Provider = std::move(provider);
			return { .Replaced = { pEntry->Provider.get() } };
		}

		auto entry = std::make_unique<Entry>(0, 0, 0, SqIndex::LEDataLocator{ 0, 0 }, 0, 0, std::move(provider));
		if (pProvider->PathSpec().HasOriginal())
			m_fullEntries.emplace(pProvider->PathSpec(), std::move(entry));
		else
			m_hashOnlyEntries.emplace(pProvider->PathSpec(), std::move(entry));
		return { .Added = { pProvider } };
	} catch (const std::exception& e) {
		return { .Error = { { pProvider->PathSpec(), std::string(e.what()) } } };
	}
}

Sqex::Sqpack::Creator::AddEntryResult Sqex::Sqpack::Creator::AddEntriesFromSqPack(const std::filesystem::path & indexPath, bool overwriteExisting, bool overwriteUnknownSegments) {
	Reader reader{ indexPath, false };

	AddEntryResult result{};

	if (overwriteUnknownSegments) {
		m_pImpl->m_sqpackIndexSegment3 = std::move(reader.Index.Segment3);
		m_pImpl->m_sqpackIndex2Segment3 = std::move(reader.Index2.Segment3);
	}

	std::map<uint32_t, EntryPathSpec> pathSpecs;
	for (const auto& entry : reader.Index.Files) {
		if (!entry.Locator.HasConflicts()) {
			auto& spec = pathSpecs[entry.Locator.Value()];
			spec.PathHash = entry.PathHash;
			spec.NameHash = entry.NameHash;
		}
	}
	for (const auto& entry : reader.Index.HashConflictSegment) {
		if (entry.NameHash == SqIndex::HashConflictSegmentEntry::EndOfList
			&& entry.PathHash == SqIndex::HashConflictSegmentEntry::EndOfList
			&& entry.ConflictIndex == SqIndex::HashConflictSegmentEntry::EndOfList)
			break;
		pathSpecs[entry.Locator.Value()] = EntryPathSpec(entry.FullPath);
	}
	for (const auto& entry : reader.Index2.Files) {
		if (!entry.Locator.HasConflicts()) {
			pathSpecs[entry.Locator.Value()].FullPathHash = entry.FullPathHash;
		}
	}
	for (const auto& entry : reader.Index2.HashConflictSegment) {
		if (entry.FullPathHash == SqIndex::HashConflictSegmentEntry::EndOfList
			&& entry.UnusedHash == SqIndex::HashConflictSegmentEntry::EndOfList
			&& entry.ConflictIndex == SqIndex::HashConflictSegmentEntry::EndOfList)
			break;
		pathSpecs[entry.Locator.Value()] = EntryPathSpec(entry.FullPath);
	}

	for (auto& [locatorValue, pathSpec] : pathSpecs) {
		const auto& locator = *reinterpret_cast<const SqIndex::LEDataLocator*>(&locatorValue);
		try {
			result += m_pImpl->AddEntry(
				reader.GetEntryProvider(pathSpec, locator),
				overwriteExisting);
		} catch (const std::exception& e) {
			result.Error.emplace(std::move(pathSpec), std::string(e.what()));
		}
	}

	return result;
}

Sqex::Sqpack::Creator::AddEntryResult Sqex::Sqpack::Creator::AddEntryFromFile(EntryPathSpec pathSpec, const std::filesystem::path & path, bool overwriteExisting) {
	std::shared_ptr<EntryProvider> provider;
	auto extensionLower = path.extension().wstring();
	CharLowerW(&extensionLower[0]);
	if (file_size(path) == 0) {
		provider = std::make_shared<EmptyEntryProvider>(std::move(pathSpec));
	} else if (extensionLower == L".tex") {
		provider = std::make_shared<OnTheFlyTextureEntryProvider>(std::move(pathSpec), path);
	} else if (extensionLower == L".mdl") {
		provider = std::make_shared<OnTheFlyModelEntryProvider>(std::move(pathSpec), path);
	} else {
		provider = std::make_shared<OnTheFlyBinaryEntryProvider>(std::move(pathSpec), path);
	}
	return m_pImpl->AddEntry(provider, overwriteExisting);
}

Sqex::Sqpack::Creator::AddEntryResult Sqex::Sqpack::Creator::AddEntriesFromTTMP(const std::filesystem::path & extractedDir, bool overwriteExisting) {
	nlohmann::json choices;
	const auto ttmpdPath = extractedDir / "TTMPD.mpd";
	const auto ttmpl = ThirdParty::TexTools::TTMPL::FromStream(FileRandomAccessStream{
		Win32::Handle::FromCreateFile(
			extractedDir / "TTMPL.mpl", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0
	)
		});

	if (const auto choicesPath = extractedDir / "choices.json"; exists(choicesPath)) {
		try {
			choices = ParseJsonFromFile(choicesPath);
			m_pImpl->Log("Choices file loaded from {}", choicesPath.wstring());
		} catch (const std::exception& e) {
			m_pImpl->Log("Failed to load choices from {}: {}", choicesPath.wstring(), e.what());
		}
	}

	if (DatExpac != "ffxiv")
		return {};

	AddEntryResult addEntryResult{};

	const auto dataStream = std::make_shared<FileRandomAccessStream>(Win32::Handle::FromCreateFile(ttmpdPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING));

	for (size_t i = 0; i < ttmpl.SimpleModsList.size(); ++i) {
		const auto& entry = ttmpl.SimpleModsList[i];
		if (choices.is_array() && i < choices.size() && choices[i].is_boolean() && !choices[i].get<boolean>())
			continue;
		if (entry.DatFile != DatName)
			continue;

		try {
			addEntryResult += m_pImpl->AddEntry(std::make_shared<RandomAccessStreamAsEntryProviderView>(entry.FullPath, dataStream, entry.ModOffset, entry.ModSize), overwriteExisting);
		} catch (const std::exception& e) {
			addEntryResult.Error.emplace(EntryPathSpec{ entry.FullPath }, std::string(e.what()));
			m_pImpl->Log("Error: {} (Name: {} > {})", entry.FullPath, ttmpl.Name, entry.Name);
		}
	}

	for (size_t pageObjectIndex = 0; pageObjectIndex < ttmpl.ModPackPages.size(); ++pageObjectIndex) {
		const auto& modGroups = ttmpl.ModPackPages[pageObjectIndex].ModGroups;
		if (modGroups.empty())
			continue;
		const auto pageConf = choices.is_array() && pageObjectIndex < choices.size() && choices[pageObjectIndex].is_array() ? choices[pageObjectIndex] : nlohmann::json::array();

		for (size_t modGroupIndex = 0; modGroupIndex < modGroups.size(); ++modGroupIndex) {
			const auto& modGroup = modGroups[modGroupIndex];
			if (modGroups.empty())
				continue;

			const auto choice = modGroupIndex < pageConf.size() ? std::max(0, std::min(static_cast<int>(modGroup.OptionList.size() - 1), pageConf[modGroupIndex].get<int>())) : 0;
			const auto& option = modGroup.OptionList[choice];

			for (const auto& entry : option.ModsJsons) {
				if (entry.DatFile != DatName)
					continue;

				try {
					addEntryResult += m_pImpl->AddEntry(std::make_shared<RandomAccessStreamAsEntryProviderView>(entry.FullPath, dataStream, entry.ModOffset, entry.ModSize), overwriteExisting);
				} catch (const std::exception& e) {
					addEntryResult.Error.emplace(EntryPathSpec{ entry.FullPath }, std::string(e.what()));
					m_pImpl->Log("Error: {} (Name: {} > {})", entry.FullPath, ttmpl.Name, entry.Name);
				}
			}
		}
	}
	return addEntryResult;
}

void Sqex::Sqpack::Creator::ReserveSpacesFromTTMP(const ThirdParty::TexTools::TTMPL & ttmpl) {
	if (DatExpac != "ffxiv")
		return;

	for (const auto& entry : ttmpl.SimpleModsList) {
		if (entry.DatFile != DatName || entry.ModSize > UINT32_MAX)
			continue;

		ReserveSwappableSpace(entry.FullPath, static_cast<uint32_t>(entry.ModSize));
	}
	for (const auto& modPackPage : ttmpl.ModPackPages) {
		for (const auto& modGroup : modPackPage.ModGroups) {
			for (const auto& option : modGroup.OptionList) {
				for (const auto& entry : option.ModsJsons) {
					if (entry.DatFile != DatName || entry.ModSize > UINT32_MAX)
						continue;

					ReserveSwappableSpace(entry.FullPath, static_cast<uint32_t>(entry.ModSize));
				}
			}
		}
	}
}

Sqex::Sqpack::Creator::AddEntryResult Sqex::Sqpack::Creator::AddEntry(std::shared_ptr<EntryProvider> provider, bool overwriteExisting) {
	return m_pImpl->AddEntry(std::move(provider), overwriteExisting);
}

void Sqex::Sqpack::Creator::ReserveSwappableSpace(EntryPathSpec pathSpec, uint32_t size) {
	if (const auto it = m_pImpl->m_hashOnlyEntries.find(pathSpec); it != m_pImpl->m_hashOnlyEntries.end()) {
		it->second->EntryReservedSize = std::max(it->second->EntryReservedSize, size);
		if (!it->second->Provider->PathSpec().HasOriginal() && pathSpec.HasOriginal()) {
			it->second->Provider->UpdatePathSpec(pathSpec);
			m_pImpl->m_fullEntries.emplace(pathSpec, std::move(it->second));
			m_pImpl->m_hashOnlyEntries.erase(it);
		}
	} else if (const auto it = m_pImpl->m_fullEntries.find(pathSpec); it != m_pImpl->m_fullEntries.end()) {
		it->second->EntryReservedSize = std::max(it->second->EntryReservedSize, size);
	} else {
		auto entry = std::make_unique<Implementation::Entry>(0, 0, 0, SqIndex::LEDataLocator{ 0, 0 }, size, 0, std::make_shared<EmptyEntryProvider>(std::move(pathSpec)));
		if (entry->Provider->PathSpec().HasOriginal())
			m_pImpl->m_fullEntries.emplace(entry->Provider->PathSpec(), std::move(entry));
		else
			m_pImpl->m_hashOnlyEntries.emplace(entry->Provider->PathSpec(), std::move(entry));
	}
}

template<Sqex::Sqpack::SqIndex::Header::IndexType IndexType, typename FileEntryType, typename ConflictEntryType, bool UseFolders>
class Sqex::Sqpack::Creator::IndexViewBase : public RandomAccessStream {
	std::vector<uint8_t> m_data;

public:
	IndexViewBase(size_t dataFilesCount, std::vector<FileEntryType> fileSegment, const std::vector<ConflictEntryType>& conflictSegment, const std::vector<SqIndex::Segment3Entry>& segment3, std::vector<SqIndex::FolderSegmentEntry> folderSegment = {}, bool strict = false) {

		std::sort(fileSegment.begin(), fileSegment.end());

		m_data.resize(sizeof SqpackHeader + sizeof SqIndex::Header);
		{
			auto& m_header = *reinterpret_cast<SqpackHeader*>(&m_data[0]);
			memcpy(m_header.Signature, SqpackHeader::Signature_Value, sizeof SqpackHeader::Signature_Value);
			m_header.HeaderSize = sizeof SqpackHeader;
			m_header.Unknown1 = SqpackHeader::Unknown1_Value;
			m_header.Type = SqpackType::SqIndex;
			m_header.Unknown2 = SqpackHeader::Unknown2_Value;
			if (strict)
				m_header.Sha1.SetFromSpan(reinterpret_cast<char*>(&m_header), offsetof(SqpackHeader, Sha1));

			auto& m_subheader = *reinterpret_cast<SqIndex::Header*>(&m_data[sizeof SqpackHeader]);
			m_subheader.HeaderSize = sizeof SqIndex::Header;
			m_subheader.Type = IndexType;
			m_subheader.FileSegment.Count = 1;
			m_subheader.FileSegment.Offset = m_header.HeaderSize + m_subheader.HeaderSize;
			m_subheader.FileSegment.Size = static_cast<uint32_t>(std::span(fileSegment).size_bytes());
			m_subheader.HashConflictSegment.Count = static_cast<uint32_t>(dataFilesCount);
			m_subheader.HashConflictSegment.Offset = m_subheader.FileSegment.Offset + m_subheader.FileSegment.Size;
			m_subheader.HashConflictSegment.Size = static_cast<uint32_t>(std::span(conflictSegment).size_bytes());
			m_subheader.UnknownSegment3.Count = 0;
			m_subheader.UnknownSegment3.Offset = m_subheader.HashConflictSegment.Offset + m_subheader.HashConflictSegment.Size;
			m_subheader.UnknownSegment3.Size = static_cast<uint32_t>(std::span(segment3).size_bytes());
			m_subheader.FolderSegment.Count = 0;
			m_subheader.FolderSegment.Offset = m_subheader.UnknownSegment3.Offset + m_subheader.UnknownSegment3.Size;
			if constexpr (UseFolders) {
				for (size_t i = 0; i < fileSegment.size(); ++i) {
					const auto& entry = fileSegment[i];
					if (folderSegment.empty() || folderSegment.back().PathHash != entry.PathHash) {
						folderSegment.emplace_back(
							entry.PathHash,
							static_cast<uint32_t>(m_subheader.FileSegment.Offset + i * sizeof entry),
							static_cast<uint32_t>(sizeof entry),
							0);
					} else {
						folderSegment.back().FileSegmentSize = folderSegment.back().FileSegmentSize + sizeof entry;
					}
				}
				m_subheader.FolderSegment.Size = static_cast<uint32_t>(std::span(folderSegment).size_bytes());
			}

			if (strict) {
				m_subheader.Sha1.SetFromSpan(reinterpret_cast<char*>(&m_subheader), offsetof(Sqpack::SqIndex::Header, Sha1));
				if (!fileSegment.empty())
					m_subheader.FileSegment.Sha1.SetFromSpan(reinterpret_cast<const uint8_t*>(&fileSegment.front()), m_subheader.FileSegment.Size);
				if (!conflictSegment.empty())
					m_subheader.HashConflictSegment.Sha1.SetFromSpan(reinterpret_cast<const uint8_t*>(&conflictSegment.front()), m_subheader.HashConflictSegment.Size);
				if (!segment3.empty())
					m_subheader.UnknownSegment3.Sha1.SetFromSpan(reinterpret_cast<const uint8_t*>(&segment3.front()), m_subheader.UnknownSegment3.Size);
				if constexpr (UseFolders) {
					if (!folderSegment.empty())
						m_subheader.FolderSegment.Sha1.SetFromSpan(reinterpret_cast<const uint8_t*>(&folderSegment.front()), m_subheader.FolderSegment.Size);
				}
			}
		}
		if (!fileSegment.empty())
			m_data.insert(m_data.end(), reinterpret_cast<const uint8_t*>(&fileSegment.front()), reinterpret_cast<const uint8_t*>(&fileSegment.back() + 1));
		if (!conflictSegment.empty())
			m_data.insert(m_data.end(), reinterpret_cast<const uint8_t*>(&conflictSegment.front()), reinterpret_cast<const uint8_t*>(&conflictSegment.back() + 1));
		if (!segment3.empty())
			m_data.insert(m_data.end(), reinterpret_cast<const uint8_t*>(&segment3.front()), reinterpret_cast<const uint8_t*>(&segment3.back() + 1));

		if constexpr (UseFolders) {
			if (!folderSegment.empty())
				m_data.insert(m_data.end(), reinterpret_cast<const uint8_t*>(&folderSegment.front()), reinterpret_cast<const uint8_t*>(&folderSegment.back() + 1));
		}
	}

	uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override {
		if (!length)
			return 0;

		const auto available = static_cast<size_t>(std::min(length, m_data.size() - offset));
		std::copy_n(&m_data[static_cast<size_t>(offset)], available, static_cast<uint8_t*>(buf));
		return available;
	}

	uint64_t StreamSize() const override {
		return m_data.size();
	}
};

class Sqex::Sqpack::Creator::DataView : public RandomAccessStream {
	const std::vector<uint8_t> m_header;
	const std::vector<std::unique_ptr<const Implementation::Entry>> m_entries;

	const SqData::Header& SubHeader() const {
		return *reinterpret_cast<const SqData::Header*>(&m_header[sizeof SqpackHeader]);
	}

	static std::vector<uint8_t> Concat(const SqpackHeader& header, const SqData::Header& subheader) {
		std::vector<uint8_t> buffer;
		buffer.reserve(sizeof header + sizeof subheader);
		buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&header), reinterpret_cast<const uint8_t*>(&header + 1));
		buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&subheader), reinterpret_cast<const uint8_t*>(&subheader + 1));
		return buffer;
	}

	mutable uint64_t m_nLastRequestedOffset = 0;
	mutable uint64_t m_nLastRequestedSize = 0;
	mutable std::vector<std::tuple<EntryProvider*, uint64_t, uint64_t>> m_pLastEntryProviders;

public:
	DataView(const SqpackHeader& header, const SqData::Header& subheader, std::vector<std::unique_ptr<const Implementation::Entry>> entries)
		: m_header(Concat(header, subheader))
		, m_entries(std::move(entries)) {
	}

	uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override {
		m_pLastEntryProviders.clear();
		m_nLastRequestedOffset = offset;
		m_nLastRequestedSize = length;
		if (!length)
			return 0;

		auto relativeOffset = offset;
		auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

		if (relativeOffset < m_header.size()) {
			const auto src = std::span(m_header).subspan(static_cast<size_t>(relativeOffset));
			const auto available = std::min(out.size_bytes(), src.size_bytes());
			std::copy_n(src.begin(), available, out.begin());
			out = out.subspan(available);
			relativeOffset = 0;
		} else
			relativeOffset -= m_header.size();

		if (out.empty()) return length;

		auto it = std::ranges::lower_bound(m_entries, nullptr, [&](const std::unique_ptr<const Implementation::Entry>& l, const std::unique_ptr<const Implementation::Entry>& r) {
			const auto lo = l ? l->OffsetAfterHeaders : relativeOffset;
			const auto ro = r ? r->OffsetAfterHeaders : relativeOffset;
			return lo < ro;
		});
		if (it != m_entries.begin() && (it == m_entries.end() || it->get()->OffsetAfterHeaders > relativeOffset))
			--it;

		if (it != m_entries.end()) {
			relativeOffset -= it->get()->OffsetAfterHeaders;

			for (; it < m_entries.end(); ++it) {
				const auto& entry = *it->get();

				if (relativeOffset < entry.EntrySize) {
					const auto available = std::min(out.size_bytes(), static_cast<size_t>(entry.EntrySize - relativeOffset));
					m_pLastEntryProviders.emplace_back(std::make_tuple(entry.Provider.get(), relativeOffset, available));
					entry.Provider->ReadStream(relativeOffset, out.data(), available);
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty())
						break;
				} else
					relativeOffset -= entry.EntrySize;

				if (relativeOffset < entry.PadSize) {
					const auto available = std::min(out.size_bytes(), static_cast<size_t>(entry.PadSize - relativeOffset));
					std::fill_n(out.begin(), available, 0);
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty())
						break;
				} else
					relativeOffset -= entry.PadSize;
			}
		}

		return length - out.size_bytes();
	}

	uint64_t StreamSize() const override {
		return m_header.size() + SubHeader().DataSize;
	}

	std::string DescribeState() const override {
		auto res = std::format("Sqpack::Creator::DataView({}->{})", m_nLastRequestedOffset, m_nLastRequestedSize);
		for (const auto& [p, off, len] : m_pLastEntryProviders) {
			res += std::format(" [{}: {}->{}: {}]", p->PathSpec(), off, len, p->DescribeState());
		}
		return res;
	}
};

Sqex::Sqpack::Creator::SqpackViews Sqex::Sqpack::Creator::AsViews(bool strict) {
	SqpackHeader dataHeader{};
	std::vector<SqData::Header> dataSubheaders;
	std::vector<std::vector<std::unique_ptr<const Implementation::Entry>>> dataEntries;

	std::map<std::pair<uint32_t, uint32_t>, std::set<EntryPathSpec, EntryPathSpec::PairHashComparator>> pairHashes;
	std::map<uint32_t, std::set<EntryPathSpec, EntryPathSpec::FullHashComparator>> fullHashes;

	std::map<EntryPathSpec, Implementation::Entry*, EntryPathSpec::FullComparator> entryFullMap;
	{
		std::vector<std::unique_ptr<Implementation::Entry>> entries;
		for (auto& entry : m_pImpl->m_fullEntries | std::views::values) {
			entryFullMap.emplace(entry->Provider->PathSpec(), entry.get());
			entries.emplace_back(std::move(entry));
		}
		for (auto& entry : m_pImpl->m_hashOnlyEntries | std::views::values) {
			entryFullMap.emplace(entry->Provider->PathSpec(), entry.get());
			entries.emplace_back(std::move(entry));
		}
		m_pImpl->m_fullEntries.clear();
		m_pImpl->m_hashOnlyEntries.clear();

		for (auto& entry : entries) {
			const auto& pathSpec = entry->Provider->PathSpec();
			entry->EntrySize = Align(std::max(entry->EntryReservedSize, static_cast<uint32_t>(entry->Provider->StreamSize()))).Alloc;
			entry->PadSize = 0;
			entry->Provider = std::make_shared<HotSwappableEntryProvider>(pathSpec, entry->EntrySize, std::move(entry->Provider));

			pairHashes[std::make_pair(pathSpec.PathHash, pathSpec.NameHash)].insert(pathSpec);
			fullHashes[pathSpec.FullPathHash].insert(pathSpec);

			if (dataSubheaders.empty() ||
				sizeof SqpackHeader + sizeof SqData::Header + dataSubheaders.back().DataSize + entry->EntrySize + entry->PadSize > dataSubheaders.back().MaxFileSize) {
				if (strict && !dataSubheaders.empty()) {
					CryptoPP::SHA1 sha1;
					for (auto& entry : dataEntries.back()) {
						const auto& provider = *entry->Provider;
						const auto length = provider.StreamSize();
						uint8_t buf[4096];
						for (uint64_t i = 0; i < length; i += sizeof buf) {
							const auto readlen = static_cast<size_t>(std::min<uint64_t>(sizeof buf, length - i));
							provider.ReadStream(i, buf, readlen);
							sha1.Update(buf, readlen);
						}
					}
					sha1.Final(reinterpret_cast<byte*>(dataSubheaders.back().DataSha1.Value));
					dataSubheaders.back().Sha1.SetFromSpan(reinterpret_cast<char*>(&dataSubheaders.back()), offsetof(Sqpack::SqData::Header, Sha1));
				}
				dataSubheaders.emplace_back(SqData::Header{
					.HeaderSize = sizeof SqData::Header,
					.Unknown1 = SqData::Header::Unknown1_Value,
					.DataSize = 0,
					.SpanIndex = static_cast<uint32_t>(dataSubheaders.size()),
					.MaxFileSize = m_maxFileSize,
					});
				dataEntries.emplace_back();
			}

			entry->DataFileIndex = static_cast<uint32_t>(dataSubheaders.size() - 1);
			entry->OffsetAfterHeaders = dataSubheaders.back().DataSize;
			entry->Locator = { entry->DataFileIndex, sizeof SqpackHeader + sizeof SqData::Header + entry->OffsetAfterHeaders };

			dataSubheaders.back().DataSize = dataSubheaders.back().DataSize + entry->EntrySize + entry->PadSize;
			dataEntries.back().emplace_back(std::move(entry));
		}
	}

	SqIndex::LEDataLocator conflictEntryLocator{};
	conflictEntryLocator.HasConflicts(true);
	std::vector<SqIndex::FileSegmentEntry> fileEntries1;
	std::vector<SqIndex::HashConflictSegmentEntry> conflictEntries1;
	for (const auto& [pairHash, pathSpecs] : pairHashes) {
		if (pathSpecs.size() == 1) {
			fileEntries1.emplace_back(SqIndex::FileSegmentEntry{ pairHash.second, pairHash.first, entryFullMap.at(*pathSpecs.begin())->Locator, 0 });
		} else {
			fileEntries1.emplace_back(SqIndex::FileSegmentEntry{ pairHash.second, pairHash.first, conflictEntryLocator, 0 });
			uint32_t i = 0;
			for (const auto& pathSpec : pathSpecs) {
				conflictEntries1.emplace_back(SqIndex::HashConflictSegmentEntry{
					.NameHash = pairHash.second,
					.PathHash = pairHash.first,
					.Locator = entryFullMap.at(pathSpec)->Locator,
					.ConflictIndex = i++,
					});
				const auto path = Utils::ToUtf8(pathSpec.Original.wstring());
				strncpy_s(conflictEntries1.back().FullPath, path.c_str(), path.size());
			}
		}
	}
	conflictEntries1.emplace_back(SqIndex::HashConflictSegmentEntry{
		.NameHash = SqIndex::HashConflictSegmentEntry::EndOfList,
		.PathHash = SqIndex::HashConflictSegmentEntry::EndOfList,
		.Locator = 0,
		.ConflictIndex = SqIndex::HashConflictSegmentEntry::EndOfList,
		});

	std::vector<SqIndex::FileSegmentEntry2> fileEntries2;
	std::vector<SqIndex::HashConflictSegmentEntry2> conflictEntries2;
	for (const auto& [fullHash, pathSpecs] : fullHashes) {
		if (pathSpecs.size() == 1) {
			fileEntries2.emplace_back(SqIndex::FileSegmentEntry2{ fullHash, entryFullMap.at(*pathSpecs.begin())->Locator });
		} else {
			fileEntries2.emplace_back(SqIndex::FileSegmentEntry2{ fullHash, conflictEntryLocator });
			uint32_t i = 0;
			for (const auto& pathSpec : pathSpecs) {
				conflictEntries2.emplace_back(SqIndex::HashConflictSegmentEntry2{
					.FullPathHash = fullHash,
					.UnusedHash = 0,
					.Locator = entryFullMap.at(pathSpec)->Locator,
					.ConflictIndex = i++,
					});
				const auto path = Utils::ToUtf8(pathSpec.Original.wstring());
				strncpy_s(conflictEntries2.back().FullPath, path.c_str(), path.size());
			}
		}
	}
	conflictEntries2.emplace_back(SqIndex::HashConflictSegmentEntry2{
		.FullPathHash = SqIndex::HashConflictSegmentEntry2::EndOfList,
		.UnusedHash = SqIndex::HashConflictSegmentEntry2::EndOfList,
		.Locator = 0,
		.ConflictIndex = SqIndex::HashConflictSegmentEntry2::EndOfList,
		});

	memcpy(dataHeader.Signature, SqpackHeader::Signature_Value, sizeof SqpackHeader::Signature_Value);
	dataHeader.HeaderSize = sizeof SqpackHeader;
	dataHeader.Unknown1 = SqpackHeader::Unknown1_Value;
	dataHeader.Type = SqpackType::SqData;
	dataHeader.Unknown2 = SqpackHeader::Unknown2_Value;
	if (strict)
		dataHeader.Sha1.SetFromSpan(reinterpret_cast<char*>(&dataHeader), offsetof(SqpackHeader, Sha1));

	auto res = SqpackViews{
		.Index = std::make_shared<Index1View>(dataSubheaders.size(), std::move(fileEntries1), std::move(conflictEntries1), m_pImpl->m_sqpackIndexSegment3, std::vector<SqIndex::FolderSegmentEntry>(), strict),
		.Index2 = std::make_shared<Index2View>(dataSubheaders.size(), std::move(fileEntries2), std::move(conflictEntries2), m_pImpl->m_sqpackIndex2Segment3, std::vector<SqIndex::FolderSegmentEntry>(), strict),
	};

	for (size_t i = 0; i < dataSubheaders.size(); ++i) {
		res.Data.emplace_back(std::make_shared<DataView>(dataHeader, dataSubheaders[i], std::move(dataEntries[i])));
	}

	for (auto& [pathSpec, entry] : entryFullMap) {
		if (pathSpec.HasOriginal())
			res.FullProviders.emplace(pathSpec, entry->Provider.get());
		else
			res.HashOnlyProviders.emplace(pathSpec, entry->Provider.get());
	}

	return res;
}

std::shared_ptr<Sqex::RandomAccessStream> Sqex::Sqpack::Creator::operator[](const EntryPathSpec& pathSpec) const {
	if (const auto it = m_pImpl->m_hashOnlyEntries.find(pathSpec); it != m_pImpl->m_hashOnlyEntries.end())
		return std::make_shared<BufferedRandomAccessStream>(std::make_shared<EntryRawStream>(it->second->Provider));
	if (const auto it = m_pImpl->m_fullEntries.find(pathSpec); it != m_pImpl->m_fullEntries.end())
		return std::make_shared<BufferedRandomAccessStream>(std::make_shared<EntryRawStream>(it->second->Provider));
	throw std::out_of_range(std::format("PathSpec({}) not found", pathSpec));
}

std::vector<Sqex::Sqpack::EntryPathSpec> Sqex::Sqpack::Creator::AllPathSpec() const {
	std::vector<EntryPathSpec> res;
	res.reserve(m_pImpl->m_hashOnlyEntries.size() + m_pImpl->m_fullEntries.size());
	for (const auto& entry : m_pImpl->m_hashOnlyEntries | std::views::keys)
		res.emplace_back(entry);
	for (const auto& entry : m_pImpl->m_fullEntries | std::views::keys)
		res.emplace_back(entry);
	return res;
}
