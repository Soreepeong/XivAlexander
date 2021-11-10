#include "pch.h"
#include "Sqex_Sqpack_Reader.h"

#include "Sqex_Sqpack_EntryRawStream.h"

template<typename HashLocatorT, typename TextLocatorT>
Sqex::Sqpack::Reader::SqIndexType<HashLocatorT, TextLocatorT>::SqIndexType(const Win32::Handle& hFile, bool strictVerify)
	: Data(hFile.Read<uint8_t>(0, static_cast<size_t>(hFile.GetFileSize())))
	, Header(*reinterpret_cast<const SqpackHeader*>(&Data[0]))
	, IndexHeader(*reinterpret_cast<const SqIndex::Header*>(&Data[Header.HeaderSize]))
	, HashLocators(reinterpret_cast<const HashLocatorT*>(&Data[IndexHeader.HashLocatorSegment.Offset]),
		IndexHeader.HashLocatorSegment.Size / sizeof HashLocatorT)
	, TextLocators(reinterpret_cast<const TextLocatorT*>(&Data[IndexHeader.TextLocatorSegment.Offset]),
		IndexHeader.TextLocatorSegment.Size / sizeof TextLocatorT)
	, Segment3(reinterpret_cast<const SqIndex::Segment3Entry*>(&Data[IndexHeader.UnknownSegment3.Offset]),
		IndexHeader.UnknownSegment3.Size / sizeof SqIndex::Segment3Entry) {

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

Sqex::Sqpack::Reader::SqIndex1Type::SqIndex1Type(const Win32::Handle& hFile, bool strictVerify)
	: SqIndexType<SqIndex::PairHashLocator, SqIndex::PairHashWithTextLocator>(hFile, strictVerify)
	, PathHashLocators(reinterpret_cast<const SqIndex::PathHashLocator*>(&Data[IndexHeader.PathHashLocatorSegment.Offset]),
		IndexHeader.PathHashLocatorSegment.Size / sizeof SqIndex::PathHashLocator) {
	if (strictVerify) {
		if (IndexHeader.TextLocatorSegment.Size % sizeof SqIndex::PathHashLocator)
			throw CorruptDataException("PathHashLocators has an invalid size alignment");
		IndexHeader.PathHashLocatorSegment.Sha1.Verify(PathHashLocators, "PathHashLocatorSegment has invalid data SHA-1");
	}
}

#pragma optimize("", off)
__declspec(noinline)
std::span<const Sqex::Sqpack::SqIndex::PairHashLocator> Sqex::Sqpack::Reader::SqIndex1Type::GetPairHashLocators(uint32_t pathHash) const {
	const auto it = std::lower_bound(PathHashLocators.begin(), PathHashLocators.end(), pathHash, PathSpecComparator());
	if (it == PathHashLocators.end() || it->PathHash != pathHash)
		throw std::out_of_range(std::format("PathHash {:08x} not found", pathHash));

	return {
		reinterpret_cast<const SqIndex::PairHashLocator*>(&Data[it->PairHashLocatorOffset]),
		it->PairHashLocatorSize / sizeof SqIndex::PairHashLocator
	};
}
#pragma optimize("", on)

const Sqex::Sqpack::SqIndex::LEDataLocator& Sqex::Sqpack::Reader::SqIndex1Type::GetLocator(uint32_t pathHash, uint32_t nameHash) const {
	const auto locators = GetPairHashLocators(pathHash);
	const auto it = std::lower_bound(locators.begin(), locators.end(), nameHash, PathSpecComparator());
	if (it == locators.end() || it->NameHash != nameHash)
		throw std::out_of_range(std::format("NameHash {:08x} in PathHash {:08x} not found", nameHash, pathHash));
	return it->Locator;
}

Sqex::Sqpack::Reader::SqIndex2Type::SqIndex2Type(const Win32::Handle& hFile, bool strictVerify)
	: SqIndexType<SqIndex::FullHashLocator, SqIndex::FullHashWithTextLocator>(hFile, strictVerify) {
}

const Sqex::Sqpack::SqIndex::LEDataLocator& Sqex::Sqpack::Reader::SqIndex2Type::GetLocator(uint32_t fullPathHash) const {
	const auto it = std::lower_bound(HashLocators.begin(), HashLocators.end(), fullPathHash, PathSpecComparator());
	if (it == HashLocators.end() || it->FullPathHash != fullPathHash)
		throw std::out_of_range(std::format("FullPathHash {:08x} not found", fullPathHash));
	return it->Locator;
}

Sqex::Sqpack::Reader::SqDataType::SqDataType(Win32::Handle hFile, const uint32_t datIndex, bool strictVerify)
	: Stream(std::make_shared<FileRandomAccessStream>(std::move(hFile))) {

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

Sqex::Sqpack::Reader::Reader(const std::filesystem::path& indexFile, bool strictVerify)
	: Index1(Win32::Handle::FromCreateFile(std::filesystem::path(indexFile).replace_extension(".index"), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN), strictVerify)
	, Index2(Win32::Handle::FromCreateFile(std::filesystem::path(indexFile).replace_extension(".index2"), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN), strictVerify) {

	std::vector<std::pair<SqIndex::LEDataLocator, std::tuple<uint32_t, uint32_t, const char*>>> offsets1;
	offsets1.reserve(
		std::max(Index1.HashLocators.size() + Index1.TextLocators.size(), Index2.HashLocators.size() + Index2.TextLocators.size())
		+ Index1.IndexHeader.TextLocatorSegment.Count
	);
	for (const auto& item : Index1.HashLocators)
		if (!item.Locator.IsSynonym())
			offsets1.emplace_back(item.Locator, std::make_tuple(item.PathHash, item.NameHash, static_cast<const char*>(nullptr)));
	for (const auto& item : Index1.TextLocators)
		offsets1.emplace_back(item.Locator, std::make_tuple(item.PathHash, item.NameHash, item.FullPath));

	std::vector<std::pair<SqIndex::LEDataLocator, std::tuple<uint32_t, const char*>>> offsets2;
	for (const auto& item : Index2.HashLocators)
		if (!item.Locator.IsSynonym())
			offsets2.emplace_back(item.Locator, std::make_tuple(item.FullPathHash, static_cast<const char*>(nullptr)));
	for (const auto& item : Index2.TextLocators)
		offsets2.emplace_back(item.Locator, std::make_tuple(item.FullPathHash, item.FullPath));

	if (offsets1.size() != offsets2.size())
		throw CorruptDataException(".index and .index2 do not have the same number of files contained");

	Data.reserve(Index1.IndexHeader.TextLocatorSegment.Count);
	for (uint32_t i = 0; i < Index1.IndexHeader.TextLocatorSegment.Count; ++i) {
		Data.emplace_back(SqDataType{
			Win32::Handle::FromCreateFile(std::filesystem::path(indexFile).replace_extension(std::format(".dat{}", i)), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0),
			i,
			strictVerify,
			});
		offsets1.emplace_back(SqIndex::LEDataLocator(i, Data[i].Stream->StreamSize()), std::make_tuple(UINT32_MAX, UINT32_MAX, static_cast<const char*>(nullptr)));
		offsets2.emplace_back(SqIndex::LEDataLocator(i, Data[i].Stream->StreamSize()), std::make_tuple(UINT32_MAX, static_cast<const char*>(nullptr)));
	}
	
	struct Comparator {
		bool operator()(const SqIndex::LEDataLocator& l, const SqIndex::LEDataLocator& r) const {
			if (l.DatFileIndex() != r.DatFileIndex())
				return l.DatFileIndex() < r.DatFileIndex();
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
	};

	std::sort(offsets1.begin(), offsets1.end(), Comparator());
	std::sort(offsets2.begin(), offsets2.end(), Comparator());
	EntryInfo.reserve(offsets1.size());

	if (strictVerify) {
		for (size_t i = 0; i < offsets1.size(); ++i) {
			if (offsets1[i].first != offsets2[i].first)
				throw CorruptDataException(".index and .index2 have items with different locators");
			if (offsets1[i].first.IsSynonym())
				throw CorruptDataException("Synonym remains after conflict resolution");
		}
	}

	for (size_t curr = 1, prev = 0; curr < offsets1.size(); ++curr, ++prev) {
		if (offsets1[prev].first.DatFileIndex() != offsets1[curr].first.DatFileIndex())
			continue;
		EntryInfo.emplace_back(offsets1[prev].first, EntryInfoType{
			.PathSpec = EntryPathSpec(
				std::get<0>(offsets1[prev].second),
				std::get<1>(offsets1[prev].second),
				std::get<0>(offsets2[prev].second),
				std::get<2>(offsets1[prev].second) ? std::string(std::get<2>(offsets1[prev].second)) :
					std::get<1>(offsets2[prev].second) ? std::string(std::get<1>(offsets2[prev].second)) :
						std::string()
			),
			.Allocation = offsets1[curr].first.DatFileOffset() - offsets1[prev].first.DatFileOffset(),
			});
	}
}

const Sqex::Sqpack::SqIndex::LEDataLocator& Sqex::Sqpack::Reader::GetLocator(const EntryPathSpec& pathSpec) const {
	try {
		if (pathSpec.HasComponentHash()) {
			const auto& locator = Index1.GetLocator(pathSpec.PathHash, pathSpec.NameHash);
			if (locator.IsSynonym())
				return Index1.GetLocatorFromTextLocators(Utils::ToUtf8(pathSpec.Original.wstring()).c_str());
			return locator;
		} else if (pathSpec.HasFullPathHash()) {
			const auto& locator = Index2.GetLocator(pathSpec.FullPathHash);
			if (locator.IsSynonym())
				return Index2.GetLocatorFromTextLocators(Utils::ToUtf8(pathSpec.Original.wstring()).c_str());
			return locator;
		}
	} catch (const std::out_of_range& e) {
		throw std::out_of_range(std::format("Failed to find {}: {}", pathSpec, e.what()));
	}
	throw std::out_of_range(std::format("Path spec is empty"));
}

std::shared_ptr<Sqex::Sqpack::EntryProvider> Sqex::Sqpack::Reader::GetEntryProvider(const EntryPathSpec& pathSpec, SqIndex::LEDataLocator locator, uint64_t allocation) const {
	return std::make_shared<RandomAccessStreamAsEntryProviderView>(pathSpec, Data.at(locator.DatFileIndex()).Stream, locator.DatFileOffset(), allocation);
}

std::shared_ptr<Sqex::Sqpack::EntryProvider> Sqex::Sqpack::Reader::GetEntryProvider(const EntryPathSpec& pathSpec) const {
	struct Comparator {
		bool operator()(const std::pair<SqIndex::LEDataLocator, EntryInfoType>& l, const SqIndex::LEDataLocator& r) const {
			return l.first < r;
		}

		bool operator()(const SqIndex::LEDataLocator& l, const std::pair<SqIndex::LEDataLocator, EntryInfoType>& r) const {
			return l < r.first;
		}
	};

	const auto& locator = GetLocator(pathSpec);
	return GetEntryProvider(pathSpec, locator, std::lower_bound(EntryInfo.begin(), EntryInfo.end(), locator, Comparator())->second.Allocation);
}

std::shared_ptr<Sqex::RandomAccessStream> Sqex::Sqpack::Reader::GetFile(const EntryPathSpec& pathSpec) const {
	return std::make_shared<BufferedRandomAccessStream>(std::make_shared<EntryRawStream>(GetEntryProvider(pathSpec)));
}

std::shared_ptr<Sqex::RandomAccessStream> Sqex::Sqpack::Reader::operator[](const EntryPathSpec& pathSpec) const {
	return std::make_shared<BufferedRandomAccessStream>(std::make_shared<EntryRawStream>(GetEntryProvider(pathSpec)));
}
