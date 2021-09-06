#include "pch.h"
#include "Sqex_Sqpack_Creator.h"

#include <fstream>

#include "Sqex_Model.h"
#include "Sqex_Sqpack_EntryProvider.h"
#include "Sqex_Sqpack_Reader.h"
#include "Sqex_ThirdParty_TexTools.h"

struct Sqex::Sqpack::Creator::Implementation {
	AddEntryResult AddEntry(std::shared_ptr<EntryProvider> provider, size_t underlyingFileIndex = SIZE_MAX, bool overwriteExisting = true);

	struct Entry {
		uint32_t DataFileIndex;
		uint32_t BlockSize;
		uint32_t PadSize;
		SqIndex::LEDataLocator Locator;

		uint64_t OffsetAfterHeaders;
		std::shared_ptr<EntryProvider> Provider;
		size_t UnderlyingFileIndex;
	};

	Creator* const this_;

	std::vector<std::unique_ptr<Entry>> m_entries;
	std::map<std::pair<uint32_t, uint32_t>, Entry*> m_pathNameTupleEntryPointerMap;
	std::map<uint32_t, Entry*> m_fullPathEntryPointerMap;

	std::vector<Utils::Win32::File> m_openFiles;

	std::vector<char> m_sqpackIndexSegment2;
	std::vector<SqIndex::Segment3Entry> m_sqpackIndexSegment3;
	std::vector<char> m_sqpackIndex2Segment2;
	std::vector<SqIndex::Segment3Entry> m_sqpackIndex2Segment3;

	Implementation(Creator* this_)
		: this_(this_) {
	}

	virtual ~Implementation() = default;

	template<typename...Args>
	void Log(Args...args) {
		if (this_->Log.Empty())
			return;

		this_->Log(std::format(std::forward<Args>(args)...));
	}
	
	size_t OpenFile(
		_In_opt_ std::filesystem::path curItemPath,
		_In_opt_ Utils::Win32::File alreadyOpenedFile = {});
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

size_t Sqex::Sqpack::Creator::Implementation::OpenFile(
	_In_opt_ std::filesystem::path curItemPath,
	_In_opt_ Utils::Win32::File alreadyOpenedFile
) {
	if (curItemPath.empty()) {
		if (!alreadyOpenedFile)
			throw std::invalid_argument("curItemPath and alreadyOpenedFile cannot both be empty");
		else
			curItemPath = alreadyOpenedFile.ResolveName();
	}

	size_t found;
	for (found = 0; found < m_openFiles.size(); ++found) {
		if (equivalent(m_openFiles[found].ResolveName(), curItemPath)) {
			break;
		}
	}
	if (found == m_openFiles.size()) {
		if (alreadyOpenedFile) {
			if (!alreadyOpenedFile.HasOwnership())
				alreadyOpenedFile = Utils::Win32::File::DuplicateFrom<Utils::Win32::File>(alreadyOpenedFile);
		} else
			alreadyOpenedFile = Utils::Win32::File::Create(curItemPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);

		m_openFiles.emplace_back(std::move(alreadyOpenedFile));
	}

	return found;
}

Sqex::Sqpack::Creator::AddEntryResult& Sqex::Sqpack::Creator::AddEntryResult::operator+=(const AddEntryResult & r) {
	auto& k = r.Added;
	Added.insert(Added.end(), r.Added.begin(), r.Added.end());
	Replaced.insert(Replaced.end(), r.Replaced.begin(), r.Replaced.end());
	SkippedExisting.insert(SkippedExisting.end(), r.SkippedExisting.begin(), r.SkippedExisting.end());
	return *this;
}

Sqex::Sqpack::EntryProvider* Sqex::Sqpack::Creator::AddEntryResult::AnyItem() const {
	if (!Added.empty())
		return Added[0];
	if (!Replaced.empty())
		return Replaced[0];
	if (!SkippedExisting.empty())
		return SkippedExisting[0];
	return nullptr;
}

std::vector<Sqex::Sqpack::EntryProvider*> Sqex::Sqpack::Creator::AddEntryResult::AllEntries() const {
	std::vector<EntryProvider*> res;
	res.insert(res.end(), Added.begin(), Added.end());
	res.insert(res.end(), Replaced.begin(), Replaced.end());
	res.insert(res.end(), SkippedExisting.begin(), SkippedExisting.end());
	return res;
}

Sqex::Sqpack::Creator::AddEntryResult Sqex::Sqpack::Creator::Implementation::AddEntry(std::shared_ptr<EntryProvider> provider, size_t underlyingFileIndex, bool overwriteExisting) {
	if (provider->PathSpec().HasComponentHash()) {
		const auto it = m_pathNameTupleEntryPointerMap.find(std::make_pair(provider->PathSpec().PathHash, provider->PathSpec().NameHash));
		if (it != m_pathNameTupleEntryPointerMap.end()) {
			if (!overwriteExisting) {
				it->second->Provider->UpdatePathSpec(provider->PathSpec());
				return { .SkippedExisting = {it->second->Provider.get()} };
			}
			it->second->Provider = std::move(provider);
			it->second->UnderlyingFileIndex = underlyingFileIndex;
			return { .Replaced = {it->second->Provider.get()} };
		}
	}
	if (provider->PathSpec().FullPathHash != EntryPathSpec::EmptyHashValue) {
		const auto it = m_fullPathEntryPointerMap.find(provider->PathSpec().FullPathHash);
		if (it != m_fullPathEntryPointerMap.end()) {
			if (!overwriteExisting) {
				it->second->Provider->UpdatePathSpec(provider->PathSpec());
				return { .SkippedExisting = {it->second->Provider.get()} };
			}
			it->second->Provider = std::move(provider);
			it->second->UnderlyingFileIndex = underlyingFileIndex;
			return { .Replaced = {it->second->Provider.get()} };
		}
	}

	const auto pProvider = provider.get();
	auto entry = std::make_unique<Entry>(0, 0, 0, 0, SqIndex::LEDataLocator{ 0, 0 }, std::move(provider), underlyingFileIndex);
	if (entry->Provider->PathSpec().HasFullPathHash())
		m_fullPathEntryPointerMap.insert_or_assign(entry->Provider->PathSpec().FullPathHash, entry.get());
	if (entry->Provider->PathSpec().HasComponentHash())
		m_pathNameTupleEntryPointerMap.insert_or_assign(std::make_pair(entry->Provider->PathSpec().PathHash, entry->Provider->PathSpec().NameHash), entry.get());
	m_entries.emplace_back(std::move(entry));
	return { .Added = {pProvider} };
}

Sqex::Sqpack::Creator::AddEntryResult Sqex::Sqpack::Creator::AddEntriesFromSqPack(const std::filesystem::path & indexPath, bool overwriteExisting, bool overwriteUnknownSegments) {
	Reader m_original{ indexPath, false };

	AddEntryResult result{};

	if (overwriteUnknownSegments) {
		m_pImpl->m_sqpackIndexSegment2 = std::move(m_original.Index.DataFileSegment);
		m_pImpl->m_sqpackIndexSegment3 = std::move(m_original.Index.Segment3);
		m_pImpl->m_sqpackIndex2Segment2 = std::move(m_original.Index2.DataFileSegment);
		m_pImpl->m_sqpackIndex2Segment3 = std::move(m_original.Index2.Segment3);
	}

	std::vector<size_t> dataFileIndexToOpenFileIndex;
	for (auto& f : m_original.Data)
		dataFileIndexToOpenFileIndex.emplace_back(m_pImpl->OpenFile("", std::move(f.FileOnDisk)));

	for (const auto& entry : m_original.Files) {
		result += m_pImpl->AddEntry(
			m_original.GetEntryProvider(entry, Utils::Win32::File{ m_pImpl->m_openFiles[dataFileIndexToOpenFileIndex[entry.DataFileIndex]], false }),
			dataFileIndexToOpenFileIndex[entry.DataFileIndex],
			overwriteExisting);
	}

	return result;
}

Sqex::Sqpack::Creator::AddEntryResult Sqex::Sqpack::Creator::AddEntryFromFile(EntryPathSpec pathSpec, const std::filesystem::path & path, bool overwriteExisting) {
	std::shared_ptr<EntryProvider> provider;
	if (file_size(path) == 0) {
		provider = std::make_shared<EmptyEntryProvider>(std::move(pathSpec));
	} else if (path.extension() == ".tex") {
		provider = std::make_shared<OnTheFlyTextureEntryProvider>(std::move(pathSpec), path);
	} else if (path.extension() == ".mdl") {
		provider = std::make_shared<OnTheFlyModelEntryProvider>(std::move(pathSpec), path);
	} else {
		// provider = std::make_shared<MemoryBinaryEntryProvider>(std::move(pathSpec), path);
		provider = std::make_shared<OnTheFlyBinaryEntryProvider>(std::move(pathSpec), path);
	}
	return m_pImpl->AddEntry(provider, {}, overwriteExisting);
}

Sqex::Sqpack::Creator::AddEntryResult Sqex::Sqpack::Creator::AddEntriesFromTTMP(const std::filesystem::path & extractedDir, bool overwriteExisting) {
	AddEntryResult addEntryResult{};
	nlohmann::json conf;
	const auto ttmpdPath = extractedDir / "TTMPD.mpd";
	size_t ttmpd = SIZE_MAX;
	const auto ttmpl = ThirdParty::TexTools::TTMPL::FromStream(FileRandomAccessStream{ Utils::Win32::File::Create(
		extractedDir / "TTMPL.mpl", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0
	) });

	if (const auto configPath = extractedDir / "choices.json"; exists(configPath)) {
		m_pImpl->Log("Config file found");
		std::ifstream in(configPath);
		in >> conf;
	}
	for (size_t i = 0; i < ttmpl.SimpleModsList.size(); ++i) {
		const auto& entry = ttmpl.SimpleModsList[i];
		if (conf.is_array() && i < conf.size() && conf[i].is_boolean() && !conf[i].get<boolean>())
			continue;
		if (entry.DatFile != DatName)
			continue;

		if (ttmpd == SIZE_MAX)
			ttmpd = m_pImpl->OpenFile(ttmpdPath);

		addEntryResult += m_pImpl->AddEntry(std::make_shared<RandomAccessStreamAsEntryProviderView>(
			entry.FullPath,
			std::make_shared<FileRandomAccessStream>(Utils::Win32::File{ m_pImpl->m_openFiles[ttmpd], false }, entry.ModOffset, entry.ModSize)
			), ttmpd, overwriteExisting);

		m_pImpl->Log("{}: {} (Name: {} > {})",
			!addEntryResult.Added.empty() ? "Added" : !addEntryResult.Replaced.empty() ? "Replaced" : "Ignored",
			entry.FullPath, ttmpl.Name, entry.Name
		);
	}
	for (size_t pageObjectIndex = 0; pageObjectIndex < ttmpl.ModPackPages.size(); ++pageObjectIndex) {
		const auto& modGroups = ttmpl.ModPackPages[pageObjectIndex].ModGroups;
		const auto pageConf = conf.is_array() && pageObjectIndex < conf.size() && conf[pageObjectIndex].is_array() ?
			conf[pageObjectIndex] :
			nlohmann::json::array();

		for (size_t modGroupIndex = 0; modGroupIndex < modGroups.size(); ++modGroupIndex) {
			const auto& modGroup = modGroups[modGroupIndex];
			const auto choice = modGroupIndex < pageConf.size() ? pageConf[modGroupIndex].get<int>() : 0;
			const auto& option = modGroup.OptionList[choice];

			for (const auto& entry : option.ModsJsons) {
				if (entry.DatFile != DatName)
					continue;

				if (ttmpd == SIZE_MAX)
					ttmpd = m_pImpl->OpenFile(ttmpdPath);

				addEntryResult += m_pImpl->AddEntry(std::make_shared<RandomAccessStreamAsEntryProviderView>(
					entry.FullPath,
					std::make_shared<FileRandomAccessStream>(Utils::Win32::File{ m_pImpl->m_openFiles[ttmpd], false }, entry.ModOffset, entry.ModSize)
					), ttmpd, overwriteExisting);

				m_pImpl->Log("{}: {} (Name: {} > {}({}) > {}({}) > {})",
					!addEntryResult.Added.empty() ? "Added" : !addEntryResult.Replaced.empty() ? "Replaced" : "Ignored",
					entry.FullPath,
					ttmpl.Name,
					modGroup.GroupName, modGroupIndex,
					option.Name, choice,
					entry.Name
				);
			}
		}
	}
	return addEntryResult;
}

template<Sqex::Sqpack::SqIndex::Header::IndexType IndexType, typename FileEntryType, bool UseFolders>
class Sqex::Sqpack::Creator::IndexViewBase : public RandomAccessStream {
	std::vector<uint8_t> m_data;

public:
	IndexViewBase(size_t dataFilesCount, std::vector<FileEntryType> fileSegment, const std::vector<char>& segment2, const std::vector<SqIndex::Segment3Entry>& segment3, std::vector<SqIndex::FolderSegmentEntry> folderSegment = {}, bool strict = false) {

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
				m_header.Sha1.SetFromSpan(&m_header, 1);

			auto& m_subheader = *reinterpret_cast<SqIndex::Header*>(&m_data[sizeof SqpackHeader]);
			m_subheader.HeaderSize = sizeof SqIndex::Header;
			m_subheader.Type = IndexType;
			m_subheader.FileSegment.Count = 1;
			m_subheader.FileSegment.Offset = m_header.HeaderSize + m_subheader.HeaderSize;
			m_subheader.FileSegment.Size = static_cast<uint32_t>(std::span(fileSegment).size_bytes());
			m_subheader.DataFilesSegment.Count = static_cast<uint32_t>(dataFilesCount);
			m_subheader.DataFilesSegment.Offset = m_subheader.FileSegment.Offset + m_subheader.FileSegment.Size;
			m_subheader.DataFilesSegment.Size = static_cast<uint32_t>(std::span(segment2).size_bytes());
			m_subheader.UnknownSegment3.Count = 0;
			m_subheader.UnknownSegment3.Offset = m_subheader.DataFilesSegment.Offset + m_subheader.DataFilesSegment.Size;
			m_subheader.UnknownSegment3.Size = static_cast<uint32_t>(std::span(segment3).size_bytes());
			m_subheader.FolderSegment.Count = 0;
			m_subheader.FolderSegment.Offset = m_subheader.UnknownSegment3.Offset + m_subheader.UnknownSegment3.Size;
			if constexpr (UseFolders) {
				for (size_t i = 0; i < fileSegment.size(); ++i) {
					const auto& entry = fileSegment[i];
					if (folderSegment.empty() || folderSegment.back().NameHash != entry.PathHash) {
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

			if (strict)
				m_subheader.Sha1.SetFromSpan(&m_subheader, 1);
		}
		if (!fileSegment.empty())
			m_data.insert(m_data.end(), reinterpret_cast<const uint8_t*>(&fileSegment.front()), reinterpret_cast<const uint8_t*>(&fileSegment.back() + 1));
		if (!segment2.empty())
			m_data.insert(m_data.end(), reinterpret_cast<const uint8_t*>(&segment2.front()), reinterpret_cast<const uint8_t*>(&segment2.back() + 1));
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

template<>
class Sqex::Sqpack::Creator::IndexView<Sqex::Sqpack::SqIndex::Header::IndexType::Index> final : public IndexViewBase<SqIndex::Header::IndexType::Index, SqIndex::FileSegmentEntry, true> {
public:
	using IndexViewBase<SqIndex::Header::IndexType::Index, SqIndex::FileSegmentEntry, true>::IndexViewBase;
};

template<>
class Sqex::Sqpack::Creator::IndexView<Sqex::Sqpack::SqIndex::Header::IndexType::Index2> final : public IndexViewBase<SqIndex::Header::IndexType::Index2, SqIndex::FileSegmentEntry2, false> {
public:
	using IndexViewBase<SqIndex::Header::IndexType::Index2, SqIndex::FileSegmentEntry2, false>::IndexViewBase;
};

class Sqex::Sqpack::Creator::DataView : public RandomAccessStream {
	const std::vector<uint8_t> m_header;
	const std::vector<std::unique_ptr<Implementation::Entry>> m_entries;
	const std::vector<std::shared_ptr<Utils::Win32::File>> m_openFiles;

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

public:
	DataView(const SqpackHeader& header, const SqData::Header& subheader, std::vector<std::unique_ptr<Implementation::Entry>> entries, std::vector<std::shared_ptr<Utils::Win32::File>> openFiles)
		: m_header(Concat(header, subheader))
		, m_entries(std::move(entries))
		, m_openFiles(std::move(openFiles)) {
	}

	uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override {
		if (!length)
			return 0;

		auto relativeOffset = offset;
		auto out = std::span(static_cast<char*>(buf), length);

		if (relativeOffset < m_header.size()) {
			const auto src = std::span(m_header).subspan(static_cast<size_t>(relativeOffset));
			const auto available = std::min(out.size_bytes(), src.size_bytes());
			std::copy_n(src.begin(), available, out.begin());
			out = out.subspan(available);
			relativeOffset = 0;
		} else
			relativeOffset -= m_header.size();

		if (out.empty()) return length;

		auto it = std::lower_bound(m_entries.begin(), m_entries.end(), nullptr, [&](const std::unique_ptr<Implementation::Entry>& l, const std::unique_ptr<Implementation::Entry>& r) {
			const auto lo = l ? l->OffsetAfterHeaders : relativeOffset;
			const auto ro = r ? r->OffsetAfterHeaders : relativeOffset;
			return lo < ro;
		});
		if (it != m_entries.begin() && it != m_entries.end())
			--it;

		if (it != m_entries.end()) {
			relativeOffset -= it->get()->OffsetAfterHeaders;

			for (; it < m_entries.end(); ++it) {
				const auto& entry = *it->get();

				if (relativeOffset < entry.BlockSize) {
					const auto available = std::min(out.size_bytes(), static_cast<size_t>(entry.BlockSize - relativeOffset));
					entry.Provider->ReadStream(relativeOffset, out.data(), available);
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty())
						break;
				} else
					relativeOffset -= entry.BlockSize;

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
		return m_header.size() + SubHeader().HeaderSize + SubHeader().DataSize;
	}
};

Sqex::Sqpack::Creator::SqpackViews Sqex::Sqpack::Creator::AsViews(bool strict) {
	std::vector<SqIndex::FileSegmentEntry> fileEntries1;
	std::vector<SqIndex::FileSegmentEntry2> fileEntries2;
	std::vector<SqIndex::FolderSegmentEntry> folderEntries;

	std::vector<std::shared_ptr<Utils::Win32::File>> openFiles;

	for (auto& f : m_pImpl->m_openFiles)
		openFiles.emplace_back(std::make_shared<Utils::Win32::File>(std::move(f)));
	m_pImpl->m_openFiles.clear();

	SqpackHeader dataHeader{};
	std::vector<SqData::Header> dataSubheaders;
	std::vector<std::vector<std::unique_ptr<Implementation::Entry>>> dataEntries;
	std::vector<std::set<size_t>> dataOpenFileIndices;

	for (auto& entry : m_pImpl->m_entries) {
		entry->BlockSize = static_cast<uint32_t>(entry->Provider->StreamSize());
		entry->PadSize = Align(entry->BlockSize).Pad;

		if (dataSubheaders.empty() ||
			sizeof SqpackHeader + sizeof SqData::Header + dataSubheaders.back().DataSize + entry->BlockSize + entry->PadSize > dataSubheaders.back().MaxFileSize) {
			if (strict && !dataSubheaders.empty())
				dataSubheaders.back().Sha1.SetFromSpan(&dataSubheaders.back(), 1);
			dataSubheaders.emplace_back(SqData::Header{
				.HeaderSize = sizeof SqData::Header,
				.Unknown1 = SqData::Header::Unknown1_Value,
				.DataSize = 0,
				.SpanIndex = static_cast<uint32_t>(dataSubheaders.size()),
				.MaxFileSize = m_maxFileSize,
				});
			dataEntries.emplace_back();
			dataOpenFileIndices.emplace_back();
		}

		entry->DataFileIndex = static_cast<uint32_t>(dataSubheaders.size() - 1);
		entry->OffsetAfterHeaders = dataSubheaders.back().DataSize;
		entry->Locator = { entry->DataFileIndex, sizeof SqpackHeader + sizeof SqData::Header + entry->OffsetAfterHeaders };
		if (entry->Provider->PathSpec().HasComponentHash())
			fileEntries1.emplace_back(SqIndex::FileSegmentEntry{ entry->Provider->PathSpec().NameHash, entry->Provider->PathSpec().PathHash, entry->Locator, 0 });
		if (entry->Provider->PathSpec().HasFullPathHash())
			fileEntries2.emplace_back(SqIndex::FileSegmentEntry2{ entry->Provider->PathSpec().FullPathHash, entry->Locator });

		dataOpenFileIndices.back().insert(entry->UnderlyingFileIndex);

		dataSubheaders.back().DataSize = dataSubheaders.back().DataSize + entry->BlockSize + entry->PadSize;
		dataEntries.back().emplace_back(std::move(entry));
	}
	m_pImpl->m_entries.clear();
	m_pImpl->m_pathNameTupleEntryPointerMap.clear();
	m_pImpl->m_fullPathEntryPointerMap.clear();
	m_pImpl->m_sqpackIndexSegment2.clear();
	m_pImpl->m_sqpackIndexSegment3.clear();
	m_pImpl->m_sqpackIndex2Segment2.clear();
	m_pImpl->m_sqpackIndex2Segment3.clear();

	memcpy(dataHeader.Signature, SqpackHeader::Signature_Value, sizeof SqpackHeader::Signature_Value);
	dataHeader.HeaderSize = sizeof SqpackHeader;
	dataHeader.Unknown1 = SqpackHeader::Unknown1_Value;
	dataHeader.Type = SqpackType::SqData;
	dataHeader.Unknown2 = SqpackHeader::Unknown2_Value;
	if (strict)
		dataHeader.Sha1.SetFromSpan(&dataHeader, 1);

	auto res = SqpackViews{
		.Index = std::make_shared<IndexView<SqIndex::Header::IndexType::Index>>(dataSubheaders.size(), std::move(fileEntries1), m_pImpl->m_sqpackIndexSegment2, m_pImpl->m_sqpackIndexSegment3, std::vector<SqIndex::FolderSegmentEntry>(), strict),
		.Index2 = std::make_shared<IndexView<SqIndex::Header::IndexType::Index2>>(dataSubheaders.size(), std::move(fileEntries2), m_pImpl->m_sqpackIndexSegment2, m_pImpl->m_sqpackIndexSegment3, std::vector<SqIndex::FolderSegmentEntry>(), strict),
	};
	
	for (size_t i = 0; i < dataSubheaders.size(); ++i) {
		std::vector<std::shared_ptr<Utils::Win32::File>> dataOpenFiles;
		for (const auto j : dataOpenFileIndices[i])
			dataOpenFiles.emplace_back(openFiles[j]);

		res.Data.emplace_back(std::make_shared<DataView>(dataHeader, dataSubheaders[i], std::move(dataEntries[i]), std::move(dataOpenFiles)));
	}

	return res;
}
