#pragma once

#include "MainApp/FontReplacement/Host.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Reads glyph coverage from the game's font textures via a staging texture per format; on the thread that calls Present.
	class GameTextureReader {
		std::map<DXGI_FORMAT, ID3D11Texture2DPtr> m_staging;
		ID3D11DeviceContextPtr m_context;

	public:
		GameTextureReader() = default;
		GameTextureReader(const GameTextureReader&) = delete;
		GameTextureReader& operator=(const GameTextureReader&) = delete;

		// Whether it has a format game font textures use.
		[[nodiscard]] static bool CanRead(uintptr_t texture);

		[[nodiscard]] std::vector<uint8_t> Read(uintptr_t texture, int x, int y, int width, int height, int plane);

	private:
		ID3D11Texture2D* GetStaging(ID3D11Texture2D* source, DXGI_FORMAT format);
	};
}
