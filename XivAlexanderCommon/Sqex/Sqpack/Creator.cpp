#include "pch.h"
#include "XivAlexanderCommon/Sqex/Sqpack/Creator.h"

#include "XivAlexanderCommon/Sqex/Model.h"
#include "XivAlexanderCommon/Sqex/Sqpack/BinaryEntryProvider.h"
#include "XivAlexanderCommon/Sqex/Sqpack/EmptyOrObfuscatedEntryProvider.h"
#include "XivAlexanderCommon/Sqex/Sqpack/EntryRawStream.h"
#include "XivAlexanderCommon/Sqex/Sqpack/HotSwappableEntryProvider.h"
#include "XivAlexanderCommon/Sqex/Sqpack/ModelEntryProvider.h"
#include "XivAlexanderCommon/Sqex/Sqpack/RandomAccessStreamAsEntryProviderView.h"
#include "XivAlexanderCommon/Sqex/Sqpack/Reader.h"
#include "XivAlexanderCommon/Sqex/Sqpack/TextureEntryProvider.h"
#include "XivAlexanderCommon/Sqex/ThirdParty/TexTools.h"

struct Sqex::Sqpack::Creator::Implementation {
	void AddEntry(AddEntryResult& result, std::shared_ptr<EntryProvider> provider, bool overwriteExisting = true);
	AddEntryResult AddEntry(std::shared_ptr<EntryProvider> provider, bool overwriteExisting = true);

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
	Error.insert(Error.end(), r.Error.begin(), r.Error.end());
	return *this;
}

Sqex::Sqpack::Creator::AddEntryResult& Sqex::Sqpack::Creator::AddEntryResult::operator+=(AddEntryResult && r) {
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

void Sqex::Sqpack::Creator::Implementation::AddEntry(AddEntryResult & result, std::shared_ptr<EntryProvider> provider, bool overwriteExisting) {
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

		auto entry = std::make_unique<Entry>(0, SqIndex::LEDataLocator{ 0, 0 }, std::move(provider));
		if (pProvider->PathSpec().HasOriginal())
			m_fullEntries.emplace(pProvider->PathSpec(), std::move(entry));
		else
			m_hashOnlyEntries.emplace(pProvider->PathSpec(), std::move(entry));
		result.Added.emplace_back(pProvider);
	} catch (const std::exception& e) {
		result.Error.emplace_back(pProvider->PathSpec(), e.what());
	}
}

Sqex::Sqpack::Creator::AddEntryResult Sqex::Sqpack::Creator::Implementation::AddEntry(std::shared_ptr<EntryProvider> provider, bool overwriteExisting) {
	AddEntryResult result;
	AddEntry(result, std::move(provider), overwriteExisting);
	return result;
}

Sqex::Sqpack::Creator::AddEntryResult Sqex::Sqpack::Creator::AddEntriesFromSqPack(const std::filesystem::path & indexPath, bool overwriteExisting, bool overwriteUnknownSegments) {
	Reader reader{ indexPath, false };

	if (overwriteUnknownSegments) {
		m_pImpl->m_sqpackIndexSegment3 = { reader.Index1.Segment3.begin(), reader.Index1.Segment3.end() };
		m_pImpl->m_sqpackIndex2Segment3 = { reader.Index2.Segment3.begin(), reader.Index2.Segment3.end() };
	}

	AddEntryResult result;
	for (const auto& [locator, entryInfo] : reader.EntryInfo) {
		try {
			m_pImpl->AddEntry(result, reader.GetEntryProvider(entryInfo.PathSpec, locator, entryInfo.Allocation), overwriteExisting);
		} catch (const std::exception& e) {
			result.Error.emplace_back(entryInfo.PathSpec, e.what());
		}
	}
	return result;
}

Sqex::Sqpack::Creator::AddEntryResult Sqex::Sqpack::Creator::AddEntryFromFile(EntryPathSpec pathSpec, const std::filesystem::path & path, bool overwriteExisting) {
	std::shared_ptr<EntryProvider> provider;
	auto extensionLower = path.extension().wstring();
	CharLowerW(&extensionLower[0]);
	if (file_size(path) == 0) {
		provider = std::make_shared<EmptyOrObfuscatedEntryProvider>(std::move(pathSpec));
	} else if (extensionLower == L".tex" || extensionLower == L".atex") {
		provider = std::make_shared<OnTheFlyTextureEntryProvider>(std::move(pathSpec), path);
	} else if (extensionLower == L".mdl") {
		provider = std::make_shared<OnTheFlyModelEntryProvider>(std::move(pathSpec), path);
	} else {
		provider = std::make_shared<OnTheFlyBinaryEntryProvider>(std::move(pathSpec), path);
	}
	return m_pImpl->AddEntry(provider, overwriteExisting);
}

Sqex::Sqpack::Creator::AddEntryResult Sqex::Sqpack::Creator::AddAllEntriesFromSimpleTTMP(const std::filesystem::path & extractedDir, bool overwriteExisting) {
	const auto ttmpdPath = extractedDir / "TTMPD.mpd";
	const auto ttmpl = ThirdParty::TexTools::TTMPL::FromStream(FileRandomAccessStream{ Win32::Handle::FromCreateFile(extractedDir / "TTMPL.mpl", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0) });

	if (DatExpac != "ffxiv")
		return {};

	const auto dataStream = std::make_shared<FileRandomAccessStream>(Win32::Handle::FromCreateFile(ttmpdPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN));

	AddEntryResult result;
	for (size_t i = 0; i < ttmpl.SimpleModsList.size(); ++i) {
		const auto& entry = ttmpl.SimpleModsList[i];
		if (entry.DatFile != DatName)
			continue;

		try {
			m_pImpl->AddEntry(result, std::make_shared<RandomAccessStreamAsEntryProviderView>(entry.FullPath, dataStream, entry.ModOffset, entry.ModSize), overwriteExisting);
		} catch (const std::exception& e) {
			result.Error.emplace_back(EntryPathSpec{ entry.FullPath }, std::string(e.what()));
			m_pImpl->Log("Error: {} (Name: {} > {})", entry.FullPath, ttmpl.Name, entry.Name);
		}
	}
	return result;
}

void Sqex::Sqpack::Creator::ReserveSpacesFromTTMP(const ThirdParty::TexTools::TTMPL & ttmpl, const std::shared_ptr<Sqex::RandomAccessStream>&ttmpd) {
	for (const auto& entry : ttmpl.SimpleModsList) {
		if (entry.DatFile != DatName || entry.ModSize > UINT32_MAX)
			continue;

		if (entry.IsMetadata()) {
			const auto metadata = Sqex::ThirdParty::TexTools::ItemMetadata(entry.FullPath, Sqex::Sqpack::EntryRawStream(std::make_shared<Sqex::Sqpack::RandomAccessStreamAsEntryProviderView>(entry.FullPath, ttmpd, entry.ModOffset, entry.ModSize)));
			if (!metadata.Get<Sqex::Imc::Entry>(Sqex::ThirdParty::TexTools::ItemMetadata::MetaDataType::Imc).empty())
				ReserveSwappableSpace(metadata.TargetImcPath, 65536);
			if (const auto eqdpedit = metadata.Get<Sqex::ThirdParty::TexTools::ItemMetadata::EqdpEntry>(Sqex::ThirdParty::TexTools::ItemMetadata::MetaDataType::Eqdp); !eqdpedit.empty()) {
				for (const auto& v : eqdpedit) {
					ReserveSwappableSpace(metadata.EqdpPath(metadata.ItemType, v.RaceCode), 1048576);
				}
			}
			continue;
		}

		ReserveSwappableSpace(entry.FullPath, static_cast<uint32_t>(entry.ModSize));
	}
	for (const auto& modPackPage : ttmpl.ModPackPages) {
		for (const auto& modGroup : modPackPage.ModGroups) {
			for (const auto& option : modGroup.OptionList) {
				for (const auto& entry : option.ModsJsons) {
					if (entry.DatFile != DatName || entry.ModSize > UINT32_MAX)
						continue;

					if (entry.IsMetadata()) {
						const auto metadata = Sqex::ThirdParty::TexTools::ItemMetadata(entry.FullPath, Sqex::Sqpack::EntryRawStream(std::make_shared<Sqex::Sqpack::RandomAccessStreamAsEntryProviderView>(entry.FullPath, ttmpd, entry.ModOffset, entry.ModSize)));
						if (!metadata.Get<Sqex::Imc::Entry>(Sqex::ThirdParty::TexTools::ItemMetadata::MetaDataType::Imc).empty())
							ReserveSwappableSpace(metadata.TargetImcPath, 65536);
						if (const auto eqdpedit = metadata.Get<Sqex::ThirdParty::TexTools::ItemMetadata::EqdpEntry>(Sqex::ThirdParty::TexTools::ItemMetadata::MetaDataType::Eqdp); !eqdpedit.empty()) {
							for (const auto& v : eqdpedit) {
								ReserveSwappableSpace(metadata.EqdpPath(metadata.ItemType, v.RaceCode), 1048576);
							}
						}
						continue;
					}

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
		it->second->EntrySize = std::max(it->second->EntrySize, size);
		if (!it->second->Provider->PathSpec().HasOriginal() && pathSpec.HasOriginal()) {
			it->second->Provider->UpdatePathSpec(pathSpec);
			m_pImpl->m_fullEntries.emplace(pathSpec, std::move(it->second));
			m_pImpl->m_hashOnlyEntries.erase(it);
		}
	} else if (const auto it = m_pImpl->m_fullEntries.find(pathSpec); it != m_pImpl->m_fullEntries.end()) {
		it->second->EntrySize = std::max(it->second->EntrySize, size);
	} else {
		auto entry = std::make_unique<Entry>(size, SqIndex::LEDataLocator{ 0, 0 }, std::make_shared<EmptyOrObfuscatedEntryProvider>(std::move(pathSpec)));
		if (entry->Provider->PathSpec().HasOriginal())
			m_pImpl->m_fullEntries.emplace(entry->Provider->PathSpec(), std::move(entry));
		else
			m_pImpl->m_hashOnlyEntries.emplace(entry->Provider->PathSpec(), std::move(entry));
	}
}

template<Sqex::Sqpack::SqIndex::Header::IndexType IndexType, typename FileEntryType, typename ConflictEntryType, bool UseFolders>
static std::vector<uint8_t> ExportIndexFileData(
	size_t dataFilesCount,
	std::vector<FileEntryType> fileSegment,
	const std::vector<ConflictEntryType>&conflictSegment,
	const std::vector<Sqex::Sqpack::SqIndex::Segment3Entry>&segment3,
	std::vector<Sqex::Sqpack::SqIndex::PathHashLocator> folderSegment = {},
	bool strict = false
) {
	using namespace Sqex::Sqpack;

	std::vector<uint8_t> data;
	data.reserve(sizeof SqpackHeader
		+ sizeof SqIndex::Header
		+ std::span(fileSegment).size_bytes()
		+ std::span(conflictSegment).size_bytes()
		+ std::span(segment3).size_bytes()
		+ std::span(folderSegment).size_bytes());

	data.resize(sizeof SqpackHeader + sizeof SqIndex::Header);
	auto& m_header = *reinterpret_cast<SqpackHeader*>(&data[0]);
	memcpy(m_header.Signature, SqpackHeader::Signature_Value, sizeof SqpackHeader::Signature_Value);
	m_header.HeaderSize = sizeof SqpackHeader;
	m_header.Unknown1 = SqpackHeader::Unknown1_Value;
	m_header.Type = SqpackType::SqIndex;
	m_header.Unknown2 = SqpackHeader::Unknown2_Value;
	if (strict)
		m_header.Sha1.SetFromSpan(reinterpret_cast<char*>(&m_header), offsetof(SqpackHeader, Sha1));

	auto& m_subheader = *reinterpret_cast<SqIndex::Header*>(&data[sizeof SqpackHeader]);
	std::sort(fileSegment.begin(), fileSegment.end());
	m_subheader.HeaderSize = sizeof SqIndex::Header;
	m_subheader.Type = IndexType;
	m_subheader.HashLocatorSegment.Count = 1;
	m_subheader.HashLocatorSegment.Offset = m_header.HeaderSize + m_subheader.HeaderSize;
	m_subheader.HashLocatorSegment.Size = static_cast<uint32_t>(std::span(fileSegment).size_bytes());
	m_subheader.TextLocatorSegment.Count = static_cast<uint32_t>(dataFilesCount);
	m_subheader.TextLocatorSegment.Offset = m_subheader.HashLocatorSegment.Offset + m_subheader.HashLocatorSegment.Size;
	m_subheader.TextLocatorSegment.Size = static_cast<uint32_t>(std::span(conflictSegment).size_bytes());
	m_subheader.UnknownSegment3.Count = 0;
	m_subheader.UnknownSegment3.Offset = m_subheader.TextLocatorSegment.Offset + m_subheader.TextLocatorSegment.Size;
	m_subheader.UnknownSegment3.Size = static_cast<uint32_t>(std::span(segment3).size_bytes());
	m_subheader.PathHashLocatorSegment.Count = 0;
	m_subheader.PathHashLocatorSegment.Offset = m_subheader.UnknownSegment3.Offset + m_subheader.UnknownSegment3.Size;
	if constexpr (UseFolders) {
		for (size_t i = 0; i < fileSegment.size(); ++i) {
			const auto& entry = fileSegment[i];
			if (folderSegment.empty() || folderSegment.back().PathHash != entry.PathHash) {
				folderSegment.emplace_back(
					entry.PathHash,
					static_cast<uint32_t>(m_subheader.HashLocatorSegment.Offset + i * sizeof entry),
					static_cast<uint32_t>(sizeof entry),
					0);
			} else {
				folderSegment.back().PairHashLocatorSize = folderSegment.back().PairHashLocatorSize + sizeof entry;
			}
		}
		m_subheader.PathHashLocatorSegment.Size = static_cast<uint32_t>(std::span(folderSegment).size_bytes());
	}

	if (strict) {
		m_subheader.Sha1.SetFromSpan(reinterpret_cast<char*>(&m_subheader), offsetof(SqIndex::Header, Sha1));
		if (!fileSegment.empty())
			m_subheader.HashLocatorSegment.Sha1.SetFromSpan(reinterpret_cast<const uint8_t*>(&fileSegment.front()), m_subheader.HashLocatorSegment.Size);
		if (!conflictSegment.empty())
			m_subheader.TextLocatorSegment.Sha1.SetFromSpan(reinterpret_cast<const uint8_t*>(&conflictSegment.front()), m_subheader.TextLocatorSegment.Size);
		if (!segment3.empty())
			m_subheader.UnknownSegment3.Sha1.SetFromSpan(reinterpret_cast<const uint8_t*>(&segment3.front()), m_subheader.UnknownSegment3.Size);
		if constexpr (UseFolders) {
			if (!folderSegment.empty())
				m_subheader.PathHashLocatorSegment.Sha1.SetFromSpan(reinterpret_cast<const uint8_t*>(&folderSegment.front()), m_subheader.PathHashLocatorSegment.Size);
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

class Sqex::Sqpack::Creator::DataView : public RandomAccessStream {
	const std::vector<uint8_t> m_header;
	const std::span<Entry*> m_entries;

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
	mutable uint64_t m_nLastTimeTaken = 0;
	mutable std::vector<std::tuple<EntryProvider*, uint64_t, uint64_t>> m_pLastEntryProviders;
	mutable size_t m_lastAccessedEntryIndex = SIZE_MAX;
	const std::shared_ptr<SqpackViewEntryCache> m_buffer;

public:

	DataView(const SqpackHeader& header, const SqData::Header& subheader, std::span<Entry*> entries, std::shared_ptr<SqpackViewEntryCache> buffer)
		: m_header(Concat(header, subheader))
		, m_entries(std::move(entries))
		, m_buffer(std::move(buffer)) {
	}

	uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override {
		const auto st = Utils::QpcUs();
		m_pLastEntryProviders.clear();
		m_nLastRequestedOffset = offset;
		m_nLastRequestedSize = length;
		m_nLastTimeTaken = 0;
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

		if (out.empty()) {
			m_nLastTimeTaken = Utils::QpcUs() - st;
			return length;
		}

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
					const auto available = std::min(out.size_bytes(), static_cast<size_t>(entry.EntrySize - relativeOffset));
					m_pLastEntryProviders.emplace_back(std::make_tuple(entry.Provider.get(), relativeOffset, available));
					if (buf)
						std::copy_n(&buf->Buffer()[static_cast<size_t>(relativeOffset)], available, &out[0]);
					else
						entry.Provider->ReadStream(relativeOffset, out.data(), available);
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty())
						break;
				} else
					relativeOffset -= entry.EntrySize;
			}
		}

		m_nLastTimeTaken = Utils::QpcUs() - st;
		return length - out.size_bytes();
	}

	uint64_t StreamSize() const override {
		return m_header.size() + SubHeader().DataSize;
	}

	std::string DescribeState() const override {
		auto res = std::format("Sqpack::Creator::DataView({}->{}) {}us", m_nLastRequestedOffset, m_nLastRequestedSize, m_nLastTimeTaken);
		for (const auto& [p, off, len] : m_pLastEntryProviders) {
			res += std::format(" [{}: {}->{}: {}]", p->PathSpec(), off, len, p->DescribeState());
		}
		return res;
	}

	void Flush() const override {
		if (m_buffer)
			m_buffer->Flush();
	}
};

Sqex::Sqpack::Creator::SqpackViews Sqex::Sqpack::Creator::AsViews(bool strict, const std::shared_ptr<SqpackViewEntryCache>&dataBuffer) {
	SqpackHeader dataHeader{};
	std::vector<SqData::Header> dataSubheaders;
	std::vector<std::pair<size_t, size_t>> dataEntryRanges;

	auto res = SqpackViews{
		.HashOnlyEntries = std::move(m_pImpl->m_hashOnlyEntries),
		.FullPathEntries = std::move(m_pImpl->m_fullEntries),
	};

	res.Entries.reserve(m_pImpl->m_fullEntries.size() + m_pImpl->m_hashOnlyEntries.size());
	for (auto& entry : res.HashOnlyEntries | std::views::values)
		res.Entries.emplace_back(entry.get());
	for (auto& entry : res.FullPathEntries | std::views::values)
		res.Entries.emplace_back(entry.get());

	std::map<std::pair<uint32_t, uint32_t>, std::vector<Entry*>> pairHashes;
	std::map<uint32_t, std::vector<Entry*>> fullHashes;
	for (const auto& entry : res.Entries) {
		const auto& pathSpec = entry->Provider->PathSpec();
		pairHashes[std::make_pair(pathSpec.PathHash, pathSpec.NameHash)].emplace_back(entry);
		fullHashes[pathSpec.FullPathHash].emplace_back(entry);
	}

	for (size_t i = 0; i < res.Entries.size(); ++i) {
		auto& entry = res.Entries[i];
		const auto& pathSpec = entry->Provider->PathSpec();
		entry->EntrySize = Align(std::max(entry->EntrySize, static_cast<uint32_t>(entry->Provider->StreamSize()))).Alloc;
		entry->Provider = std::make_shared<HotSwappableEntryProvider>(pathSpec, entry->EntrySize, std::move(entry->Provider));

		if (dataSubheaders.empty() ||
			sizeof SqpackHeader + sizeof SqData::Header + dataSubheaders.back().DataSize + entry->EntrySize > dataSubheaders.back().MaxFileSize) {
			if (strict && !dataSubheaders.empty()) {
				CryptoPP::SHA1 sha1;
				for (auto j = dataEntryRanges.back().first, j_ = j + dataEntryRanges.back().second; j < j_; ++j) {
					const auto& entry = res.Entries[j];
					const auto& provider = *entry->Provider;
					const auto length = provider.StreamSize();
					uint8_t buf[4096];
					for (uint64_t j = 0; j < length; j += sizeof buf) {
						const auto readlen = static_cast<size_t>(std::min<uint64_t>(sizeof buf, length - j));
						provider.ReadStream(j, buf, readlen);
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
			dataEntryRanges.emplace_back(i, 0);
		}

		entry->Locator = { static_cast<uint32_t>(dataSubheaders.size() - 1), sizeof SqpackHeader + sizeof SqData::Header + dataSubheaders.back().DataSize };

		dataSubheaders.back().DataSize = dataSubheaders.back().DataSize + entry->EntrySize;
		dataEntryRanges.back().second++;
	}

	if (strict && !dataSubheaders.empty()) {
		CryptoPP::SHA1 sha1;
		for (auto j = dataEntryRanges.back().first, j_ = j + dataEntryRanges.back().second; j < j_; ++j) {
			const auto& entry = res.Entries[j];
			const auto& provider = *entry->Provider;
			const auto length = provider.StreamSize();
			uint8_t buf[4096];
			for (uint64_t j = 0; j < length; j += sizeof buf) {
				const auto readlen = static_cast<size_t>(std::min<uint64_t>(sizeof buf, length - j));
				provider.ReadStream(j, buf, readlen);
				sha1.Update(buf, readlen);
			}
		}
		sha1.Final(reinterpret_cast<byte*>(dataSubheaders.back().DataSha1.Value));
		dataSubheaders.back().Sha1.SetFromSpan(reinterpret_cast<char*>(&dataSubheaders.back()), offsetof(Sqpack::SqData::Header, Sha1));
	}

	std::vector<SqIndex::PairHashLocator> fileEntries1;
	std::vector<SqIndex::PairHashWithTextLocator> conflictEntries1;
	for (const auto& [pairHash, correspondingEntries] : pairHashes) {
		if (correspondingEntries.size() == 1) {
			fileEntries1.emplace_back(SqIndex::PairHashLocator{ pairHash.second, pairHash.first, correspondingEntries.front()->Locator, 0 });
		} else {
			fileEntries1.emplace_back(SqIndex::PairHashLocator{ pairHash.second, pairHash.first, SqIndex::LEDataLocator::Synonym(), 0 });
			uint32_t i = 0;
			for (const auto& entry : correspondingEntries) {
				conflictEntries1.emplace_back(SqIndex::PairHashWithTextLocator{
					.NameHash = pairHash.second,
					.PathHash = pairHash.first,
					.Locator = entry->Locator,
					.ConflictIndex = i++,
					});
				const auto path = entry->Provider->PathSpec().NativeRepresentation();
				strncpy_s(conflictEntries1.back().FullPath, path.c_str(), path.size());
			}
		}
	}
	conflictEntries1.emplace_back(SqIndex::PairHashWithTextLocator{
		.NameHash = SqIndex::PairHashWithTextLocator::EndOfList,
		.PathHash = SqIndex::PairHashWithTextLocator::EndOfList,
		.Locator = 0,
		.ConflictIndex = SqIndex::PairHashWithTextLocator::EndOfList,
		});

	std::vector<SqIndex::FullHashLocator> fileEntries2;
	std::vector<SqIndex::FullHashWithTextLocator> conflictEntries2;
	for (const auto& [fullHash, correspondingEntries] : fullHashes) {
		if (correspondingEntries.size() == 1) {
			fileEntries2.emplace_back(SqIndex::FullHashLocator{ fullHash, correspondingEntries.front()->Locator });
		} else {
			fileEntries2.emplace_back(SqIndex::FullHashLocator{ fullHash, SqIndex::LEDataLocator::Synonym() });
			uint32_t i = 0;
			for (const auto& entry : correspondingEntries) {
				conflictEntries2.emplace_back(SqIndex::FullHashWithTextLocator{
					.FullPathHash = fullHash,
					.UnusedHash = 0,
					.Locator = entry->Locator,
					.ConflictIndex = i++,
					});
				const auto path = entry->Provider->PathSpec().NativeRepresentation();
				strncpy_s(conflictEntries2.back().FullPath, path.c_str(), path.size());
			}
		}
	}
	conflictEntries2.emplace_back(SqIndex::FullHashWithTextLocator{
		.FullPathHash = SqIndex::FullHashWithTextLocator::EndOfList,
		.UnusedHash = SqIndex::FullHashWithTextLocator::EndOfList,
		.Locator = 0,
		.ConflictIndex = SqIndex::FullHashWithTextLocator::EndOfList,
		});

	memcpy(dataHeader.Signature, SqpackHeader::Signature_Value, sizeof SqpackHeader::Signature_Value);
	dataHeader.HeaderSize = sizeof SqpackHeader;
	dataHeader.Unknown1 = SqpackHeader::Unknown1_Value;
	dataHeader.Type = SqpackType::SqData;
	dataHeader.Unknown2 = SqpackHeader::Unknown2_Value;
	if (strict)
		dataHeader.Sha1.SetFromSpan(reinterpret_cast<char*>(&dataHeader), offsetof(SqpackHeader, Sha1));

	res.Index1 = std::make_shared<Sqex::MemoryRandomAccessStream>(ExportIndexFileData<Sqex::Sqpack::SqIndex::Header::IndexType::Index, SqIndex::PairHashLocator, SqIndex::PairHashWithTextLocator, true>(
		dataSubheaders.size(), std::move(fileEntries1), std::move(conflictEntries1), m_pImpl->m_sqpackIndexSegment3, std::vector<SqIndex::PathHashLocator>(), strict));
	res.Index2 = std::make_shared<Sqex::MemoryRandomAccessStream>(ExportIndexFileData<Sqex::Sqpack::SqIndex::Header::IndexType::Index, SqIndex::FullHashLocator, SqIndex::FullHashWithTextLocator, false>(
		dataSubheaders.size(), std::move(fileEntries2), std::move(conflictEntries2), m_pImpl->m_sqpackIndex2Segment3, std::vector<SqIndex::PathHashLocator>(), strict));
	for (size_t i = 0; i < dataSubheaders.size(); ++i)
		res.Data.emplace_back(std::make_shared<DataView>(dataHeader, dataSubheaders[i], std::span(res.Entries).subspan(dataEntryRanges[i].first, dataEntryRanges[i].second), dataBuffer));

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

void Sqex::Sqpack::Creator::WriteToFiles(const std::filesystem::path & dir, bool strict) {
	SqpackHeader dataHeader{};
	memcpy(dataHeader.Signature, SqpackHeader::Signature_Value, sizeof SqpackHeader::Signature_Value);
	dataHeader.HeaderSize = sizeof SqpackHeader;
	dataHeader.Unknown1 = SqpackHeader::Unknown1_Value;
	dataHeader.Type = SqpackType::SqData;
	dataHeader.Unknown2 = SqpackHeader::Unknown2_Value;
	if (strict)
		dataHeader.Sha1.SetFromSpan(reinterpret_cast<char*>(&dataHeader), offsetof(SqpackHeader, Sha1));

	std::vector<SqData::Header> dataSubheaders;

	std::vector<std::unique_ptr<Entry>> entries;
	entries.reserve(m_pImpl->m_fullEntries.size() + m_pImpl->m_hashOnlyEntries.size());
	for (auto& e : m_pImpl->m_fullEntries)
		entries.emplace_back(std::move(e.second));
	for (auto& e : m_pImpl->m_hashOnlyEntries)
		entries.emplace_back(std::move(e.second));
	m_pImpl->m_fullEntries.clear();
	m_pImpl->m_hashOnlyEntries.clear();

	std::map<std::pair<uint32_t, uint32_t>, std::vector<Entry*>> pairHashes;
	std::map<uint32_t, std::vector<Entry*>> fullHashes;
	std::map<Entry*, EntryPathSpec> entryPathSpecs;
	for (const auto& entry : entries) {
		const auto& pathSpec = entry->Provider->PathSpec();
		entryPathSpecs.emplace(entry.get(), pathSpec);
		pairHashes[std::make_pair(pathSpec.PathHash, pathSpec.NameHash)].emplace_back(entry.get());
		fullHashes[pathSpec.FullPathHash].emplace_back(entry.get());
	}

	std::vector<SqIndex::LEDataLocator> locators;

	Utils::Win32::Handle dataFile;
	std::vector<uint8_t> buf(1024 * 1024);
	for (size_t i = 0; i < entries.size(); ++i) {
		auto& entry = *entries[i];
		const auto provider{ std::move(entry.Provider) };
		const auto entrySize = provider->StreamSize();

		if (dataSubheaders.empty() ||
			sizeof SqpackHeader + sizeof SqData::Header + dataSubheaders.back().DataSize + entrySize > dataSubheaders.back().MaxFileSize) {
			if (strict && !dataSubheaders.empty()) {
				CryptoPP::SHA1 sha1;
				Align<uint64_t>(dataSubheaders.back().DataSize, buf.size()).IterateChunked([&](uint64_t index, uint64_t offset, uint64_t size) {
					const auto bufSpan = std::span(buf).subspan(0, static_cast<size_t>(size));
					dataFile.Read(offset, bufSpan);
					sha1.Update(&bufSpan[0], static_cast<size_t>(size));
					dataFile.Write(entry.Locator.DatFileOffset() + offset, bufSpan);
					}, sizeof SqpackHeader + sizeof SqData::Header);

				sha1.Final(reinterpret_cast<byte*>(dataSubheaders.back().DataSha1.Value));
				dataSubheaders.back().Sha1.SetFromSpan(reinterpret_cast<char*>(&dataSubheaders.back()), offsetof(Sqpack::SqData::Header, Sha1));
				dataFile.Write(0, &dataHeader, sizeof dataHeader);
				dataFile.Write(sizeof dataHeader, &dataSubheaders.back(), sizeof dataSubheaders.back());
				dataFile.Clear();
			}

			dataFile = Utils::Win32::Handle::FromCreateFile(dir / std::format("{}.win32.dat{}", DatName, dataSubheaders.size()),
				GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS);
			dataSubheaders.emplace_back(SqData::Header{
				.HeaderSize = sizeof SqData::Header,
				.Unknown1 = SqData::Header::Unknown1_Value,
				.DataSize = 0,
				.SpanIndex = static_cast<uint32_t>(dataSubheaders.size()),
				.MaxFileSize = m_maxFileSize,
				});
		}

		entry.Locator = { static_cast<uint32_t>(dataSubheaders.size() - 1), sizeof SqpackHeader + sizeof SqData::Header + dataSubheaders.back().DataSize };
		Align<uint64_t>(entrySize, buf.size()).IterateChunked([&](uint64_t index, uint64_t offset, uint64_t size) {
			const auto bufSpan = std::span(buf).subspan(0, static_cast<size_t>(size));
			provider->ReadStream(offset, bufSpan);
			dataFile.Write(entry.Locator.DatFileOffset() + offset, bufSpan);
			});

		dataSubheaders.back().DataSize = dataSubheaders.back().DataSize + entrySize;
	}

	if (dataFile) {
		if (strict) {
			CryptoPP::SHA1 sha1;
			Align<uint64_t>(dataSubheaders.back().DataSize, buf.size()).IterateChunked([&](uint64_t index, uint64_t offset, uint64_t size) {
				const auto bufSpan = std::span(buf).subspan(0, static_cast<size_t>(size));
				dataFile.Read(offset, bufSpan);
				sha1.Update(&bufSpan[0], static_cast<size_t>(size));
				}, sizeof SqpackHeader + sizeof SqData::Header);

			sha1.Final(reinterpret_cast<byte*>(dataSubheaders.back().DataSha1.Value));
			dataSubheaders.back().Sha1.SetFromSpan(reinterpret_cast<char*>(&dataSubheaders.back()), offsetof(Sqpack::SqData::Header, Sha1));
			dataFile.Write(0, &dataHeader, sizeof dataHeader);
			dataFile.Write(sizeof dataHeader, &dataSubheaders.back(), sizeof dataSubheaders.back());
			dataFile.Clear();
		}
		dataFile.Write(0, &dataHeader, sizeof dataHeader);
		dataFile.Write(sizeof dataHeader, &dataSubheaders.back(), sizeof dataSubheaders.back());
		dataFile.Clear();
	}

	std::vector<SqIndex::PairHashLocator> fileEntries1;
	std::vector<SqIndex::PairHashWithTextLocator> conflictEntries1;
	for (const auto& [pairHash, correspondingEntries] : pairHashes) {
		if (correspondingEntries.size() == 1) {
			fileEntries1.emplace_back(SqIndex::PairHashLocator{ pairHash.second, pairHash.first, correspondingEntries.front()->Locator, 0 });
		} else {
			fileEntries1.emplace_back(SqIndex::PairHashLocator{ pairHash.second, pairHash.first, SqIndex::LEDataLocator::Synonym(), 0 });
			uint32_t i = 0;
			for (const auto& entry : correspondingEntries) {
				conflictEntries1.emplace_back(SqIndex::PairHashWithTextLocator{
					.NameHash = pairHash.second,
					.PathHash = pairHash.first,
					.Locator = entry->Locator,
					.ConflictIndex = i++,
					});
				const auto path = entryPathSpecs[entry].NativeRepresentation();
				strncpy_s(conflictEntries1.back().FullPath, path.c_str(), path.size());
			}
		}
	}
	conflictEntries1.emplace_back(SqIndex::PairHashWithTextLocator{
		.NameHash = SqIndex::PairHashWithTextLocator::EndOfList,
		.PathHash = SqIndex::PairHashWithTextLocator::EndOfList,
		.Locator = 0,
		.ConflictIndex = SqIndex::PairHashWithTextLocator::EndOfList,
		});

	std::vector<SqIndex::FullHashLocator> fileEntries2;
	std::vector<SqIndex::FullHashWithTextLocator> conflictEntries2;
	for (const auto& [fullHash, correspondingEntries] : fullHashes) {
		if (correspondingEntries.size() == 1) {
			fileEntries2.emplace_back(SqIndex::FullHashLocator{ fullHash, correspondingEntries.front()->Locator });
		} else {
			fileEntries2.emplace_back(SqIndex::FullHashLocator{ fullHash, SqIndex::LEDataLocator::Synonym() });
			uint32_t i = 0;
			for (const auto& entry : correspondingEntries) {
				conflictEntries2.emplace_back(SqIndex::FullHashWithTextLocator{
					.FullPathHash = fullHash,
					.UnusedHash = 0,
					.Locator = entry->Locator,
					.ConflictIndex = i++,
					});
				const auto path = entryPathSpecs[entry].NativeRepresentation();
				strncpy_s(conflictEntries2.back().FullPath, path.c_str(), path.size());
			}
		}
	}
	conflictEntries2.emplace_back(SqIndex::FullHashWithTextLocator{
		.FullPathHash = SqIndex::FullHashWithTextLocator::EndOfList,
		.UnusedHash = SqIndex::FullHashWithTextLocator::EndOfList,
		.Locator = 0,
		.ConflictIndex = SqIndex::FullHashWithTextLocator::EndOfList,
		});

	Utils::Win32::Handle::FromCreateFile(dir / std::format("{}.win32.index", DatName), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS)
		.Write(0, std::span<const uint8_t>(ExportIndexFileData<Sqex::Sqpack::SqIndex::Header::IndexType::Index, SqIndex::PairHashLocator, SqIndex::PairHashWithTextLocator, true>(
			dataSubheaders.size(), std::move(fileEntries1), std::move(conflictEntries1), m_pImpl->m_sqpackIndexSegment3, std::vector<SqIndex::PathHashLocator>(), strict)));
	Utils::Win32::Handle::FromCreateFile(dir / std::format("{}.win32.index2", DatName), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS)
		.Write(0, std::span<const uint8_t>(ExportIndexFileData<Sqex::Sqpack::SqIndex::Header::IndexType::Index, SqIndex::FullHashLocator, SqIndex::FullHashWithTextLocator, false>(
			dataSubheaders.size(), std::move(fileEntries2), std::move(conflictEntries2), m_pImpl->m_sqpackIndex2Segment3, std::vector<SqIndex::PathHashLocator>(), strict)));
}

void Sqex::Sqpack::Creator::SqpackViewEntryCache::Flush() {
	m_lastActiveEntry.ClearEntry();
}

Sqex::Sqpack::Creator::SqpackViewEntryCache::BufferedEntry* Sqex::Sqpack::Creator::SqpackViewEntryCache::GetBuffer(const DataView * view, const Entry * entry) {
	if (m_lastActiveEntry.IsEntry(view, entry))
		return &m_lastActiveEntry;

	if (entry->EntrySize > LargeEntryBufferSizeMax)
		return nullptr;

	m_lastActiveEntry.SetEntry(view, entry);
	return &m_lastActiveEntry;
}
