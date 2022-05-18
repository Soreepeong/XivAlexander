#pragma once

#include "PackedFileStream.h"

#include "Internal/ZlibWrapper.h"

namespace XivRes {
	class BasePackedFileStreamDecoder {
	protected:
		struct ReadStreamState {
			static constexpr auto ReadBufferMaxSize = 16384;

			uint8_t ReadBuffer[ReadBufferMaxSize];
			std::span<uint8_t> TargetBuffer;
			std::streamoff RelativeOffset = 0;
			size_t ReadBufferValidSize = 0;
			uint32_t RequestOffsetVerify = 0;
			bool HadCompressedBlocks = false;

			Internal::ZlibReusableInflater Inflater{ -MAX_WBITS };

			[[nodiscard]] const auto& AsHeader() const {
				return *reinterpret_cast<const PackedBlockHeader*>(ReadBuffer);
			}

		private:
			void AttemptSatisfyRequestOffset(const uint32_t requestOffset) {
				if (RequestOffsetVerify < requestOffset) {
					const auto padding = requestOffset - RequestOffsetVerify;
					if (RelativeOffset < padding) {
						const auto available = (std::min<size_t>)(TargetBuffer.size_bytes(), padding);
						std::fill_n(TargetBuffer.begin(), available, 0);
						TargetBuffer = TargetBuffer.subspan(available);
						RelativeOffset = 0;

					} else
						RelativeOffset -= padding;

					RequestOffsetVerify = requestOffset;

				} else if (RequestOffsetVerify > requestOffset)
					throw CorruptDataException("Duplicate read on same region");
			}

		public:
			void ProgressRead(const IStream& stream, uint32_t blockOffset, size_t knownBlockSize = ReadBufferMaxSize) {
				ReadBufferValidSize = static_cast<size_t>(stream.ReadStreamPartial(blockOffset, ReadBuffer, knownBlockSize));

				if (ReadBufferValidSize < sizeof AsHeader() || ReadBufferValidSize < AsHeader().TotalBlockSize())
					throw XivRes::CorruptDataException("Incomplete block read");

				if (sizeof ReadBuffer < AsHeader().TotalBlockSize())
					throw XivRes::CorruptDataException("sizeof blockHeader + blockHeader.CompressSize must be under 16K");
			}

			void ProgressDecode(const uint32_t requestOffset) {
				const auto read = std::span(ReadBuffer, ReadBufferValidSize);

				AttemptSatisfyRequestOffset(requestOffset);
				if (TargetBuffer.empty())
					return;

				RequestOffsetVerify += AsHeader().DecompressedSize;

				if (RelativeOffset < AsHeader().DecompressedSize) {
					auto target = TargetBuffer.subspan(0, (std::min)(TargetBuffer.size_bytes(), static_cast<size_t>(AsHeader().DecompressedSize - RelativeOffset)));
					if (AsHeader().IsCompressed()) {
						if (sizeof AsHeader() + AsHeader().CompressedSize > read.size_bytes())
							throw CorruptDataException("Failed to read block");

						if (RelativeOffset) {
							const auto buf = Inflater(read.subspan(sizeof AsHeader(), AsHeader().CompressedSize), AsHeader().DecompressedSize);
							if (buf.size_bytes() != AsHeader().DecompressedSize)
								throw CorruptDataException(std::format("Expected {} bytes, inflated to {} bytes",
									*AsHeader().DecompressedSize, buf.size_bytes()));
							std::copy_n(&buf[static_cast<size_t>(RelativeOffset)],
								target.size_bytes(),
								target.begin());
						} else {
							const auto buf = Inflater(read.subspan(sizeof AsHeader(), AsHeader().CompressedSize), target);
							if (buf.size_bytes() != target.size_bytes())
								throw CorruptDataException(std::format("Expected {} bytes, inflated to {} bytes",
									target.size_bytes(), buf.size_bytes()));
						}

					} else {
						std::copy_n(&read[static_cast<size_t>(sizeof AsHeader() + RelativeOffset)], target.size(), target.begin());
					}

					TargetBuffer = TargetBuffer.subspan(target.size_bytes());
					RelativeOffset = 0;

				} else
					RelativeOffset -= AsHeader().DecompressedSize;
			}
		};

		const std::shared_ptr<const PackedFileStream> m_stream;

	public:
		BasePackedFileStreamDecoder(std::shared_ptr<const PackedFileStream> stream)
			: m_stream(std::move(stream)) {
		}

		virtual std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) = 0;

		virtual ~BasePackedFileStreamDecoder() = default;

		static std::unique_ptr<BasePackedFileStreamDecoder> CreateNew(const PackedFileHeader& header, std::shared_ptr<const PackedFileStream> stream, std::span<uint8_t> obfuscatedHeaderRewrite = {});
	};
}

#include "BinaryPackedFileStreamDecoder.h"
#include "EmptyOrObfuscatedPackedFileStreamDecoder.h"
#include "ModelPackedFileStreamDecoder.h"
#include "TexturePackedFileStreamDecoder.h"

inline std::unique_ptr<XivRes::BasePackedFileStreamDecoder> XivRes::BasePackedFileStreamDecoder::CreateNew(const PackedFileHeader& header, std::shared_ptr<const PackedFileStream> stream, std::span<uint8_t> obfuscatedHeaderRewrite) {
	if (header.DecompressedSize == 0)
		return nullptr;

	switch (header.Type) {
		case PackedFileType::EmptyOrObfuscated:
			return std::make_unique<EmptyOrObfuscatedPackedFileStreamDecoder>(header, std::move(stream), obfuscatedHeaderRewrite);

		case PackedFileType::Binary:
			return std::make_unique<BinaryPackedFileStreamDecoder>(header, std::move(stream));

		case PackedFileType::Texture:
			return std::make_unique<TexturePackedFileStreamDecoder>(header, std::move(stream));

		case PackedFileType::Model:
			return std::make_unique<ModelPackedFileStreamDecoder>(header, std::move(stream));

		default:
			throw XivRes::CorruptDataException("Unsupported type");
	}
}
