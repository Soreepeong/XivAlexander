#include "pch.h"
#include "Sqex_Sqpack_Reader.h"

#include "Sqex_Sqpack_EntryRawStream.h"

Sqex::Sqpack::Reader::SqIndexType::SqIndexType(const Win32::Handle& hFile, bool strictVerify) {
	std::vector<std::pair<size_t, size_t>> accesses;

	hFile.Read(0, &Header, sizeof SqpackHeader);
	if (strictVerify) {
		accesses.emplace_back(0, Header.HeaderSize);
		Header.VerifySqpackHeader(SqpackType::SqIndex);
	}

	hFile.Read(Header.HeaderSize, &IndexHeader, sizeof SqIndex::Header);
	if (strictVerify) {
		accesses.emplace_back(Header.HeaderSize, IndexHeader.HeaderSize);
		IndexHeader.VerifySqpackIndexHeader(SqIndex::Header::IndexType::Index);
	}

	Files.resize(IndexHeader.FileSegment.Size / sizeof Files[0]);
	hFile.Read(IndexHeader.FileSegment.Offset, std::span(Files));
	if (strictVerify) {
		accesses.emplace_back(IndexHeader.FileSegment.Offset, IndexHeader.FileSegment.Size);
		IndexHeader.FileSegment.Sha1.Verify(std::span(Files), "FileSegment Data SHA-1");
	}

	HashConflictSegment.resize(IndexHeader.HashConflictSegment.Size / sizeof HashConflictSegment[0]);
	hFile.Read(IndexHeader.HashConflictSegment.Offset, std::span(HashConflictSegment));
	if (strictVerify) {
		accesses.emplace_back(IndexHeader.HashConflictSegment.Offset, IndexHeader.HashConflictSegment.Size);
		IndexHeader.HashConflictSegment.Sha1.Verify(std::span(HashConflictSegment), "HashConflictSegment Data SHA-1");
	}

	Segment3.resize(IndexHeader.UnknownSegment3.Size / sizeof Segment3[0]);
	hFile.Read(IndexHeader.UnknownSegment3.Offset, std::span(Segment3));
	if (strictVerify) {
		accesses.emplace_back(IndexHeader.UnknownSegment3.Offset, IndexHeader.UnknownSegment3.Size);
		IndexHeader.UnknownSegment3.Sha1.Verify(std::span(Segment3), "UnknownSegment3 Data SHA-1");
	}

	Folders.resize(IndexHeader.FolderSegment.Size / sizeof Folders[0]);
	hFile.Read(IndexHeader.FolderSegment.Offset, std::span(Folders));
	if (strictVerify) {
		accesses.emplace_back(IndexHeader.FolderSegment.Offset, IndexHeader.FolderSegment.Size);
		IndexHeader.FolderSegment.Sha1.Verify(std::span(Folders), "FolderSegment Data SHA-1");
	}

	if (strictVerify) {
		std::ranges::sort(accesses);
		auto ptr = accesses[0].first;
		for (const auto& [accessPointer, accessSize] : accesses) {
			if (ptr > accessPointer)
				throw CorruptDataException("Unread region found");
			else if (ptr < accessPointer)
				throw CorruptDataException("Overlapping region found");
			ptr += accessSize;
		}
		if (ptr != hFile.GetFileSize())
			throw CorruptDataException("Trailing region found");
	}
}

Sqex::Sqpack::Reader::SqIndex2Type::SqIndex2Type(const Win32::Handle& hFile, bool strictVerify) {
	std::vector<std::pair<size_t, size_t>> accesses;

	hFile.Read(0, &Header, sizeof SqpackHeader);
	if (strictVerify) {
		accesses.emplace_back(0, Header.HeaderSize);
		Header.VerifySqpackHeader(SqpackType::SqIndex);
	}

	hFile.Read(Header.HeaderSize, &IndexHeader, sizeof SqIndex::Header);
	accesses.emplace_back(Header.HeaderSize, IndexHeader.HeaderSize);
	if (strictVerify)
		IndexHeader.VerifySqpackIndexHeader(SqIndex::Header::IndexType::Index2);

	Files.resize(IndexHeader.FileSegment.Size / sizeof Files[0]);
	hFile.Read(IndexHeader.FileSegment.Offset, std::span(Files));
	if (strictVerify) {
		accesses.emplace_back(IndexHeader.FileSegment.Offset, IndexHeader.FileSegment.Size);
		IndexHeader.FileSegment.Sha1.Verify(std::span(Files), "FileSegment Data SHA-1");
	}

	HashConflictSegment.resize(IndexHeader.HashConflictSegment.Size / sizeof HashConflictSegment[0]);
	hFile.Read(IndexHeader.HashConflictSegment.Offset, std::span(HashConflictSegment));
	if (strictVerify) {
		accesses.emplace_back(IndexHeader.HashConflictSegment.Offset, IndexHeader.HashConflictSegment.Size);
		IndexHeader.HashConflictSegment.Sha1.Verify(std::span(HashConflictSegment), "HashConflictSegment Data SHA-1");
	}

	Segment3.resize(IndexHeader.UnknownSegment3.Size / sizeof Segment3[0]);
	hFile.Read(IndexHeader.UnknownSegment3.Offset, std::span(Segment3));
	if (strictVerify) {
		accesses.emplace_back(IndexHeader.UnknownSegment3.Offset, IndexHeader.UnknownSegment3.Size);
		IndexHeader.UnknownSegment3.Sha1.Verify(std::span(Segment3), "UnknownSegment3 Data SHA-1");
	}

	Folders.resize(IndexHeader.FolderSegment.Size / sizeof Folders[0]);
	hFile.Read(IndexHeader.FolderSegment.Offset, std::span(Folders));
	if (strictVerify) {
		accesses.emplace_back(IndexHeader.FolderSegment.Offset, IndexHeader.FolderSegment.Size);
		IndexHeader.FolderSegment.Sha1.Verify(std::span(Folders), "FolderSegment Data SHA-1");
	}

	if (strictVerify) {
		std::ranges::sort(accesses);
		auto ptr = accesses[0].first;
		for (const auto& [accessPointer, accessSize] : accesses) {
			if (ptr > accessPointer)
				throw CorruptDataException("Unread region found");
			else if (ptr < accessPointer)
				throw CorruptDataException("Overlapping region found");
			ptr += accessSize;
		}
		if (ptr != hFile.GetFileSize())
			throw CorruptDataException("Trailing region found");
	}
}

Sqex::Sqpack::Reader::SqDataType::SqDataType(Win32::Handle hFile, const uint32_t datIndex, bool strictVerify)
	: Stream(std::make_shared<FileRandomAccessStream>(std::move(hFile))) {
	std::vector<std::pair<size_t, size_t>> accesses;

	Stream->ReadStream(0, &Header, sizeof SqpackHeader);
	if (strictVerify) {
		if (datIndex == 0)
			Header.VerifySqpackHeader(SqpackType::SqData);
		accesses.emplace_back(0, sizeof SqpackHeader);
	}

	Stream->ReadStream(sizeof SqpackHeader, &DataHeader, sizeof SqData::Header);
	if (strictVerify) {
		if (datIndex == 0)
			DataHeader.Verify(datIndex + 1);
		accesses.emplace_back(sizeof SqpackHeader, sizeof SqData::Header);
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
	: Index(Win32::Handle::FromCreateFile(std::filesystem::path(indexFile).replace_extension(".index"), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN), strictVerify)
	, Index2(Win32::Handle::FromCreateFile(std::filesystem::path(indexFile).replace_extension(".index2"), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN), strictVerify) {

	std::vector<std::set<uint64_t>> offsets(Index.IndexHeader.HashConflictSegment.Count);
	for (const auto& item : Index.Files)
		if (!item.Locator.HasConflicts())
			offsets[item.Locator.Index()].insert(item.Locator.Offset());
	for (const auto& item : Index.HashConflictSegment)
		offsets[item.Locator.Index()].insert(item.Locator.Offset());
	for (const auto& item : Index2.Files)
		if (!item.Locator.HasConflicts())
			offsets[item.Locator.Index()].insert(item.Locator.Offset());
	for (const auto& item : Index2.HashConflictSegment)
		offsets[item.Locator.Index()].insert(item.Locator.Offset());

	Data.reserve(Index.IndexHeader.HashConflictSegment.Count);
	for (uint32_t i = 0; i < Index.IndexHeader.HashConflictSegment.Count; ++i) {
		Data.emplace_back(SqDataType{
			Win32::Handle::FromCreateFile(std::filesystem::path(indexFile).replace_extension(std::format(".dat{}", i)), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0),
			i,
			strictVerify,
			});
		offsets[i].insert(Data.back().Stream->StreamSize());

		for (auto itnext = offsets[i].begin(), it = itnext++; itnext != offsets[i].end(); ++itnext, ++it) {
			FileSizes[SqIndex::LEDataLocator(i, *it).Value()] = *itnext - *it;
		}
	}
}

std::shared_ptr<Sqex::Sqpack::EntryProvider> Sqex::Sqpack::Reader::GetEntryProvider(const EntryPathSpec& pathSpec, SqIndex::LEDataLocator locator) const {
	static const auto pathSpecComparator = PathSpecComparator();
	
	if (locator) {
		// pass

	} else if (pathSpec.HasComponentHash()) {
		const auto folder = std::lower_bound(Index.Folders.begin(), Index.Folders.end(), pathSpec, pathSpecComparator);
		if (folder == Index.Folders.end() || folder->PathHash != pathSpec.PathHash)
			throw std::out_of_range(std::format("Entry {} not found (no corresponding folder from .index)", pathSpec));

		const auto folderContents = std::span(
			Index.Files.begin() + (static_cast<size_t>(0) + folder->FileSegmentOffset - Index.IndexHeader.FileSegment.Offset) / sizeof Index.Files[0],
			Index.Files.begin() + (static_cast<size_t>(0) + folder->FileSegmentOffset + folder->FileSegmentSize - Index.IndexHeader.FileSegment.Offset) / sizeof Index.Files[0]
		);

		const auto file = std::lower_bound(folderContents.begin(), folderContents.end(), pathSpec, pathSpecComparator);
		if (file == folderContents.end() || file->NameHash != pathSpec.NameHash)
			throw std::out_of_range(std::format("Entry {} not found (no corresponding file from .index)", pathSpec));

		if (file->Locator.HasConflicts()) {
			const auto resolved = std::lower_bound(Index.HashConflictSegment.begin(), Index.HashConflictSegment.end(), pathSpec, pathSpecComparator);
			if (resolved == Index.HashConflictSegment.end() || lstrcmpiW(Utils::FromUtf8(resolved->FullPath).c_str(), pathSpec.Original.c_str()) != 0)
				throw std::out_of_range(std::format("Entry {} not found (no corresponding conflict resolution from .index)", pathSpec));
			else
				locator = resolved->Locator;
		} else
			locator = file->Locator;

	} else if (pathSpec.HasFullPathHash()) {
		const auto file = std::lower_bound(Index2.Files.begin(), Index2.Files.end(), pathSpec, pathSpecComparator);
		if (file == Index2.Files.end())
			throw std::out_of_range(std::format("Entry {} not found (no corresponding file from .index2)", pathSpec));

		if (file->Locator.HasConflicts()) {
			const auto resolved = std::lower_bound(Index2.HashConflictSegment.begin(), Index2.HashConflictSegment.end(), pathSpec, pathSpecComparator);
			if (resolved == Index2.HashConflictSegment.end() || lstrcmpiW(Utils::FromUtf8(resolved->FullPath).c_str(), pathSpec.Original.c_str()) != 0)
				throw std::out_of_range(std::format("Entry {} not found (no corresponding conflict resolution from .index2)", pathSpec));
			else
				locator = resolved->Locator;
		} else
			locator = file->Locator;

	}
	
	if (!locator)
		throw std::out_of_range(std::format("Path spec is empty"));

	if (locator.HasConflicts())
		throw std::out_of_range(std::format("Conflict resolution still resulted in conflict"));

	return std::make_shared<RandomAccessStreamAsEntryProviderView>(pathSpec, Data.at(locator.Index()).Stream, locator.Offset(), FileSizes.at(locator.Value()));
}

std::shared_ptr<Sqex::RandomAccessStream> Sqex::Sqpack::Reader::operator[](const EntryPathSpec& pathSpec) const {
	return std::make_shared<BufferedRandomAccessStream>(std::make_shared<EntryRawStream>(GetEntryProvider(pathSpec)));
}
