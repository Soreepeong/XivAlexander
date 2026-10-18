#pragma once

#include <dxgi.h>

#include "MainApp/FontReplacement/Utilities/Misc.h"
#include "Misc/Hooks.h"
#include "Misc/Logger.h"

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

	template<typename R, typename... Args>
	class Hook {
		std::optional<Misc::Hooks::PointerFunction<R, Args...>> m_hook;

		// Declared after m_hook, so that unhooking comes before the hook goes.
		xivres::util::on_dtor m_unhook;

	public:
		Hook(const char* name, uintptr_t address, std::function<R(Args...)> detour) {
			m_hook.emplace(name, reinterpret_cast<R(*)(Args...)>(address));
			m_unhook = m_hook->SetHook(std::move(detour));
		}

		Hook(const Hook&) = delete;
		Hook& operator=(const Hook&) = delete;

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

	private:
		HRESULT PresentDetour(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags);
	};

	// Redirects only the game's Kernel::SwapChain::Present call in DeviceDX11::PostTick, the only time the immediate context is free, so that other
	// Present hooks (Dalamud, ReShade, overlays) aren't in the way and still run.
	class PresentCallHook {
		std::function<void()> m_beforePresent;
		std::optional<Misc::Hooks::CallSiteFunction<void, void*>> m_hook;

		// Declared after m_hook, so that the call is restored before the hook goes, which waits for a call still in Present (possibly waiting for the display).
		xivres::util::on_dtor m_unhook;

	public:
		explicit PresentCallHook(std::function<void()> beforePresent);
		PresentCallHook(const PresentCallHook&) = delete;
		PresentCallHook& operator=(const PresentCallHook&) = delete;

	private:
		void PresentDetour(void* swapChain);
	};
}
