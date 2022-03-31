#pragma once
#include "XivAlexanderCommon/Sqex.h"

namespace Sqex::Texture {
	enum class Format : uint32_t {
		Unknown = 0,
		L8 = 4400,
		A8 = 4401,
		A4R4G4B4 = 5184,
		A1R5G5B5 = 5185,
		A8R8G8B8 = 5200,
		X8R8G8B8 = 5201,
		R32F = 8528,
		G16R16F = 8784,
		G32R32F = 8800,
		A16B16G16R16F = 9312,
		A32B32G32R32F = 9328,
		DXT1 = 13344,
		DXT3 = 13360,
		DXT5 = 13361,
		D16 = 16704,
	};

	struct RGBA4444 {
		static constexpr size_t ChannelCount = 4;
		static constexpr uint8_t MaxR = 15;
		static constexpr uint8_t MaxG = 15;
		static constexpr uint8_t MaxB = 15;
		static constexpr uint8_t MaxA = 15;

		uint8_t R : 4;
		uint8_t G : 4;
		uint8_t B : 4;
		uint8_t A : 4;  // Actually opacity

		void SetFrom(uint32_t r, uint32_t g, uint32_t b) {
			R = static_cast<uint8_t>(r);
			G = static_cast<uint8_t>(g);
			B = static_cast<uint8_t>(b);
		}

		void SetFrom(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
			R = static_cast<uint8_t>(r);
			G = static_cast<uint8_t>(g);
			B = static_cast<uint8_t>(b);
			A = static_cast<uint8_t>(a);
		}
	};

	struct RGBA5551 {
		static constexpr size_t ChannelCount = 4;
		static constexpr uint16_t MaxR = 31;
		static constexpr uint16_t MaxG = 31;
		static constexpr uint16_t MaxB = 31;
		static constexpr uint16_t MaxA = 1;

		uint16_t R : 5;
		uint16_t G : 5;
		uint16_t B : 5;
		uint16_t A : 1;  // Actually opacity

		void SetFrom(uint32_t r, uint32_t g, uint32_t b) {
			R = static_cast<uint16_t>(r);
			G = static_cast<uint16_t>(g);
			B = static_cast<uint16_t>(b);
		}

		void SetFrom(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
			R = static_cast<uint16_t>(r);
			G = static_cast<uint16_t>(g);
			B = static_cast<uint16_t>(b);
			A = static_cast<uint16_t>(a);
		}
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

		static constexpr size_t ChannelCount = 4;
		static constexpr float MaxR = 1.f;
		static constexpr float MaxG = 1.f;
		static constexpr float MaxB = 1.f;
		static constexpr float MaxA = 1.f;

		uint64_t Value;
		struct {
			Half R;
			Half G;
			Half B;
			Half A;  // Actually opacity
		};
	};

	union RGBAFFFF {
		static constexpr size_t ChannelCount = 4;
		static constexpr float MaxR = 1.f;
		static constexpr float MaxG = 1.f;
		static constexpr float MaxB = 1.f;
		static constexpr float MaxA = 1.f;

		struct {
			float R;
			float G;
			float B;
			float A;  // Actually opacity
		};
	};

	union RGBA8888 {
		static constexpr size_t ChannelCount = 4;
		static constexpr uint32_t MaxR = 255;
		static constexpr uint32_t MaxG = 255;
		static constexpr uint32_t MaxB = 255;
		static constexpr uint32_t MaxA = 255;

		uint32_t Value;
		struct {
			uint32_t R : 8;
			uint32_t G : 8;
			uint32_t B : 8;
			uint32_t A : 8;  // Actually opacity
		};

		RGBA8888() : Value(0) {}
		RGBA8888(uint32_t value) : Value(value) {}
		RGBA8888(uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255) {
			SetFrom(r, g, b, a);
		}

		void SetFrom(uint32_t r, uint32_t g, uint32_t b) {
			R = r;
			G = g;
			B = b;
		}

		void SetFrom(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
			R = r;
			G = g;
			B = b;
			A = a;
		}

		template<typename RGBABits = RGBAHHHH>
		void SetFromF(const RGBABits& v) {
			R = static_cast<uint32_t>(Utils::Clamp(255.f * v.R, 0.f, 255.f));
			G = static_cast<uint32_t>(Utils::Clamp(255.f * v.G, 0.f, 255.f));
			B = static_cast<uint32_t>(Utils::Clamp(255.f * v.B, 0.f, 255.f));
			A = static_cast<uint32_t>(Utils::Clamp(255.f * v.A, 0.f, 255.f));
		}
	};

	struct Header {
		LE<uint16_t> Unknown1;
		LE<uint16_t> HeaderSize;
		LE<Format> Type;
		LE<uint16_t> Width;
		LE<uint16_t> Height;
		LE<uint16_t> Depth;
		LE<uint16_t> MipmapCount;
		char Unknown2[0xC]{};
	};

	inline size_t RawDataLength(Format type, size_t width, size_t height, size_t depth, size_t mipmapIndex = 0) {
		width = std::max<size_t>(1, width >> mipmapIndex);
		height = std::max<size_t>(1, height >> mipmapIndex);
		depth = std::max<size_t>(1, depth >> mipmapIndex);
		switch (type) {
			case Format::L8:
			case Format::A8:
				return width * height * depth;

			case Format::A4R4G4B4:
			case Format::A1R5G5B5:
				return width * height * depth * 2;

			case Format::A8R8G8B8:
			case Format::X8R8G8B8:
			case Format::R32F:
			case Format::G16R16F:
				return width * height * depth * 4;

			case Format::A16B16G16R16F:
			case Format::G32R32F:
				return width * height * depth * 8;

			case Format::A32B32G32R32F:
				return width * height * depth * 16;

			case Format::DXT1:
				return depth * std::max<size_t>(1, ((width + 3) / 4)) * std::max<size_t>(1, ((height + 3) / 4)) * 8;

			case Format::DXT3:
			case Format::DXT5:
				return depth * std::max<size_t>(1, ((width + 3) / 4)) * std::max<size_t>(1, ((height + 3) / 4)) * 16;

			case Format::D16:
			case Format::Unknown:
			default:
				throw std::invalid_argument("Unsupported type");
		}
	}

	inline size_t RawDataLength(const Header& header, size_t mipmapIndex = 0) {
		return RawDataLength(header.Type, header.Width, header.Height, header.Depth, mipmapIndex);
	}
}
