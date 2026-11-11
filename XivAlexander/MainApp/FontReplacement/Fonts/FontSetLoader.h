#pragma once

#include "MainApp/FontReplacement/Fonts/FontSet.h"
#include "MainApp/FontReplacement/Presets.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Game fonts the preset has no font of keep the game's glyphs, and characters a font lacks come from the game's glyphs unless
	// SystemFallback.
	struct FontSettings {
		Presets::Fonts Preset;
		bool SystemFallback = true;
	};

	// Makes fonts on a thread of its own, which the game's thread takes once made. A request supersedes those before it.
	class FontLoader {
		FontServices& m_services;
		SystemFallbackFonts& m_fallbackFonts;

		std::mutex m_mutex;
		std::condition_variable m_wake;
		std::optional<FontSettings> m_request;
		std::unique_ptr<FontSet> m_ready;
		std::optional<std::string> m_error;
		std::atomic<uint64_t> m_serial = 0;
		std::atomic<bool> m_stopped = false;
		std::thread m_thread;

	public:
		struct Taken {
			std::unique_ptr<FontSet> Fonts;
			std::optional<std::string> Error;
		};

		FontLoader(FontServices& services, SystemFallbackFonts& fallbackFonts)
			: m_services(services)
			, m_fallbackFonts(fallbackFonts) {}

		FontLoader(const FontLoader&) = delete;
		FontLoader& operator=(const FontLoader&) = delete;

		// Waits for the loader, which stops at its next font.
		~FontLoader();

		void Request(FontSettings value);

		// The fonts made since the last call, if any, or why the last request failed.
		Taken TryTake();

	private:
		void ThreadBody();

		// A font that can't be made is left out with a warning. nullptr once stale.
		[[nodiscard]] static std::unique_ptr<FontSet> Build(FontServices& services, SystemFallbackFonts& fallbackFonts, const FontSettings& value, const std::function<bool()>& stale);
	};
}
