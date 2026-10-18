#include "pch.h"
#include "MainApp/FontReplacement/Game/KernelTextureWrapper.h"

#include "Game/SignatureDefinitions.h"
#include "MainApp/FontReplacement/GameCode.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// The game's Device.CreateTexture2D and the rest textures need; resolved at the first texture.
	struct TextureFunctions {
		using CreateTexture2DFn = uintptr_t(*)(void* device, int* size, uint8_t mipLevels, uint32_t format, uint32_t flags, uint32_t unknown);

		void* const* Device{};
		CreateTexture2DFn CreateTexture2D{};
		int D3D11Texture2D{};
		int DecRef{};

		static const TextureFunctions& Get() {
			static const auto s_functions = [] {
				TextureFunctions res;
				const auto functions = FontReplacement::ResolveGameCode(XivAlexander::Game::Resolved::Fonts::Textures);
				res.Device = functions.Device;
				res.CreateTexture2D = reinterpret_cast<CreateTexture2DFn>(functions.CreateTexture2D);
				res.D3D11Texture2D = functions.D3D11Texture2D;

				// Released with ReferencedClassBase's DecRef, which no code site pins down well enough for a signature.
				res.DecRef = 0x18;
				return res;
			}();
			return s_functions;
		}
	};

	// The game's B8G8R8A8_UNORM and B4G4R4A4_UNORM; with flags 0, a 2D texture of DEFAULT usage that shaders read, and its view.
	constexpr uint32_t TextureFormatB8G8R8A8 = 0x1450;
	constexpr uint32_t TextureFormatB4G4R4A4 = 0x1440;

	void DecRef(uintptr_t kernel, int offset) {
		reinterpret_cast<void(*)(uintptr_t)>(*reinterpret_cast<uintptr_t*>(*reinterpret_cast<uintptr_t*>(kernel) + offset))(kernel);
	}
}

FontReplacement::KernelTextureWrapper::KernelTextureWrapper(int width, int height, bool format4444) {
	const auto& functions = TextureFunctions::Get();
	m_decRef = functions.DecRef;
	const auto device = *functions.Device;
	if (!device)
		throw std::runtime_error("The game's device isn't made yet.");

	int size[2]{width, height};
	m_kernel = functions.CreateTexture2D(device, size, 1, format4444 ? TextureFormatB4G4R4A4 : TextureFormatB8G8R8A8, 0, 0);
	if (!m_kernel)
		throw std::runtime_error(std::format("Making a {} x {} texture failed.", width, height));

	const auto texture = *reinterpret_cast<ID3D11Texture2D**>(m_kernel + functions.D3D11Texture2D);
	if (!texture || FAILED(texture->QueryInterface(__uuidof(ID3D11Resource), reinterpret_cast<void**>(&m_resource)))) {
		DecRef(m_kernel, m_decRef);
		throw std::runtime_error("The game's texture has no D3D11 texture.");
	}
}

FontReplacement::KernelTextureWrapper::~KernelTextureWrapper() {
	m_resource = nullptr;

	// Kernel textures are delayed-release: the game frees them frames after the last reference, so frames in flight can keep sampling.
	if (m_kernel)
		DecRef(m_kernel, m_decRef);
}
