#include "pch.h"
#include "MainApp/FontReplacement/Game/GameTextureReader.h"

#include "MainApp/FontReplacement/Fonts/GlyphAtlas.h"
#include "MainApp/FontReplacement/GameUi.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	int BytesPerTexel(DXGI_FORMAT format) {
		switch (format) {
			case DXGI_FORMAT_B4G4R4A4_UNORM:
				return 2;
			case DXGI_FORMAT_B8G8R8A8_UNORM:
			case DXGI_FORMAT_R8G8B8A8_UNORM:
				return 4;
			default:
				return 0;
		}
	}
}

bool FontReplacement::GameTextureReader::CanRead(uintptr_t texture) {
	const auto resource = reinterpret_cast<ID3D11Texture2D*>(GameUi::GetD3D11Texture(texture));
	if (!resource)
		return false;
	D3D11_TEXTURE2D_DESC desc;
	resource->GetDesc(&desc);
	return BytesPerTexel(desc.Format) != 0;
}

std::vector<uint8_t> FontReplacement::GameTextureReader::Read(uintptr_t texture, int x, int y, int width, int height, int plane) {
	std::vector<uint8_t> alpha(static_cast<size_t>(width) * height);
	const auto stride = width;
	const auto& copy = GetCopy(texture);
	width = (std::min)(width, copy.Width - x);
	height = (std::min)(height, copy.Height - y);
	if (width <= 0 || height <= 0 || copy.BytesPerTexel == 0)
		return alpha;

	const auto pitch = static_cast<size_t>(copy.Width) * copy.BytesPerTexel;
	const auto offset = copy.Format == DXGI_FORMAT_R8G8B8A8_UNORM ? plane : GlyphAtlas::BgraOffset(plane);
	for (auto row = 0; row < height; row++) {
		const auto src = copy.Texels.data() + static_cast<size_t>(y + row) * pitch + static_cast<size_t>(x) * copy.BytesPerTexel;
		const auto dst = &alpha[static_cast<size_t>(row) * stride];
		if (copy.BytesPerTexel == 2) {
			for (auto col = 0; col < width; col++)
				dst[col] = GlyphAtlas::Get4444(static_cast<uint16_t>(src[col * 2] | (src[col * 2 + 1] << 8)), plane);
		} else {
			for (auto col = 0; col < width; col++)
				dst[col] = src[col * 4 + offset];
		}
	}
	return alpha;
}

const FontReplacement::GameTextureReader::TextureCopy& FontReplacement::GameTextureReader::GetCopy(uintptr_t texture) {
	if (const auto it = m_copies.find(texture); it != m_copies.end())
		return it->second;

	const auto resource = reinterpret_cast<ID3D11Texture2D*>(GameUi::GetD3D11Texture(texture));
	D3D11_TEXTURE2D_DESC desc;
	resource->GetDesc(&desc);
	const auto bytes = BytesPerTexel(desc.Format);
	return m_copies.emplace(texture, bytes == 0 ? TextureCopy{.Format = desc.Format} : ReadAll(resource, desc, bytes)).first->second;
}

FontReplacement::GameTextureReader::TextureCopy FontReplacement::GameTextureReader::ReadAll(ID3D11Texture2D* resource, const D3D11_TEXTURE2D_DESC& desc, int bytes) {
	const auto device = GetDevice(resource);
	ID3D11DeviceContextPtr context;
	device->GetImmediateContext(&context);

	const D3D11_TEXTURE2D_DESC stagingDesc{
		.Width = desc.Width,
		.Height = desc.Height,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = desc.Format,
		.SampleDesc = {1, 0},
		.Usage = D3D11_USAGE_STAGING,
		.CPUAccessFlags = D3D11_CPU_ACCESS_READ,
	};
	ID3D11Texture2DPtr staging;
	ThrowOnError(device->CreateTexture2D(&stagingDesc, nullptr, &staging), "Making a staging texture");
	context->CopySubresourceRegion(staging, 0, 0, 0, 0, resource, 0, nullptr);

	const auto pitch = static_cast<size_t>(desc.Width) * bytes;
	TextureCopy copy{static_cast<int>(desc.Width), static_cast<int>(desc.Height), bytes, desc.Format, std::vector<uint8_t>(pitch * desc.Height)};
	D3D11_MAPPED_SUBRESOURCE mapped;
	ThrowOnError(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped), "Mapping a staging texture");
	const auto unmap = xivres::util::on_dtor([&] { context->Unmap(staging, 0); });
	for (UINT row = 0; row < desc.Height; row++)
		std::memcpy(copy.Texels.data() + row * pitch, static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(row) * mapped.RowPitch, pitch);
	return copy;
}
