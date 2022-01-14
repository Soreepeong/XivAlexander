#include "pch.h"
#include "XivAlexanderCommon/Sqex/Texture.h"

void Sqex::Texture::to_json(nlohmann::json& j, const Format& o) {
	switch (o) {
		case Format::L8: j = "L8";
			break;
		case Format::A8: j = "A8";
			break;
		case Format::A4R4G4B4: j = "A4R4G4B4";
			break;
		case Format::A1R5G5B5: j = "A1R5G5B5";
			break;
		case Format::A8R8G8B8: j = "A8R8G8B8";
			break;
		case Format::X8R8G8B8: j = "X8R8G8B8";
			break;
		case Format::A16B16G16R16F: j = "A16B16G16R16F";
			break;
		case Format::DXT1: j = "DXT1";
			break;
		case Format::DXT3: j = "DXT3";
			break;
		case Format::DXT5: j = "DXT5";
			break;
		case Format::R32F: j = "R32F";
			break;
		case Format::G16R16F: j = "G16R16F";
			break;
		case Format::G32R32F: j = "G32R32F";
			break;
		case Format::A32B32G32R32F: j = "A32B32G32R32F";
			break;
		case Format::D16: j = "D16";
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
		if (s == "L8")
			o = Format::L8;
		else if (s == "A8")
			o = Format::A8;
		else if (s == "RGBA4444" || s == "RGBA16" || s == "A4R4G4B4")
			o = Format::A4R4G4B4;
		else if (s == "RGBA5551" || s == "A1R5G5B5")
			o = Format::A1R5G5B5;
		else if (s == "RGBA" || s == "RGBA8888" || s == "RGBA_1" || s == "RGBA8888_1" || s == "RGBA32" || s == "RGBA32_1" || s == "A8R8G8B8")
			o = Format::A8R8G8B8;
		else if (s == "RGBA_2" || s == "RGBA8888_2" || s == "RGBA32_2" || s == "X8R8G8B8")
			o = Format::X8R8G8B8;
		else if (s == "RGBAF" || s == "RGBAHHHH" || s == "A16B16G16R16F")
			o = Format::A16B16G16R16F;
		else if (s == "DXT1")
			o = Format::DXT1;
		else if (s == "DXT3")
			o = Format::DXT3;
		else if (s == "DXT5")
			o = Format::DXT5;
		else if (s == "R32F")
			o = Format::R32F;
		else if (s == "G16R16F")
			o = Format::G16R16F;
		else if (s == "G32R32F")
			o = Format::G32R32F;
		else if (s == "A32B32G32R32F")
			o = Format::A32B32G32R32F;
		else if (s == "D16")
			o = Format::D16;
		else
			throw std::invalid_argument(std::format("Unexpected value \"{}\" for Texture Format", j.get<std::string>()));;
	}
}

size_t Sqex::Texture::RawDataLength(Format type, size_t width, size_t height, size_t depth, size_t mipmapIndex) {
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
