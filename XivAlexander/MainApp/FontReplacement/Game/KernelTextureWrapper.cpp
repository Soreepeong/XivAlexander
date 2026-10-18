#include "pch.h"
#include "MainApp/FontReplacement/Game/KernelTextureWrapper.h"

#include "Game/SignatureDefinitions.h"
#include "MainApp/FontReplacement/GameCode.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	using CreateTexture2DFn = uintptr_t(*)(void* device, int* size, uint8_t mipLevels, uint32_t format, uint32_t flags, uint32_t unknown);

	// The game's B8G8R8A8_UNORM and B4G4R4A4_UNORM; with flags 0, a 2D texture of DEFAULT usage that shaders read, and its view.
	constexpr uint32_t TextureFormatB8G8R8A8 = 0x1450;
	constexpr uint32_t TextureFormatB4G4R4A4 = 0x1440;

	// Released with ReferencedClassBase's DecRef, which no code site pins down well enough for a signature.
	constexpr int DecRefVtableOffset = 0x18;

	void DecRef(uintptr_t kernel) {
		reinterpret_cast<void(*)(uintptr_t)>(*reinterpret_cast<uintptr_t*>(*reinterpret_cast<uintptr_t*>(kernel) + DecRefVtableOffset))(kernel);
	}
}

FontReplacement::KernelTextureWrapper::KernelTextureWrapper(int width, int height, bool format4444) {
	static const auto s_functions = ResolveGameCode(Game::Resolved::Fonts::Textures);
	const auto device = *s_functions.Device;
	if (!device)
		throw std::runtime_error("The game's device isn't made yet.");

	int size[2]{width, height};
	m_kernel = reinterpret_cast<CreateTexture2DFn>(s_functions.CreateTexture2D)(device, size, 1, format4444 ? TextureFormatB4G4R4A4 : TextureFormatB8G8R8A8, 0, 0);
	if (!m_kernel)
		throw std::runtime_error(std::format("Making a {} x {} texture failed.", width, height));

	const auto texture = *reinterpret_cast<ID3D11Texture2D**>(m_kernel + s_functions.D3D11Texture2D);
	if (!texture || FAILED(texture->QueryInterface(__uuidof(ID3D11Resource), reinterpret_cast<void**>(&m_resource)))) {
		DecRef(m_kernel);
		throw std::runtime_error("The game's texture has no D3D11 texture.");
	}
}

FontReplacement::KernelTextureWrapper::~KernelTextureWrapper() {
	m_resource = nullptr;

	// Kernel textures are delayed-release: the game frees them frames after the last reference, so frames in flight can keep sampling.
	if (m_kernel)
		DecRef(m_kernel);
}
