#include "pch.h"
#include "Sqex_Texture.h"

void Sqex::Texture::to_json(nlohmann::json& j, const CompressionType& o) {
	switch (o) {
	case CompressionType::L8_1: j = "L8"; break;
	case CompressionType::L8_2: j = "L8_2"; break;
	case CompressionType::RGBA4444: j = "RGBA4444"; break;
	case CompressionType::RGBA5551: j = "RGBA5551"; break;
	case CompressionType::RGBA_1: j = "RGBA"; break;
	case CompressionType::RGBA_2: j = "RGBA_2";  break;
	case CompressionType::RGBAF: j = "RGBAF"; break;
	case CompressionType::DXT1: j = "DXT1"; break;
	case CompressionType::DXT3: j = "DXT3"; break;
	case CompressionType::DXT5: j = "DXT5"; break;
	default: j = "Unknown";
	}
}

void Sqex::Texture::from_json(const nlohmann::json& j, CompressionType& o) {
	if (j.is_number()) {
		o = static_cast<CompressionType>(j.get<uint32_t>());
	} else {
		auto s = StringTrim(j.get<std::string>());
		CharUpperA(&s[0]);
		if (s == "L8" || s == "L8_1")
			o = CompressionType::L8_1;
		else if (s == "L8_2")
			o = CompressionType::L8_2;
		else if (s == "RGBA4444" || s == "RGBA16")
			o = CompressionType::RGBA4444;
		else if (s == "RGBA5551")
			o = CompressionType::RGBA5551;
		else if (s == "RGBA" || s == "RGBA8888" || s == "RGBA_1" || s == "RGBA8888_1" || s == "RGBA32" || s == "RGBA32_1")
			o = CompressionType::RGBA_1;
		else if (s == "RGBA_2" || s == "RGBA8888_2" || s == "RGBA32_2")
			o = CompressionType::RGBA_2;
		else if (s == "RGBAF" || s == "RGBAHHHH")
			o = CompressionType::RGBAF;
		else if (s == "DXT1")
			o = CompressionType::DXT1;
		else if (s == "DXT3")
			o = CompressionType::DXT3;
		else if (s == "DXT5")
			o = CompressionType::DXT5;
		else
			throw std::invalid_argument(std::format("Unexpected value \"{}\" for Texture CompressionType", j.get<std::string>()));;
	}
}

size_t Sqex::Texture::RawDataLength(CompressionType type, size_t width, size_t height) {
	switch (type) {
		case CompressionType::L8_1:
		case CompressionType::L8_2:
			return width * height;

		case CompressionType::RGBA4444:
			return width * height * sizeof RGBA4444;

		case CompressionType::RGBA5551:
			return width * height * sizeof RGBA5551;

		case CompressionType::RGBA_1:
		case CompressionType::RGBA_2:
			return width * height * sizeof RGBA8888;

		case CompressionType::RGBAF:
			return width * height * sizeof RGBAHHHH;

		case CompressionType::DXT1:
			return width * height * 8;

		case CompressionType::DXT3:
		case CompressionType::DXT5:
			return width * height * 16;

		case CompressionType::Unknown:
		default:
			throw std::invalid_argument("Unsupported type");
	}
}
