#include "pch.h"
#include "XivAlexanderCommon/Sqex/Texture/ModifiableTextureStream.h"

#include "XivAlexanderCommon/Sqex/Sqpack.h"

Sqex::Texture::ModifiableTextureStream::ModifiableTextureStream(Format type, uint16_t width, uint16_t height, uint16_t depth, uint16_t mipmapCount, uint16_t repeatCount)
	: m_header{
		.Unknown1 = 0,
		.HeaderSize = static_cast<uint16_t>(Align(sizeof m_header)),
		.Type = type,
		.Width = width,
		.Height = height,
		.Depth = depth,
		.MipmapCount = 0,
		.Unknown2 = {}
	}
	, m_repeats(0)
	, m_repeatedUnitSize(0) {
	Resize(mipmapCount, repeatCount);
}

Sqex::Texture::ModifiableTextureStream::ModifiableTextureStream(const std::shared_ptr<RandomAccessStream>& stream)
	: m_header{ stream->ReadStream<Texture::Header>(0) }
	, m_repeats(0)
	, m_repeatedUnitSize(0) {
	
	const auto mipmapLocators = stream->ReadStreamIntoVector<uint32_t>(sizeof m_header, m_header.MipmapCount);
	const auto repeatUnitSize = CalculateRepeatUnitSize(m_header.MipmapCount);
	Resize(mipmapLocators.size(), (stream->StreamSize() - mipmapLocators[0] + repeatUnitSize - 1) / repeatUnitSize);
	for (size_t repeatI = 0; repeatI < m_repeats.size(); ++repeatI) {
		for (size_t mipmapI = 0; mipmapI < mipmapLocators.size(); ++mipmapI) {
			const auto mipmapSize = RawDataLength(m_header, mipmapI);
			auto mipmapDataView = std::make_shared<Sqex::RandomAccessStreamPartialView>(stream, repeatUnitSize * repeatI + mipmapLocators[mipmapI], mipmapSize);
			auto mipmapView = std::make_shared<WrappedMipmapStream>(m_header, mipmapI, std::move(mipmapDataView));
			SetMipmap(mipmapI, repeatI, std::move(mipmapView));
		}
	}
}

Sqex::Texture::ModifiableTextureStream::~ModifiableTextureStream() = default;

std::shared_ptr<Sqex::Texture::MipmapStream> Sqex::Texture::ModifiableTextureStream::GetMipmap(size_t mipmapIndex, size_t repeatIndex) const {
	return m_repeats.at(repeatIndex).at(mipmapIndex);
}

void Sqex::Texture::ModifiableTextureStream::SetMipmap(size_t mipmapIndex, size_t repeatIndex, std::shared_ptr<MipmapStream> mipmap) {
	auto& mipmaps = m_repeats.at(repeatIndex);
	const auto w = std::max(1, m_header.Width >> mipmapIndex);
	const auto h = std::max(1, m_header.Height >> mipmapIndex);
	const auto d = std::max(1, m_header.Depth >> mipmapIndex);

	if (mipmap->Width != w)
		throw std::invalid_argument("invalid mipmap width");
	if (mipmap->Height != h)
		throw std::invalid_argument("invalid mipmap height");
	if (mipmap->Depth != d)
		throw std::invalid_argument("invalid mipmap layers");
	if (mipmap->Type != m_header.Type)
		throw std::invalid_argument("invalid mipmap type");
	if (mipmap->StreamSize() != RawDataLength(mipmap->Type, w, h, d))
		throw std::invalid_argument("invalid mipmap size");

	mipmaps.at(mipmapIndex) = std::move(mipmap);
}

void Sqex::Texture::ModifiableTextureStream::Resize(size_t mipmapCount, size_t repeatCount) {
	if (mipmapCount == 0)
		throw std::invalid_argument("mipmap count must be a positive integer");
	if (repeatCount == 0)
		throw std::invalid_argument("repeat count must be a positive integer");

	m_repeats.resize(repeatCount);
	for (auto& mipmaps : m_repeats)
		mipmaps.resize(mipmapCount);

	m_header.MipmapCount = static_cast<uint16_t>(mipmapCount);
	m_header.HeaderSize = static_cast<uint32_t>(Align(sizeof m_header + std::span(m_mipmapOffsets).size_bytes()));

	m_mipmapOffsets.clear();
	m_repeatedUnitSize = 0;
	for (size_t i = 0; i < mipmapCount; ++i) {
		m_mipmapOffsets.push_back(m_header.HeaderSize + m_repeatedUnitSize);
		m_repeatedUnitSize += static_cast<uint32_t>(Align(RawDataLength(m_header, i)).Alloc);
	}
}

uint64_t Sqex::Texture::ModifiableTextureStream::StreamSize() const {
	return Align(sizeof m_header + std::span(m_mipmapOffsets).size_bytes()) + m_repeats.size() * m_repeatedUnitSize;
}

uint64_t Sqex::Texture::ModifiableTextureStream::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const {
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

	if (relativeOffset < sizeof m_header) {
		const auto src = std::span(reinterpret_cast<const char*>(&m_header), sizeof m_header)
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length;
	} else
		relativeOffset -= sizeof m_header;

	if (const auto srcTyped = std::span(m_mipmapOffsets);
		relativeOffset < srcTyped.size_bytes()) {
		const auto src = std::span(reinterpret_cast<const char*>(srcTyped.data()), srcTyped.size_bytes())
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length;
	} else
		relativeOffset -= srcTyped.size_bytes();

	const auto headerPadInfo = Align(sizeof m_header + std::span(m_mipmapOffsets).size_bytes());
	if (const auto padSize = headerPadInfo.Pad;
		relativeOffset < padSize) {
		const auto available = std::min(out.size_bytes(), static_cast<size_t>(padSize - relativeOffset));
		std::fill_n(out.begin(), available, 0);
		out = out.subspan(static_cast<size_t>(available));
		relativeOffset = 0;

		if (out.empty())
			return length;
	} else
		relativeOffset -= padSize;

	if (m_repeats.empty())
		return length - out.size_bytes();

	auto beginningRepeatIndex = relativeOffset / m_repeatedUnitSize;
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
			uint64_t padSize;
			if (const auto& mipmap = m_repeats[repeatI][mipmapI]) {
				const auto mipmapSize = mipmap->StreamSize();
				padSize = Sqex::Align(mipmapSize).Pad;

				if (relativeOffset < mipmapSize) {
					const auto available = static_cast<size_t>(std::min<uint64_t>(out.size_bytes(), mipmapSize - relativeOffset));
					mipmap->ReadStream(relativeOffset, out.data(), available);
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty())
						return length;
				} else {
					relativeOffset -= mipmapSize;
				}

			} else {
				padSize = Sqex::Align(RawDataLength(m_header, mipmapI)).Alloc;
			}

			if (relativeOffset < padSize) {
				const auto available = std::min(out.size_bytes(), static_cast<size_t>(padSize - relativeOffset));
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

Sqex::Texture::Format Sqex::Texture::ModifiableTextureStream::GetType() const {
	return m_header.Type;
}

uint16_t Sqex::Texture::ModifiableTextureStream::GetWidth() const {
	return m_header.Width;
}

uint16_t Sqex::Texture::ModifiableTextureStream::GetHeight() const {
	return m_header.Height;
}

uint16_t Sqex::Texture::ModifiableTextureStream::GetDepth() const {
	return m_header.Depth;
}

uint16_t Sqex::Texture::ModifiableTextureStream::GetMipmapCount() const {
	return m_header.MipmapCount;
}

uint16_t Sqex::Texture::ModifiableTextureStream::GetRepeatCount() const {
	return static_cast<uint16_t>(m_repeats.size());
}

size_t Sqex::Texture::ModifiableTextureStream::CalculateRepeatUnitSize(size_t mipmapCount) const {
	size_t res = 0;
	for (size_t i = 0; i < mipmapCount; ++i)
		res += static_cast<uint32_t>(Align(RawDataLength(m_header, i)).Alloc);
	return res;
}
