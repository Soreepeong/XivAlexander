#pragma once

#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Reads the game's textures, each copied to memory at its first read, as each read back waits for the GPU.
	// Must be used on the thread that calls Present, as it uses the immediate context.
	class TextureReader {
		struct TextureCopy {
			int Width = 0;
			int Height = 0;
			int BytesPerTexel = 0;
			DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
			std::vector<uint8_t> Texels;
		};

		std::map<uintptr_t, TextureCopy> m_copies;

	public:
		TextureReader() = default;
		TextureReader(const TextureReader&) = delete;
		TextureReader& operator=(const TextureReader&) = delete;

		[[nodiscard]] static bool CanRead(uintptr_t texture);

		[[nodiscard]] std::vector<uint8_t> Read(uintptr_t texture, int x, int y, int width, int height, int plane);

		void Clear() { m_copies.clear(); }

	private:
		const TextureCopy& GetCopy(uintptr_t texture);

		[[nodiscard]] static TextureCopy ReadAll(ID3D11Texture2D* resource, const D3D11_TEXTURE2D_DESC& desc, int bytes);
	};
}
