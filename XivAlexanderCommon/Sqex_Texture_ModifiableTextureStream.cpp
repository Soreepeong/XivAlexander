#include "pch.h"
#include "Sqex_Texture_ModifiableTextureStream.h"

#include "Sqex_Sqpack.h"

Sqex::Texture::ModifiableTextureStream::ModifiableTextureStream(Format type, uint16_t width, uint16_t height, uint16_t depth)
	: m_header{
		.Unknown1 = 0,
		.HeaderSize = static_cast<uint16_t>(Align(sizeof m_header)),
		.Type = type,
		.Width = width,
		.Height = height,
		.Depth = depth,
		.MipmapCount = 0,
		.Unknown2 = {}
	} {
}

Sqex::Texture::ModifiableTextureStream::ModifiableTextureStream(const std::shared_ptr<RandomAccessStream>& stream)
	: m_header{ stream->ReadStream<Texture::Header>(0) } {
	const auto mipmapLocators = stream->ReadStreamIntoVector<uint32_t>(sizeof m_header, m_header.MipmapCount);
	for (size_t i = 0; i < mipmapLocators.size(); ++i)
		AppendMipmap(std::make_shared<WrappedMipmapStream>(m_header.Width >> i, m_header.Height >> i, m_header.Type,
			std::make_shared<Sqex::RandomAccessStreamPartialView>(stream, mipmapLocators[i],
				RawDataLength(m_header.Type, m_header.Width >> i, m_header.Height >> i))
			));
}

Sqex::Texture::ModifiableTextureStream::~ModifiableTextureStream() = default;

void Sqex::Texture::ModifiableTextureStream::AppendMipmap(std::shared_ptr<MipmapStream> mipmap) {
	if (mipmap->Width() != m_header.Width >> m_mipmaps.size())
		throw std::invalid_argument("invalid mipmap width");
	if (mipmap->Height() != m_header.Height >> m_mipmaps.size())
		throw std::invalid_argument("invalid mipmap height");
	if (mipmap->Type() != m_header.Type)
		throw std::invalid_argument("invalid mipmap type");
	if (mipmap->StreamSize() != RawDataLength(mipmap->Type(), mipmap->Width(), mipmap->Height()))
		throw std::invalid_argument("invalid mipmap size");
	m_mipmaps.emplace_back(std::move(mipmap));
	m_header.MipmapCount = static_cast<uint16_t>(m_mipmaps.size());

	m_mipmapOffsets.clear();
	m_mipmapOffsets.push_back(static_cast<uint32_t>(Align(sizeof m_header + std::span(m_mipmapOffsets).size_bytes())));
	for (size_t i = 0; i < m_mipmaps.size() - 1; ++i)
		m_mipmapOffsets.push_back(static_cast<uint32_t>(m_mipmapOffsets[i] + Align(m_mipmaps[i]->StreamSize()).Alloc));
	m_header.HeaderSize = Align(sizeof m_header + std::span(m_mipmapOffsets).size_bytes());
}

void Sqex::Texture::ModifiableTextureStream::TruncateMipmap(size_t count) {
	if (m_mipmaps.size() < count)
		throw std::invalid_argument("only truncation is supported");
	m_mipmaps.resize(count);
	m_mipmapOffsets.resize(count);
	m_header.MipmapCount = static_cast<uint16_t>(count);
}

uint64_t Sqex::Texture::ModifiableTextureStream::StreamSize() const {
	uint64_t res = Align(sizeof m_header + std::span(m_mipmapOffsets).size_bytes());
	for (const auto& mipmap : m_mipmaps)
		res += Align(mipmap->StreamSize());
	return res;
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

	relativeOffset += headerPadInfo.Alloc;

	if (m_mipmaps.empty())
		return length - out.size_bytes();

	auto it = std::ranges::lower_bound(m_mipmapOffsets,
		static_cast<uint32_t>(relativeOffset),
		[&](uint32_t l, uint32_t r) {
			return l < r;
		});

	if (it == m_mipmapOffsets.end() || *it > relativeOffset)
		--it;

	relativeOffset -= *it;

	for (auto i = it - m_mipmapOffsets.begin(); it != m_mipmapOffsets.end(); ++it, ++i) {
		const auto& mipmapStream = *m_mipmaps[i];

		const auto padInfo = Align(mipmapStream.StreamSize());
		if (relativeOffset < mipmapStream.StreamSize()) {
			const auto available = std::min(out.size_bytes(), mipmapStream.StreamSize() - relativeOffset);
			mipmapStream.ReadStream(relativeOffset, out.data(), available);
			out = out.subspan(available);
			relativeOffset = 0;

			if (out.empty())
				return length;
		} else
			relativeOffset -= mipmapStream.StreamSize();

		if (const auto padSize = padInfo.Pad;
			relativeOffset < padSize) {
			const auto available = std::min(out.size_bytes(), static_cast<size_t>(padSize - relativeOffset));
			std::fill_n(out.begin(), available, 0);
			out = out.subspan(static_cast<size_t>(available));
			relativeOffset = 0;

			if (out.empty())
				return length;
		} else
			relativeOffset -= padSize;
	}

	return length - out.size_bytes();
}
