#include "pch.h"
#include "Sqex_Sqpack_StreamDecoder.h"

#include "Sqex_Sqpack_BinaryStreamDecoder.h"
#include "Sqex_Sqpack_ModelStreamDecoder.h"
#include "Sqex_Sqpack_TextureStreamDecoder.h"

void Sqex::Sqpack::StreamDecoder::ReadStreamState::AttemptSatisfyRequestOffset(const uint32_t requestOffset) {
	if (RequestOffsetVerify < requestOffset) {
		const auto padding = requestOffset - RequestOffsetVerify;
		if (RelativeOffset < padding) {
			const auto available = std::min<size_t>(TargetBuffer.size_bytes(), padding);
			std::fill_n(TargetBuffer.begin(), available, 0);
			TargetBuffer = TargetBuffer.subspan(available);
			RelativeOffset = 0;

		} else
			RelativeOffset -= padding;

		RequestOffsetVerify = requestOffset;

	} else if (RequestOffsetVerify > requestOffset)
		throw CorruptDataException("Duplicate read on same region");
}

void Sqex::Sqpack::StreamDecoder::ReadStreamState::Progress(const uint32_t requestOffset, uint32_t blockOffset) {
	const auto read = std::span(&ReadBuffer[0], static_cast<size_t>(Underlying.ReadStreamPartial(blockOffset, &ReadBuffer[0], ReadBuffer.size())));
	const auto& blockHeader = *reinterpret_cast<const SqData::BlockHeader*>(&ReadBuffer[0]);

	if (ReadBuffer.size() < sizeof blockHeader + blockHeader.CompressedSize) {
		ReadBuffer.resize(static_cast<uint16_t>(sizeof blockHeader + blockHeader.CompressedSize));
		Progress(requestOffset, blockOffset);
		return;
	}

	AttemptSatisfyRequestOffset(requestOffset);
	if (TargetBuffer.empty())
		return;

	RequestOffsetVerify += blockHeader.DecompressedSize;

	if (RelativeOffset < blockHeader.DecompressedSize) {
		auto target = TargetBuffer.subspan(0, std::min(TargetBuffer.size_bytes(), static_cast<size_t>(blockHeader.DecompressedSize - RelativeOffset)));
		if (blockHeader.CompressedSize == SqData::BlockHeader::CompressedSizeNotCompressed) {
			std::copy_n(&read[static_cast<size_t>(sizeof blockHeader + RelativeOffset)], target.size(), target.begin());
		} else {
			if (sizeof blockHeader + blockHeader.CompressedSize > read.size_bytes())
				throw CorruptDataException("Failed to read block");

			if (RelativeOffset) {
				const auto buf = Inflater(read.subspan(sizeof blockHeader, blockHeader.CompressedSize), blockHeader.DecompressedSize);
				if (buf.size_bytes() != blockHeader.DecompressedSize)
					throw CorruptDataException(std::format("Expected {} bytes, inflated to {} bytes",
						blockHeader.DecompressedSize.Value(), buf.size_bytes()));
				std::copy_n(&buf[static_cast<size_t>(RelativeOffset)],
					target.size_bytes(),
					target.begin());
			} else {
				const auto buf = Inflater(read.subspan(sizeof blockHeader, blockHeader.CompressedSize), target);
				if (buf.size_bytes() != target.size_bytes())
					throw CorruptDataException(std::format("Expected {} bytes, inflated to {} bytes",
						target.size_bytes(), buf.size_bytes()));
			}
		}
		TargetBuffer = TargetBuffer.subspan(target.size_bytes());

		RelativeOffset = 0;
	} else
		RelativeOffset -= blockHeader.DecompressedSize;
}

std::unique_ptr<Sqex::Sqpack::StreamDecoder> Sqex::Sqpack::StreamDecoder::CreateNew(const SqData::FileEntryHeader& header, std::shared_ptr<const EntryProvider> stream) {
	if (header.DecompressedSize == 0)
		return nullptr;

	switch (header.Type) {
		case SqData::FileEntryType::EmptyOrObfuscated:
			return nullptr;

		case SqData::FileEntryType::Binary:
			return std::make_unique<BinaryStreamDecoder>(header, std::move(stream));

		case SqData::FileEntryType::Texture:
			return std::make_unique<TextureStreamDecoder>(header, std::move(stream));

		case SqData::FileEntryType::Model:
			return std::make_unique<ModelStreamDecoder>(header, std::move(stream));

		default:
			throw Sqex::CorruptDataException("Unsupported type");
	}
}
