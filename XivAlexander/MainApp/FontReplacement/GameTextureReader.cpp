#include "pch.h"
#include "MainApp/FontReplacement/GameTextureReader.h"

#include "MainApp/FontReplacement/GameUi.h"
#include "MainApp/FontReplacement/Utilities.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// A game glyph's fields are bytes, so a glyph fits.
	constexpr int StagingSize = 256;

	// A plane's bit shift in a B4G4R4A4 texel, and its byte offset in a B8G8R8A8 or R8G8B8A8 texel: planes are the R, G, B
	// and A channels, as in the atlas.
	constexpr int Shifts4444[]{8, 4, 0, 12};
	constexpr int OffsetsBgra[]{2, 1, 0, 3};
	constexpr int OffsetsRgba[]{0, 1, 2, 3};

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
	const auto resource = reinterpret_cast<ID3D11Texture2D*>(GameUi::GetD3D11Texture(texture));
	D3D11_TEXTURE2D_DESC desc;
	resource->GetDesc(&desc);
	width = (std::min)(width, static_cast<int>(desc.Width) - x);
	height = (std::min)(height, static_cast<int>(desc.Height) - y);
	const auto bytes = BytesPerTexel(desc.Format);
	if (width <= 0 || height <= 0 || width > StagingSize || height > StagingSize || bytes == 0)
		return alpha;

	const auto staging = GetStaging(resource, desc.Format);
	const D3D11_BOX box{
		.left = static_cast<UINT>(x),
		.top = static_cast<UINT>(y),
		.front = 0,
		.right = static_cast<UINT>(x + width),
		.bottom = static_cast<UINT>(y + height),
		.back = 1,
	};
	m_context->CopySubresourceRegion(staging, 0, 0, 0, 0, resource, 0, &box);

	D3D11_MAPPED_SUBRESOURCE mapped;
	ThrowOnError(m_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped), "Mapping a staging texture");
	const auto unmap = xivres::util::on_dtor([&] { m_context->Unmap(staging, 0); });
	for (auto row = 0; row < height; row++) {
		const auto src = static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(row) * mapped.RowPitch;
		const auto dst = &alpha[static_cast<size_t>(row) * stride];
		if (bytes == 2) {
			const auto shift = Shifts4444[plane];
			for (auto col = 0; col < width; col++)
				dst[col] = static_cast<uint8_t>(((reinterpret_cast<const uint16_t*>(src)[col] >> shift) & 0xF) * 17);
		} else {
			const auto offset = (desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ? OffsetsRgba : OffsetsBgra)[plane];
			for (auto col = 0; col < width; col++)
				dst[col] = src[col * 4 + offset];
		}
	}
	return alpha;
}

ID3D11Texture2D* FontReplacement::GameTextureReader::GetStaging(ID3D11Texture2D* source, DXGI_FORMAT format) {
	if (const auto it = m_staging.find(format); it != m_staging.end())
		return it->second;

	ID3D11DevicePtr device;
	source->GetDevice(&device);
	if (!m_context)
		device->GetImmediateContext(&m_context);

	const D3D11_TEXTURE2D_DESC desc{
		.Width = StagingSize,
		.Height = StagingSize,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = format,
		.SampleDesc = {1, 0},
		.Usage = D3D11_USAGE_STAGING,
		.CPUAccessFlags = D3D11_CPU_ACCESS_READ,
	};
	ID3D11Texture2DPtr texture;
	ThrowOnError(device->CreateTexture2D(&desc, nullptr, &texture), "Making a staging texture");
	return m_staging.emplace(format, std::move(texture)).first->second;
}
