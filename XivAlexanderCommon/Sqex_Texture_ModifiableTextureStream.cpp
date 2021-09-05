#include "pch.h"
#include "Sqex_Texture_ModifiableTextureStream.h"

#include "XaDxtDecompression.h"

std::shared_ptr<const Sqex::Texture::MipmapStream> Sqex::Texture::MipmapStream::ViewARGB8888(CompressionType type) const {
	if (type != CompressionType::ARGB_1 && type != CompressionType::ARGB_2)
		throw std::invalid_argument("invalid argb8888 compression type");

	if (m_type == CompressionType::ARGB_1 || m_type == CompressionType::ARGB_2) {
		auto res = std::static_pointer_cast<const MipmapStream>(shared_from_this());
		if (m_type == type)
			return res;
		else
			return std::make_shared<WrappedMipmapStream>(this->Width(), this->Height(), type, std::move(res));
	}

	return MemoryBackedMipmap::NewARGB8888From(this, type);
}

struct RGBA4444 {
	uint8_t R : 4;
	uint8_t G : 4;
	uint8_t B : 4;
	uint8_t A : 4;
};

struct RGBA5551 {
	uint8_t R : 5;
	uint8_t G : 5;
	uint8_t B : 5;
	uint8_t A : 1;
};

union RGBAHHHH {
	union Float {
		float Value;
		uint32_t UintValue;
		struct {
			uint32_t Sign : 1;
			uint32_t Exponent : 8;
			uint32_t Mantissa : 23;
		} Bits;
		struct {
			uint32_t Sign : 1;
			uint32_t Exponent : 8;
			uint32_t Mantissa : 10;
			uint32_t MantissaPad : 13;
		} HalfCompatibleBits;
	};

	union Half {
		uint16_t UintValue;
		struct {
			uint16_t Sign : 1;
			uint16_t Exponent : 5;
			uint16_t Mantissa : 10;
		} Bits;

		operator float() const {
			const auto v1 = Float{ .UintValue = (((UintValue & 0x8000U) << 16)
						| (((UintValue & 0x7c00U) + 0x1C000U) << 13)
						| ((UintValue & 0x03FFU) << 13)) }.Value;
			const auto v2 = Float{ .HalfCompatibleBits = {Bits.Sign, Bits.Exponent - 15U + 127U, Bits.Mantissa, 0} }.Value;
			if (v1 != v2)
				throw std::runtime_error("test");
			return v2;
		}
	};

	Half R;
	Half G;
	Half B;
	Half A;
};

union RGBA8888 {
	uint32_t Value;
	struct {
		uint32_t R : 8;
		uint32_t G : 8;
		uint32_t B : 8;
		uint32_t A : 8;
	};

	template<typename RGBABits = RGBA4444>
	void SetFrom(const RGBABits& v) {
		R = v.R;
		G = v.G;
		B = v.B;
		A = v.A;
	}

	template<typename RGBABits = RGBAHHHH>
	void SetFromF(const RGBABits& v) {
		R = static_cast<uint8_t>(Utils::Clamp(255.f * v.R, 0.f, 255.f));
		G = static_cast<uint8_t>(Utils::Clamp(255.f * v.G, 0.f, 255.f));
		B = static_cast<uint8_t>(Utils::Clamp(255.f * v.B, 0.f, 255.f));
		A = static_cast<uint8_t>(Utils::Clamp(255.f * v.A, 0.f, 255.f));
	}
};

std::shared_ptr<Sqex::Texture::MemoryBackedMipmap> Sqex::Texture::MemoryBackedMipmap::NewARGB8888From(const MipmapStream* stream, CompressionType type) {
	if (type != CompressionType::ARGB_1 && type != CompressionType::ARGB_2)
		throw std::invalid_argument("invalid argb8888 compression type");

	const auto width = stream->Width();
	const auto height = stream->Height();
	const auto pixelCount = static_cast<size_t>(width) * height;
	const auto cbSource = stream->StreamSize();

	std::vector<uint8_t> result(pixelCount * sizeof RGBA8888);
	const auto rgba8888view = std::span(reinterpret_cast<RGBA8888*>(&result[0]), result.size() / sizeof RGBA8888);
	uint32_t pos = 0;
	uint8_t buf8[8192];
	switch (stream->Type()) {
		case CompressionType::L8_1:
		case CompressionType::L8_2:
		{
			if (cbSource < pixelCount)
				throw std::runtime_error("Truncated data detected");
			while (const auto len = static_cast<uint32_t>(std::min<uint64_t>(cbSource - pos, sizeof buf8))) {
				stream->ReadStream(pos, buf8, len);
				for (size_t i = 0; i < len; ++pos, ++i) {
					rgba8888view[pos].Value = buf8[i] * 0x1010101UL;
					// result[pos].R = result[pos].G = result[pos].B = result[pos].A = buf8[i];
				}
			}
			break;
		}

		case CompressionType::RGBA4444:
		{
			if (cbSource < pixelCount * sizeof RGBA4444)
				throw std::runtime_error("Truncated data detected");
			const auto view = std::span(reinterpret_cast<RGBA4444*>(buf8), sizeof buf8 / sizeof RGBA4444);
			while (const auto len = static_cast<uint32_t>(std::min<uint64_t>(cbSource - pos, sizeof buf8))) {
				stream->ReadStream(pos, buf8, len);
				for (size_t i = 0, count = len / sizeof uint16_t; i < count; ++pos, ++i)
					rgba8888view[pos].SetFrom(view[i]);
			}
			break;
		}

		case CompressionType::RGBA5551:
		{
			if (cbSource < pixelCount * sizeof RGBA5551)
				throw std::runtime_error("Truncated data detected");
			const auto view = std::span(reinterpret_cast<RGBA5551*>(buf8), sizeof buf8 / sizeof RGBA5551);
			while (const auto len = static_cast<uint32_t>(std::min<uint64_t>(cbSource - pos, sizeof buf8))) {
				stream->ReadStream(pos, buf8, len);
				for (size_t i = 0, count = len / sizeof uint16_t; i < count; ++pos, ++i)
					rgba8888view[pos].SetFrom(view[i]);
			}
			break;
		}

		case CompressionType::ARGB_1:
		case CompressionType::ARGB_2:
			if (cbSource < pixelCount * sizeof RGBA8888)
				throw std::runtime_error("Truncated data detected");
			stream->ReadStream(0, std::span(rgba8888view));
			break;

		case CompressionType::RGBAF:
		{
			if (cbSource < pixelCount * sizeof RGBAHHHH)
				throw std::runtime_error("Truncated data detected");
			stream->ReadStream(0, std::span(rgba8888view));
			const auto view = std::span(reinterpret_cast<RGBAHHHH*>(buf8), sizeof buf8 / sizeof RGBAHHHH);
			while (const auto len = static_cast<uint32_t>(std::min<uint64_t>(cbSource - pos, sizeof buf8))) {
				stream->ReadStream(pos, buf8, len);
				for (size_t i = 0, count = len / sizeof uint16_t; i < count; ++pos, ++i)
					rgba8888view[pos].SetFromF(view[i]);
			}
			break;
		}

		case CompressionType::DXT1:
		{
			if (cbSource < pixelCount * 8)
				throw std::runtime_error("Truncated data detected");
			while (const auto len = static_cast<uint32_t>(std::min<uint64_t>(cbSource - pos, sizeof buf8))) {
				for (size_t i = 0, count = len; i < count; i += 8, pos += 8) {
					Utils::DecompressBlockDXT1(
						pos / 2 % width,
						pos / 2 / width * 4,
						width, &buf8[i], reinterpret_cast<uint32_t*>(&rgba8888view[0]));
				}
			}
			break;
		}

		case CompressionType::DXT3:
		{
			if (cbSource < pixelCount * 16)
				throw std::runtime_error("Truncated data detected");
			while (const auto len = static_cast<uint32_t>(std::min<uint64_t>(cbSource - pos, sizeof buf8))) {
				for (size_t i = 0, count = len; i < count; i += 16, pos += 16) {
					Utils::DecompressBlockDXT1(
						pos / 4 % width,
						pos / 4 / width * 4,
						width, &buf8[i], reinterpret_cast<uint32_t*>(&rgba8888view[0]));
					for (size_t dy = 0; dy < 4; dy += 1) {
						for (size_t dx = 0; dx < 4; dx += 2) {
							rgba8888view[dy * width + dx].A = 17 * (buf8[i + dy * 2 + dx / 2] & 0xF);
							rgba8888view[dy * width + dx + 1].A = 17 * (buf8[i + dy * 2 + dx / 2] >> 4);
						}
					}
				}
			}
			break;
		}

		case CompressionType::DXT5:
		{
			if (cbSource < pixelCount * 16)
				throw std::runtime_error("Truncated data detected");
			while (const auto len = static_cast<uint32_t>(std::min<uint64_t>(cbSource - pos, sizeof buf8))) {
				for (size_t i = 0, count = len; i < count; i += 16, pos += 16) {
					Utils::DecompressBlockDXT5(
						pos / 4 % width,
						pos / 4 / width * 4,
						width, &buf8[i], reinterpret_cast<uint32_t*>(&rgba8888view[0]));
				}
			}
			break;
		}

		case CompressionType::Unknown:
		default:
			throw std::runtime_error("Unsupported type");
	}

	return std::make_shared<MemoryBackedMipmap>(stream->Width(), stream->Height(), type, std::move(result));
}

uint64_t Sqex::Texture::MemoryBackedMipmap::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const {
	const auto available = static_cast<size_t>(std::min(m_data.size() - offset, length));
	std::copy_n(&m_data[static_cast<size_t>(offset)], available, static_cast<char*>(buf));
	return available;
}
