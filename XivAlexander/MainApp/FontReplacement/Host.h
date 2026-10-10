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

// The font replacement ports FontChanger.DalamudPlugin file for file and class for class, so the two can be compared; this is its IHost.
namespace XivAlexander::Apps::MainApp::FontReplacement::Host {
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

	[[nodiscard]] std::filesystem::path GameFileName();

	[[nodiscard]] uintptr_t GameBaseAddress();

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

		R Original(Args... args) {
			return m_hook->bridge(args...);
		}
	};

	// Runs right before each frame is presented, where the plugin's Dalamud draws its UI.
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

	// Redirects only the game's Kernel::SwapChain::Present call in DeviceDX11::PostTick, so Present hooks (Dalamud, ReShade, overlays) aren't in the way and still run.
	// That is after the render thread is done with the frame and before the next is kicked, the only time the immediate context is free.
	class PresentCallHook {
		std::function<void()> m_beforePresent;
		std::optional<Misc::Hooks::CallSiteFunction<void, void*>> m_hook;
		xivres::util::on_dtor m_unhook;

	public:
		explicit PresentCallHook(std::function<void()> beforePresent);
		PresentCallHook(const PresentCallHook&) = delete;
		PresentCallHook& operator=(const PresentCallHook&) = delete;
		~PresentCallHook();

	private:
		void PresentDetour(void* swapChain);
	};

	// A Kernel::Texture on the game's device; destroying it releases it once the game is done with it.
	class Texture {
		uintptr_t m_kernel = 0;
		ID3D11ResourcePtr m_resource;
		int m_decRef = 0;

	public:
		Texture(int width, int height);
		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;
		~Texture();

		// A Kernel::Texture*.
		[[nodiscard]] uintptr_t Kernel() const { return m_kernel; }

		[[nodiscard]] ID3D11Resource* Resource() const { return m_resource.GetInterfacePtr(); }
	};
}
