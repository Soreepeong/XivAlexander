#include "pch.h"
#include "Sqex_Texture.h"

void Sqex::Texture::to_json(nlohmann::json& j, const Format& o) {
	switch (o) {
		case Format::L8_1: j = "L8";
			break;
		case Format::L8_2: j = "L8_2";
			break;
		case Format::RGBA4444: j = "RGBA4444";
			break;
		case Format::RGBA5551: j = "RGBA5551";
			break;
		case Format::RGBA_1: j = "RGBA";
			break;
		case Format::RGBA_2: j = "RGBA_2";
			break;
		case Format::RGBAF: j = "RGBAF";
			break;
		case Format::DXT1: j = "DXT1";
			break;
		case Format::DXT3: j = "DXT3";
			break;
		case Format::DXT5: j = "DXT5";
			break;
		default: j = "Unknown";
	}
}

void Sqex::Texture::from_json(const nlohmann::json& j, Format& o) {
	if (j.is_number()) {
		o = static_cast<Format>(j.get<uint32_t>());
	} else {
		auto s = StringTrim(j.get<std::string>());
		CharUpperA(&s[0]);
		if (s == "L8" || s == "L8_1")
			o = Format::L8_1;
		else if (s == "L8_2")
			o = Format::L8_2;
		else if (s == "RGBA4444" || s == "RGBA16")
			o = Format::RGBA4444;
		else if (s == "RGBA5551")
			o = Format::RGBA5551;
		else if (s == "RGBA" || s == "RGBA8888" || s == "RGBA_1" || s == "RGBA8888_1" || s == "RGBA32" || s == "RGBA32_1")
			o = Format::RGBA_1;
		else if (s == "RGBA_2" || s == "RGBA8888_2" || s == "RGBA32_2")
			o = Format::RGBA_2;
		else if (s == "RGBAF" || s == "RGBAHHHH")
			o = Format::RGBAF;
		else if (s == "DXT1")
			o = Format::DXT1;
		else if (s == "DXT3")
			o = Format::DXT3;
		else if (s == "DXT5")
			o = Format::DXT5;
		else
			throw std::invalid_argument(std::format("Unexpected value \"{}\" for Texture Format", j.get<std::string>()));;
	}
}

size_t Sqex::Texture::RawDataLength(Format type, size_t width, size_t height) {
	switch (type) {
		case Format::L8_1:
		case Format::L8_2:
			return width * height;

		case Format::RGBA4444:
			return width * height * sizeof RGBA4444;

		case Format::RGBA5551:
			return width * height * sizeof RGBA5551;

		case Format::RGBA_1:
		case Format::RGBA_2:
			return width * height * sizeof RGBA8888;

		case Format::RGBAF:
			return width * height * sizeof RGBAHHHH;

		case Format::DXT1:
			return width * height * 8;

		case Format::DXT3:
		case Format::DXT5:
			return width * height * 16;

		case Format::Unknown:
		default:
			throw std::invalid_argument("Unsupported type");
	}
}
