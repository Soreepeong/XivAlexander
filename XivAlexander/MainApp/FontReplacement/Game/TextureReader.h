#pragma once

#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Must be used on the thread that calls Present, as it uses the immediate context.
	class TextureReader {
		std::map<DXGI_FORMAT, ID3D11Texture2DPtr> m_staging;
		ID3D11DeviceContextPtr m_context;

	public:
		TextureReader() = default;
		TextureReader(const TextureReader&) = delete;
		TextureReader& operator=(const TextureReader&) = delete;

		[[nodiscard]] static bool CanRead(uintptr_t texture);

		[[nodiscard]] std::vector<uint8_t> Read(uintptr_t texture, int x, int y, int width, int height, int plane);

	private:
		ID3D11Texture2D* GetStaging(ID3D11Texture2D* source, DXGI_FORMAT format);
	};
}
