#ifndef _XIVRES_TEXTURESTREAM_H_
#define _XIVRES_TEXTURESTREAM_H_

#include "Common.h"
#include "Texture.h"
#include "MipmapStream.h"

namespace XivRes {
	class TextureStream : public DefaultAbstractStream {
		TextureHeader m_header;
		std::vector<std::vector<std::shared_ptr<MipmapStream>>> m_repeats;
		std::vector<uint32_t> m_mipmapOffsets;
		uint32_t m_repeatedUnitSize;

	public:
		TextureStream(const std::shared_ptr<IStream>& stream)
			: m_header(ReadStream<TextureHeader>(*stream, 0))
			, m_repeats(0)
			, m_repeatedUnitSize(0) {

			const auto mipmapLocators = ReadStreamIntoVector<uint32_t>(*stream, sizeof m_header, m_header.MipmapCount);
			const auto repeatUnitSize = CalculateRepeatUnitSize(m_header.MipmapCount);
			Resize(mipmapLocators.size(), (static_cast<size_t>(stream->StreamSize()) - mipmapLocators[0] + repeatUnitSize - 1) / repeatUnitSize);
			for (size_t repeatI = 0; repeatI < m_repeats.size(); ++repeatI) {
				for (size_t mipmapI = 0; mipmapI < mipmapLocators.size(); ++mipmapI) {
					const auto mipmapSize = TextureRawDataLength(m_header, mipmapI);
					auto mipmapDataView = std::make_shared<XivRes::PartialViewStream>(stream, repeatUnitSize * repeatI + mipmapLocators[mipmapI], mipmapSize);
					auto mipmapView = std::make_shared<WrappedMipmapStream>(m_header, mipmapI, std::move(mipmapDataView));
					SetMipmap(mipmapI, repeatI, std::move(mipmapView));
				}
			}
		}

		TextureStream(TextureFormat type, size_t width, size_t height, size_t depth = 1, size_t mipmapCount = 1, size_t repeatCount = 1)
			: m_header({
				.Unknown1 = 0,
				.HeaderSize = static_cast<uint16_t>(Align(sizeof m_header)),
				.Type = type,
				.Width = Internal::RangeCheckedCast<uint16_t>(width),
				.Height = Internal::RangeCheckedCast<uint16_t>(height),
				.Depth = Internal::RangeCheckedCast<uint16_t>(depth),
				.MipmapCount = 0,
				.Unknown2 = {}
				})
			, m_repeats(0)
			, m_repeatedUnitSize(0) {
			Resize(mipmapCount, repeatCount);
		}

		void SetMipmap(size_t mipmapIndex, size_t repeatIndex, std::shared_ptr<MipmapStream> mipmap) {
			auto& mipmaps = m_repeats.at(repeatIndex);
			const auto w = (std::max)(1, m_header.Width >> mipmapIndex);
			const auto h = (std::max)(1, m_header.Height >> mipmapIndex);
			const auto l = (std::max)(1, m_header.Depth >> mipmapIndex);

			if (mipmap->Width != w)
				throw std::invalid_argument("invalid mipmap width");
			if (mipmap->Height != h)
				throw std::invalid_argument("invalid mipmap height");
			if (mipmap->Depth != l)
				throw std::invalid_argument("invalid mipmap depths");
			if (mipmap->Type != m_header.Type)
				throw std::invalid_argument("invalid mipmap type");
			if (mipmap->StreamSize() != TextureRawDataLength(mipmap->Type, w, h, l))
				throw std::invalid_argument("invalid mipmap size");

			mipmaps.at(mipmapIndex) = std::move(mipmap);
		}

		void Resize(size_t mipmapCount, size_t repeatCount) {
			if (mipmapCount == 0)
				throw std::invalid_argument("mipmap count must be a positive integer");
			if (repeatCount == 0)
				throw std::invalid_argument("repeat count must be a positive integer");

			m_header.MipmapCount = Internal::RangeCheckedCast<uint16_t>(mipmapCount);
			m_header.HeaderSize = static_cast<uint32_t>(Align(sizeof m_header + std::span(m_mipmapOffsets).size_bytes()));

			m_repeats.resize(repeatCount = Internal::RangeCheckedCast<uint16_t>(repeatCount));
			for (auto& mipmaps : m_repeats)
				mipmaps.resize(mipmapCount);

			m_mipmapOffsets.clear();
			m_repeatedUnitSize = 0;
			for (size_t i = 0; i < mipmapCount; ++i) {
				m_mipmapOffsets.push_back(m_header.HeaderSize + m_repeatedUnitSize);
				m_repeatedUnitSize += static_cast<uint32_t>(Align(TextureRawDataLength(m_header, i)).Alloc);
			}
		}

		[[nodiscard]] std::streamsize StreamSize() const override {
			return Align(sizeof m_header + std::span(m_mipmapOffsets).size_bytes()) + m_repeats.size() * m_repeatedUnitSize;
		}

		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const override {
			if (!length)
				return 0;

			auto relativeOffset = offset;
			auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

			if (relativeOffset < sizeof m_header) {
				const auto src = Internal::span_cast<uint8_t>(1, &m_header).subspan(static_cast<size_t>(relativeOffset));
				const auto available = (std::min)(out.size_bytes(), src.size_bytes());
				std::copy_n(src.begin(), available, out.begin());
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty())
					return length;
			} else
				relativeOffset -= sizeof m_header;

			if (const auto srcTyped = std::span(m_mipmapOffsets);
				relativeOffset < static_cast<std::streamoff>(srcTyped.size_bytes())) {
				const auto src = Internal::span_cast<uint8_t>(srcTyped).subspan(static_cast<size_t>(relativeOffset));
				const auto available = (std::min)(out.size_bytes(), src.size_bytes());
				std::copy_n(src.begin(), available, out.begin());
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty())
					return length;
			} else
				relativeOffset -= srcTyped.size_bytes();

			const auto headerPadInfo = Align(sizeof m_header + std::span(m_mipmapOffsets).size_bytes());
			if (const auto padSize = static_cast<std::streamoff>(headerPadInfo.Pad);
				relativeOffset < padSize) {
				const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(padSize - relativeOffset));
				std::fill_n(out.begin(), available, 0);
				out = out.subspan(static_cast<size_t>(available));
				relativeOffset = 0;

				if (out.empty())
					return length;
			} else
				relativeOffset -= padSize;

			if (m_repeats.empty())
				return length - out.size_bytes();

			auto beginningRepeatIndex = static_cast<size_t>(relativeOffset / m_repeatedUnitSize);
			relativeOffset %= m_repeatedUnitSize;

			relativeOffset += headerPadInfo.Alloc;

			auto it = std::ranges::lower_bound(m_mipmapOffsets,
				static_cast<uint32_t>(relativeOffset),
				[&](uint32_t l, uint32_t r) {
					return l < r;
				});

			if (it == m_mipmapOffsets.end() || *it > relativeOffset)
				--it;

			relativeOffset -= *it;

			for (auto repeatI = beginningRepeatIndex; repeatI < m_repeats.size(); ++repeatI) {
				for (auto mipmapI = it - m_mipmapOffsets.begin(); it != m_mipmapOffsets.end(); ++it, ++mipmapI) {
					std::streamoff padSize;
					if (const auto& mipmap = m_repeats[repeatI][mipmapI]) {
						const auto mipmapSize = mipmap->StreamSize();
						padSize = XivRes::Align(mipmapSize).Pad;

						if (relativeOffset < mipmapSize) {
							const auto available = static_cast<size_t>((std::min<uint64_t>)(out.size_bytes(), mipmapSize - relativeOffset));
							ReadStream(*mipmap, relativeOffset, out.data(), available);
							out = out.subspan(available);
							relativeOffset = 0;

							if (out.empty())
								return length;
						} else {
							relativeOffset -= mipmapSize;
						}

					} else {
						padSize = XivRes::Align(TextureRawDataLength(m_header, mipmapI)).Alloc;
					}

					if (relativeOffset < padSize) {
						const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(padSize - relativeOffset));
						std::fill_n(out.begin(), available, 0);
						out = out.subspan(static_cast<size_t>(available));
						relativeOffset = 0;

						if (out.empty())
							return length;
					} else
						relativeOffset -= padSize;
				}
				it = m_mipmapOffsets.begin();
			}

			return length - out.size_bytes();
		}

		[[nodiscard]] TextureFormat GetType() const {
			return m_header.Type;
		}

		[[nodiscard]] uint16_t GetWidth() const {
			return m_header.Width;
		}

		[[nodiscard]] uint16_t GetHeight() const {
			return m_header.Height;
		}

		[[nodiscard]] uint16_t GetDepth() const {
			return m_header.Depth;
		}

		[[nodiscard]] uint16_t GetMipmapCount() const {
			return m_header.MipmapCount;
		}

		[[nodiscard]] uint16_t GetRepeatCount() const {
			return static_cast<uint16_t>(m_repeats.size());
		}

		[[nodiscard]] size_t CalculateRepeatUnitSize(size_t mipmapCount) const {
			size_t res = 0;
			for (size_t i = 0; i < mipmapCount; ++i)
				res += static_cast<uint32_t>(Align(TextureRawDataLength(m_header, i)).Alloc);
			return res;
		}

		[[nodiscard]] std::shared_ptr<MipmapStream> GetMipmap(size_t repeatIndex, size_t mipmapIndex) const {
			return m_repeats.at(repeatIndex).at(mipmapIndex);
		}
	};

	std::shared_ptr<TextureStream> MipmapStream::ToSingleTextureStream() {
		auto res = std::make_shared<TextureStream>(Type, Width, Height);
		res->SetMipmap(0, 0, std::dynamic_pointer_cast<MipmapStream>(this->shared_from_this()));
		return res;
	}
}

#endif
