#include "pch.h"
#include "MainApp/FontReplacement/Host.h"

#include "MainApp/FontReplacement/GameLayout.h"
#include "MainApp/FontReplacement/Utilities.h"
#include "Utils/Win32/LoadedModule.h"
#include "Utils/Win32/Process.h"
#include "XivAlexander.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

void FontReplacement::Host::Log(LogLevel level, std::string message) {
	static const auto s_logger = Misc::Logger::Acquire();
	s_logger->Log(LogCategory::FontReplacement, message, level);
}

std::filesystem::path FontReplacement::Host::GameFileName() {
	return Utils::Win32::Process::Current().PathOf();
}

uintptr_t FontReplacement::Host::GameBaseAddress() {
	return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
}

namespace {
	// Gets IDXGISwapChain::Present of the system's DXGI, from a swap chain made for a window of no use: every swap chain of
	// it has the same.
	uintptr_t FindPresent() {
		std::wstring system(MAX_PATH, L'\0');
		system.resize(GetSystemDirectoryW(system.data(), MAX_PATH));
		const auto d3d11 = LoadLibraryW((std::filesystem::path(system) / L"d3d11.dll").c_str());
		if (!d3d11)
			throw std::runtime_error("d3d11.dll can't be loaded.");
		const auto unload = xivres::util::on_dtor([d3d11] { FreeLibrary(d3d11); });
		const auto create = reinterpret_cast<PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN>(GetProcAddress(d3d11, "D3D11CreateDeviceAndSwapChain"));
		if (!create)
			throw std::runtime_error("D3D11CreateDeviceAndSwapChain isn't there.");

		const auto window = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 16, 16, nullptr, nullptr, nullptr, nullptr);
		if (!window)
			throw std::runtime_error("A window to find Present with can't be made.");
		const auto destroy = xivres::util::on_dtor([window] { DestroyWindow(window); });

		DXGI_SWAP_CHAIN_DESC desc{};
		desc.BufferDesc = {16, 16, {60, 1}, DXGI_FORMAT_R8G8B8A8_UNORM};
		desc.SampleDesc = {1, 0};
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = 1;
		desc.OutputWindow = window;
		desc.Windowed = TRUE;
		desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

		IDXGISwapChainPtr swapChain;
		ID3D11DevicePtr device;
		ID3D11DeviceContextPtr context;
		auto hr = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &desc, &swapChain, &device, nullptr, &context);
		if (FAILED(hr))
			hr = create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &desc, &swapChain, &device, nullptr, &context);
		FontReplacement::ThrowOnError(hr, "Making a swap chain to find Present with");
		return (*reinterpret_cast<uintptr_t* const*>(swapChain.GetInterfacePtr()))[8];
	}

	// Device.CreateTexture2D(Device* this, int* size, byte mipLevels, uint format, uint flags, uint unk) and what textures
	// need of the game; resolved at the first texture.
	struct TextureFunctions {
		using CreateTexture2DFn = uintptr_t(*)(void* device, int* size, uint8_t mipLevels, uint32_t format, uint32_t flags, uint32_t unknown);

		void* const* Device{};
		CreateTexture2DFn CreateTexture2D{};
		int D3D11Texture2D{};
		int DecRef{};

		static const TextureFunctions& Get() {
			static const auto s_functions = [] {
				TextureFunctions res;
				FontReplacement::GameLayout::Resolve("Atlas textures", [&res] {
					res.Device = reinterpret_cast<void* const*>(FontReplacement::GameLayout::Target("Device.Instance"));
					res.CreateTexture2D = reinterpret_cast<CreateTexture2DFn>(FontReplacement::GameLayout::Address("CreateTexture2D"));
					res.D3D11Texture2D = FontReplacement::GameLayout::Get("Texture.D3D11Texture2D");

					// Released with ReferencedClassBase's DecRef, which no code site pins down well enough for a signature.
					res.DecRef = FontReplacement::GameLayout::Get("ReferencedClassBase.DecRef.VtableOffset");
				});
				return res;
			}();
			return s_functions;
		}
	};

	// The game's B8G8R8A8_UNORM; with flags 0, a 2D texture of DEFAULT usage that shaders read, and its view.
	constexpr uint32_t TextureFormatB8G8R8A8 = 0x1450;
}

FontReplacement::Host::PresentHook::PresentHook(HWND window, std::function<void()> beforePresent)
	: m_window(window)
	, m_beforePresent(std::move(beforePresent)) {
	m_hook.emplace("IDXGISwapChain::Present", FindPresent(), [this](IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
		return PresentDetour(swapChain, syncInterval, flags);
	});
}

FontReplacement::Host::PresentHook::~PresentHook() {
	m_hook.reset();
}

HRESULT FontReplacement::Host::PresentHook::PresentDetour(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
	if (DXGI_SWAP_CHAIN_DESC desc; SUCCEEDED(swapChain->GetDesc(&desc)) && desc.OutputWindow == m_window) {
		try {
			m_beforePresent();
		} catch (const std::exception& e) {
			Error("Before presenting a frame: {}", e.what());
		}
	}
	return m_hook->Original(swapChain, syncInterval, flags);
}

FontReplacement::Host::Texture::Texture(int width, int height) {
	const auto& functions = TextureFunctions::Get();
	m_decRef = functions.DecRef;
	const auto device = *functions.Device;
	if (!device)
		throw std::runtime_error("The game's device isn't made yet.");

	int size[2]{width, height};
	m_kernel = functions.CreateTexture2D(device, size, 1, TextureFormatB8G8R8A8, 0, 0);
	if (!m_kernel)
		throw std::runtime_error(std::format("Making a {} x {} texture failed.", width, height));

	const auto texture = *reinterpret_cast<ID3D11Texture2D**>(m_kernel + functions.D3D11Texture2D);
	if (!texture || FAILED(texture->QueryInterface(__uuidof(ID3D11Resource), reinterpret_cast<void**>(&m_resource)))) {
		reinterpret_cast<void(*)(uintptr_t)>(*reinterpret_cast<uintptr_t*>(*reinterpret_cast<uintptr_t*>(m_kernel) + m_decRef))(m_kernel);
		throw std::runtime_error("The game's texture has no D3D11 texture.");
	}
}

FontReplacement::Host::Texture::~Texture() {
	m_resource = nullptr;

	// The game releases a kernel texture some frames after its last reference goes (it is a delayed-release resource), so
	// the frames still in flight can keep sampling it.
	if (m_kernel)
		reinterpret_cast<void(*)(uintptr_t)>(*reinterpret_cast<uintptr_t*>(*reinterpret_cast<uintptr_t*>(m_kernel) + m_decRef))(m_kernel);
}
