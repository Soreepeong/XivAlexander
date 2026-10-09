#pragma once

#include <d3d11.h>
#include <dxgi.h>

#include "Misc/Hooks.h"
#include "Misc/Logger.h"

_COM_SMARTPTR_TYPEDEF(ID3D11Device, __uuidof(ID3D11Device));
_COM_SMARTPTR_TYPEDEF(ID3D11DeviceContext, __uuidof(ID3D11DeviceContext));
_COM_SMARTPTR_TYPEDEF(ID3D11PixelShader, __uuidof(ID3D11PixelShader));
_COM_SMARTPTR_TYPEDEF(ID3D11Resource, __uuidof(ID3D11Resource));
_COM_SMARTPTR_TYPEDEF(ID3D11Texture2D, __uuidof(ID3D11Texture2D));
_COM_SMARTPTR_TYPEDEF(IDXGISwapChain, __uuidof(IDXGISwapChain));

// The font replacement is a port of FFXIV-FontChanger's Dalamud plugin (FontChanger.DalamudPlugin), kept in the same
// shape, file for file and class for class, so that the two can be compared. This is what its IHost is here: the game's
// executable, hooks, textures the game can bind, and a log.
namespace XivAlexander::Apps::MainApp::FontReplacement::Host {
	// Logs to XivAlexander's log, in the font replacement's category.
	void Log(LogLevel level, std::string message);

	template<typename... Args>
	void Debug(std::format_string<Args...> format, Args&&... args) {
		Log(LogLevel::Debug, std::format(format, std::forward<Args>(args)...));
	}

	template<typename... Args>
	void Information(std::format_string<Args...> format, Args&&... args) {
		Log(LogLevel::Info, std::format(format, std::forward<Args>(args)...));
	}

	template<typename... Args>
	void Warning(std::format_string<Args...> format, Args&&... args) {
		Log(LogLevel::Warning, std::format(format, std::forward<Args>(args)...));
	}

	template<typename... Args>
	void Error(std::format_string<Args...> format, Args&&... args) {
		Log(LogLevel::Error, std::format(format, std::forward<Args>(args)...));
	}

	// Gets the path of the game's executable.
	[[nodiscard]] std::filesystem::path GameFileName();

	// Gets where the game's executable is loaded.
	[[nodiscard]] uintptr_t GameBaseAddress();

	// A hook of a game function, enabled while it exists.
	template<typename R, typename... Args>
	class Hook {
		std::optional<Misc::Hooks::PointerFunction<R, Args...>> m_hook;
		xivres::util::on_dtor m_unhook;

	public:
		Hook(const char* name, uintptr_t address, std::function<R(Args...)> detour) {
			m_hook.emplace(name, reinterpret_cast<R(*)(Args...)>(address));
			m_unhook = m_hook->SetHook(std::move(detour));
		}

		Hook(const Hook&) = delete;
		Hook& operator=(const Hook&) = delete;

		~Hook() {
			m_unhook.clear();
			m_hook.reset();
		}

		// Calls the original function.
		R Original(Args... args) {
			return m_hook->bridge(args...);
		}
	};

	// Calls a function on the thread that presents the game's frames, right before each is presented (the place the plugin's
	// Dalamud draws its UI): a hook of IDXGISwapChain::Present, for the swap chains of a window.
	class PresentHook {
		HWND m_window;
		std::function<void()> m_beforePresent;
		std::optional<Hook<HRESULT, IDXGISwapChain*, UINT, UINT>> m_hook;

	public:
		PresentHook(HWND window, std::function<void()> beforePresent);
		PresentHook(const PresentHook&) = delete;
		PresentHook& operator=(const PresentHook&) = delete;
		~PresentHook();

	private:
		HRESULT PresentDetour(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags);
	};

	// A B8G8R8A8 texture the game can bind (a Kernel::Texture), made on the game's device; destroying it releases it once
	// the game is done with it.
	class Texture {
		uintptr_t m_kernel = 0;
		ID3D11ResourcePtr m_resource;
		int m_decRef = 0;

	public:
		Texture(int width, int height);
		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;
		~Texture();

		// Gets the texture as a Kernel::Texture*.
		[[nodiscard]] uintptr_t Kernel() const { return m_kernel; }

		// Gets its D3D11 resource, to update its contents.
		[[nodiscard]] ID3D11Resource* Resource() const { return m_resource.GetInterfacePtr(); }
	};
}
