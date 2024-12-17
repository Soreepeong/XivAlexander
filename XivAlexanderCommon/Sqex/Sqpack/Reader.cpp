#include "pch.h"
#include "XivAlexanderCommon/Sqex/Sqpack/Reader.h"

#include "XivAlexanderCommon/Sqex/Sqpack/EntryRawStream.h"
#include "XivAlexanderCommon/Sqex/Sqpack/RandomAccessStreamAsEntryProviderView.h"

template<typename HashLocatorT, typename TextLocatorT>
Sqex::Sqpack::Reader::SqIndexType<HashLocatorT, TextLocatorT>::SqIndexType(const RandomAccessStream* stream, bool strictVerify)
	: Data(stream ? stream->ReadStreamIntoVector<uint8_t>(0) : std::vector<uint8_t>())
	, Header(Data.empty() ? SqpackHeader{} : *reinterpret_cast<const SqpackHeader*>(&Data[0]))
	, IndexHeader(Data.empty() ? SqIndex::Header{} : *reinterpret_cast<const SqIndex::Header*>(&Data[Header.HeaderSize]))
	, HashLocators(Data.empty() ? std::span<const HashLocatorT>() : span_cast<HashLocatorT>(Data, IndexHeader.HashLocatorSegment.Offset, IndexHeader.HashLocatorSegment.Size, 1))
	, TextLocators(Data.empty() ? std::span<const TextLocatorT>() : span_cast<TextLocatorT>(Data, IndexHeader.TextLocatorSegment.Offset, IndexHeader.TextLocatorSegment.Size, 1))
	, Segment3(Data.empty() ? std::span<const SqIndex::Segment3Entry>() : span_cast<SqIndex::Segment3Entry>(Data, IndexHeader.UnknownSegment3.Offset, IndexHeader.UnknownSegment3.Size, 1)) {

	if (strictVerify) {
		Header.VerifySqpackHeader(SqpackType::SqIndex);
		IndexHeader.VerifySqpackIndexHeader(SqIndex::Header::IndexType::Index);
		if (IndexHeader.HashLocatorSegment.Size % sizeof(HashLocatorT))
			throw CorruptDataException("HashLocators has an invalid size alignment");
		if (IndexHeader.TextLocatorSegment.Size % sizeof(TextLocatorT))
			throw CorruptDataException("TextLocators has an invalid size alignment");
		if (IndexHeader.UnknownSegment3.Size % sizeof(SqIndex::Segment3Entry))
			throw CorruptDataException("Segment3 has an invalid size alignment");
		IndexHeader.HashLocatorSegment.Sha1.Verify(HashLocators, "HashLocatorSegment has invalid data SHA-1");
		IndexHeader.TextLocatorSegment.Sha1.Verify(TextLocators, "TextLocatorSegment has invalid data SHA-1");
		IndexHeader.UnknownSegment3.Sha1.Verify(Segment3, "UnknownSegment3 has invalid data SHA-1");
	}
}

Sqex::Sqpack::Reader::SqIndex1Type::SqIndex1Type(const RandomAccessStream* stream, bool strictVerify)
	: SqIndexType<SqIndex::PairHashLocator, SqIndex::PairHashWithTextLocator>(stream, strictVerify)
	, PathHashLocators(Data.empty() ? std::span<const SqIndex::PathHashLocator>() : span_cast<SqIndex::PathHashLocator>(Data, IndexHeader.PathHashLocatorSegment.Offset, IndexHeader.PathHashLocatorSegment.Size, 1)) {
	if (strictVerify) {
		if (IndexHeader.PathHashLocatorSegment.Size % sizeof SqIndex::PathHashLocator)
			throw CorruptDataException("PathHashLocators has an invalid size alignment");
		IndexHeader.PathHashLocatorSegment.Sha1.Verify(PathHashLocators, "PathHashLocatorSegment has invalid data SHA-1");
	}
}

std::span<const Sqex::Sqpack::SqIndex::PairHashLocator> Sqex::Sqpack::Reader::SqIndex1Type::GetPairHashLocators(uint32_t pathHash) const {
	const auto it = std::lower_bound(PathHashLocators.begin(), PathHashLocators.end(), pathHash, PathSpecComparator());
	if (it == PathHashLocators.end() || it->PathHash != pathHash)
		throw std::out_of_range(std::format("PathHash {:08x} not found", pathHash));

	return span_cast<SqIndex::PairHashLocator>(Data, it->PairHashLocatorOffset, it->PairHashLocatorSize, 1);
}

const Sqex::Sqpack::SqIndex::LEDataLocator& Sqex::Sqpack::Reader::SqIndex1Type::GetLocator(uint32_t pathHash, uint32_t nameHash) const {
	const auto locators = GetPairHashLocators(pathHash);
	const auto it = std::lower_bound(locators.begin(), locators.end(), nameHash, PathSpecComparator());
	if (it == locators.end() || it->NameHash != nameHash)
		throw std::out_of_range(std::format("NameHash {:08x} in PathHash {:08x} not found", nameHash, pathHash));
	return it->Locator;
}

Sqex::Sqpack::Reader::SqIndex2Type::SqIndex2Type(const RandomAccessStream* stream, bool strictVerify)
	: SqIndexType<SqIndex::FullHashLocator, SqIndex::FullHashWithTextLocator>(stream, strictVerify) {
}

const Sqex::Sqpack::SqIndex::LEDataLocator& Sqex::Sqpack::Reader::SqIndex2Type::GetLocator(uint32_t fullPathHash) const {
	const auto it = std::lower_bound(HashLocators.begin(), HashLocators.end(), fullPathHash, PathSpecComparator());
	if (it == HashLocators.end() || it->FullPathHash != fullPathHash)
		throw std::out_of_range(std::format("FullPathHash {:08x} not found", fullPathHash));
	return it->Locator;
}

Sqex::Sqpack::Reader::SqDataType::SqDataType(std::shared_ptr<RandomAccessStream> stream, const uint32_t datIndex, bool strictVerify)
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

static constexpr char EmptyIndexFileData[] = "\x53\x71\x50\x61\x63\x6b\x00\x00\x00\x00\x00\x00\x00\x04\x00\x00\x01\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x14\x03\x16\xfb\x3d\x2f\x7a\x61\xd8\xd9\x51\x20\x12\xe4\x4a\xf6\xa1\xe1\x45\x2e\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x04\x00\x00\x01\x00\x00\x00\x00\x08\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x08\x00\x00\x00\x01\x00\x00\x5e\x9d\x28\xd0\x48\x5d\xa8\x38\xf6\x2d\x71\x3c\x3d\xb6\x96\x1a\x6e\x13\xd8\x3b\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x09\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x57\x7f\x4d\xc3\x47\x77\xce\x82\xb2\xe9\xfe\xd5\x36\xe9\xf8\xb1\x49\x2b\xd9\x30\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00\x00\x00\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";

Sqex::Sqpack::Reader Sqex::Sqpack::Reader::FromPath(const std::filesystem::path& indexFile, bool strictVerify) {
	const std::filesystem::path index1Path = std::filesystem::path(indexFile).replace_extension(".index");
	const std::filesystem::path index2Path = std::filesystem::path(indexFile).replace_extension(".index2");

	std::vector<std::shared_ptr<RandomAccessStream>> dataStreams;
	for (int i = 0; i < 8; ++i) {
		const std::filesystem::path dataPath = std::filesystem::path(indexFile).replace_extension(std::format(".dat{}", i));
		if (!exists(dataPath))
			break;
		dataStreams.emplace_back(std::make_shared<FileRandomAccessStream>(dataPath));
	}

	if (exists(index1Path) && exists(index2Path)) {
		return Reader(
			FileRandomAccessStream(index1Path),
			FileRandomAccessStream(index2Path),
			dataStreams);
	}

	if (exists(index2Path)) {
		return Reader(
			MemoryRandomAccessStream(span_cast<uint8_t, char>(EmptyIndexFileData)),
			FileRandomAccessStream(index2Path),
			dataStreams);
	}

	if (exists(index1Path)) {
		return Reader(
			FileRandomAccessStream(index1Path),
			MemoryRandomAccessStream(span_cast<uint8_t, char>(EmptyIndexFileData)),
			dataStreams);
	}
	
	throw std::exception("No corresponding index file is found");
}

Sqex::Sqpack::Reader::Reader(const RandomAccessStream& indexStream1, const RandomAccessStream& indexStream2, std::vector<std::shared_ptr<RandomAccessStream>> dataStreams, bool strictVerify)
	: Index1(&indexStream1, strictVerify)
	, Index2(&indexStream2, strictVerify) {

	std::vector<std::pair<SqIndex::LEDataLocator, std::tuple<uint32_t, uint32_t, const char*>>> offsets1;
	offsets1.reserve(
		std::max(Index1.HashLocators.size() + Index1.TextLocators.size(), Index2.HashLocators.size() + Index2.TextLocators.size())
		+ Index1.IndexHeader.TextLocatorSegment.Count
	);
	for (const auto& item : Index1.HashLocators)
		if (!item.Locator.IsSynonym)
			offsets1.emplace_back(item.Locator, std::make_tuple(item.PathHash, item.NameHash, static_cast<const char*>(nullptr)));
	for (const auto& item : Index1.TextLocators) {
		if (item.PathHash == EntryPathSpec::EmptyHashValue && item.NameHash == EntryPathSpec::EmptyHashValue)
			continue;
		offsets1.emplace_back(item.Locator, std::make_tuple(item.PathHash, item.NameHash, item.FullPath));
	}

	std::vector<std::pair<SqIndex::LEDataLocator, std::tuple<uint32_t, const char*>>> offsets2;
	for (const auto& item : Index2.HashLocators)
		if (!item.Locator.IsSynonym)
			offsets2.emplace_back(item.Locator, std::make_tuple(item.FullPathHash, static_cast<const char*>(nullptr)));
	for (const auto& item : Index2.TextLocators) {
		if (item.FullPathHash == EntryPathSpec::EmptyHashValue)
			continue;
		offsets2.emplace_back(item.Locator, std::make_tuple(item.FullPathHash, item.FullPath));
	}

	if (strictVerify && offsets1.size() != offsets2.size())
		throw CorruptDataException(".index and .index2 do not have the same number of files contained");

	Data.reserve(dataStreams.size());
	for (uint32_t i = 0; i < dataStreams.size(); ++i) {
		Data.emplace_back(SqDataType{ dataStreams[i], i, strictVerify });
		if (!offsets1.empty())
			offsets1.emplace_back(SqIndex::LEDataLocator(i, Data[i].Stream->StreamSize()), std::make_tuple(UINT32_MAX, UINT32_MAX, static_cast<const char*>(nullptr)));
		if (!offsets2.empty())
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
	EntryInfo.reserve((std::max)(offsets1.size(), offsets2.size()));

	if (strictVerify && !offsets1.empty() && !offsets2.empty()) {
		for (size_t i = 0; i < offsets1.size(); ++i) {
			if (offsets1[i].first != offsets2[i].first)
				throw CorruptDataException(".index and .index2 have items with different locators");
			if (offsets1[i].first.IsSynonym)
				throw CorruptDataException("Synonym remains after conflict resolution");
		}
	}

	if (offsets1.empty()) {
		for (size_t curr = 1, prev = 0; curr < offsets2.size(); ++curr, ++prev) {
			if (offsets2[prev].first.DatFileIndex != offsets2[curr].first.DatFileIndex)
				continue;
			if (std::get<0>(offsets2[prev].second) == EntryPathSpec::EmptyHashValue)
				continue;
			EntryInfo.emplace_back(offsets2[prev].first, EntryInfoType{
				.PathSpec = EntryPathSpec(
					EntryPathSpec::EmptyHashValue,
					EntryPathSpec::EmptyHashValue,
					std::get<0>(offsets2[prev].second),
					std::get<1>(offsets2[prev].second) ? std::string(std::get<1>(offsets2[prev].second)) : std::string()
				),
				.Allocation = offsets2[curr].first.DatFileOffset() - offsets2[prev].first.DatFileOffset(),
				});
		}
	} else if (offsets2.empty()) {
		for (size_t curr = 1, prev = 0; curr < offsets1.size(); ++curr, ++prev) {
			if (offsets1[prev].first.DatFileIndex != offsets1[curr].first.DatFileIndex)
				continue;
			if (std::get<0>(offsets1[prev].second) == EntryPathSpec::EmptyHashValue
				&& std::get<1>(offsets1[prev].second) == EntryPathSpec::EmptyHashValue)
				continue;
			EntryInfo.emplace_back(offsets1[prev].first, EntryInfoType{
				.PathSpec = EntryPathSpec(
					std::get<0>(offsets1[prev].second),
					std::get<1>(offsets1[prev].second),
					EntryPathSpec::EmptyHashValue,
					std::get<2>(offsets1[prev].second) ? std::string(std::get<2>(offsets1[prev].second)) : std::string()
				),
				.Allocation = offsets1[curr].first.DatFileOffset() - offsets1[prev].first.DatFileOffset(),
				});
		}
	} else {
		for (size_t curr = 1, prev = 0; curr < offsets1.size(); ++curr, ++prev) {
			if (offsets1[prev].first.DatFileIndex != offsets1[curr].first.DatFileIndex)
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

	std::sort(EntryInfo.begin(), EntryInfo.end(), Comparator());
}

const Sqex::Sqpack::SqIndex::LEDataLocator& Sqex::Sqpack::Reader::GetLocator(const EntryPathSpec& pathSpec) const {
	try {
		if (pathSpec.HasFullPathHash()) {
			const auto& locator = Index2.GetLocator(pathSpec.FullPathHash);
			if (locator.IsSynonym)
				return Index2.GetLocatorFromTextLocators(pathSpec.NativeRepresentation().c_str());
			return locator;
		}
		if (pathSpec.HasComponentHash()) {
			const auto& locator = Index1.GetLocator(pathSpec.PathHash, pathSpec.NameHash);
			if (locator.IsSynonym)
				return Index1.GetLocatorFromTextLocators(pathSpec.NativeRepresentation().c_str());
			return locator;
		}
	} catch (const std::out_of_range& e) {
		throw std::out_of_range(std::format("Failed to find {}: {}", pathSpec, e.what()));
	}
	throw std::out_of_range(std::format("Path spec is empty"));
}

std::shared_ptr<Sqex::Sqpack::EntryProvider> Sqex::Sqpack::Reader::GetEntryProvider(const EntryPathSpec& pathSpec, SqIndex::LEDataLocator locator, uint64_t allocation) const {
	return std::make_shared<RandomAccessStreamAsEntryProviderView>(pathSpec, Data.at(locator.DatFileIndex).Stream, locator.DatFileOffset(), allocation);
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
	const auto entryInfo = std::lower_bound(EntryInfo.begin(), EntryInfo.end(), locator, Comparator());
	return GetEntryProvider(pathSpec, locator, entryInfo->second.Allocation);
}

std::shared_ptr<Sqex::RandomAccessStream> Sqex::Sqpack::Reader::GetFile(const EntryPathSpec& pathSpec) const {
	return std::make_shared<BufferedRandomAccessStream>(std::make_shared<EntryRawStream>(GetEntryProvider(pathSpec)));
}

std::shared_ptr<Sqex::RandomAccessStream> Sqex::Sqpack::Reader::operator[](const EntryPathSpec& pathSpec) const {
	return std::make_shared<BufferedRandomAccessStream>(std::make_shared<EntryRawStream>(GetEntryProvider(pathSpec)));
}

Sqex::Sqpack::GameReader::GameReader(std::filesystem::path gamePath)
	: m_gamePath(std::move(gamePath)) {
}

std::shared_ptr<Sqex::Sqpack::EntryProvider> Sqex::Sqpack::GameReader::GetEntryProvider(const EntryPathSpec& pathSpec) const {
	if (pathSpec.HasOriginal())
		return GetReaderForPath(pathSpec).GetEntryProvider(pathSpec);

	PreloadAllSqpackFiles();
	for (const auto& reader : m_readers | std::views::values) {
		try {
			return reader->GetEntryProvider(pathSpec);
		} catch (const std::out_of_range&) {
			// pass
		}
	}
	throw std::out_of_range("File not found in any sqpack file");
}

std::shared_ptr<Sqex::RandomAccessStream> Sqex::Sqpack::GameReader::GetFile(const EntryPathSpec& pathSpec) const {
	return std::make_shared<BufferedRandomAccessStream>(std::make_shared<EntryRawStream>(GetEntryProvider(pathSpec)));
}

std::shared_ptr<Sqex::RandomAccessStream> Sqex::Sqpack::GameReader::operator[](const EntryPathSpec& pathSpec) const {
	return GetFile(pathSpec);
}

Sqex::Sqpack::Reader& Sqex::Sqpack::GameReader::GetReaderForPath(const EntryPathSpec& rawPathSpec) const {
	const auto lock = std::lock_guard(m_readersMtx);
	const auto datFileName = rawPathSpec.DatFile();
	auto& item = m_readers[datFileName];
	if (!item)
		item.emplace(Reader::FromPath(m_gamePath / "sqpack" / rawPathSpec.DatExpac() / (datFileName + ".win32.index")));
	return *item;
}

void Sqex::Sqpack::GameReader::PreloadAllSqpackFiles() const {
	const auto lock = std::lock_guard(m_readersMtx);

	for (const auto& iter : std::filesystem::recursive_directory_iterator(m_gamePath / "sqpack")) {
		if (iter.is_directory() || !iter.path().wstring().ends_with(L".win32.index"))
			continue;
		const auto datFileName = std::filesystem::path{ iter.path() }.replace_extension("").replace_extension("").string();
		auto& item = m_readers[datFileName];
		if (!item)
			item.emplace(Sqex::Sqpack::Reader::FromPath(iter.path()));
	}
}
