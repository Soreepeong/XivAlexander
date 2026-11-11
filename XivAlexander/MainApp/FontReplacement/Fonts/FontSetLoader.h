#pragma once

#include "MainApp/FontReplacement/Fonts/LoadedFontSet.h"
#include "MainApp/FontReplacement/Fonts/Records/FontRequest.h"
#include "MainApp/FontReplacement/Fonts/Records/FontSettings.h"
#include "MainApp/FontReplacement/Fonts/Records/LoadReport.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Reads the sources and makes the fonts on a thread of its own, which the game's thread takes once made. A request supersedes those
	// before it.
	class FontSetLoader {
		struct Pending {
			FontRequest Request;
			bool KeepOnFailure;
			uint64_t Serial;
		};

		struct Unbuilt {
			FontSettings Settings;
			std::optional<WatchList> Watch;
		};

		DirectWriteServices& m_services;
		SystemFallbackFonts& m_fallbackFonts;

		std::mutex m_mutex;
		std::condition_variable m_wake;
		std::optional<Pending> m_request;
		std::unique_ptr<LoadedFontSet> m_ready;
		std::optional<std::string> m_error;
		std::optional<LoadReport> m_report;
		std::atomic<uint64_t> m_serial = 0;

		// A request that applies whatever fails hasn't begun reading its sources yet, so the one superseding it does too.
		bool m_mustApply = false;

		// The settings read last whose fonts weren't handed over, with what to watch for them. A later request that reads none makes these, as
		// its superseding them stopped their fonts from being made.
		std::optional<Unbuilt> m_unbuilt;
		std::atomic<bool> m_stopped = false;
		std::thread m_thread;

	public:
		struct Taken {
			std::unique_ptr<LoadedFontSet> Fonts;

			// Why making the fonts failed.
			std::optional<std::string> Error;

			// What reading the sources came to.
			std::optional<LoadReport> Report;
		};

		FontSetLoader(DirectWriteServices& services, SystemFallbackFonts& fallbackFonts)
			: m_services(services)
			, m_fallbackFonts(fallbackFonts) {}

		FontSetLoader(const FontSetLoader&) = delete;
		FontSetLoader& operator=(const FontSetLoader&) = delete;

		// Waits for the loader, which stops at its next font.
		~FontSetLoader();

		// Game thread. Takes what was made since the last call, then asks for the fonts of request, if given. In one step, so that a request
		// can't supersede what the one before it finished making before that is taken.
		// keepOnFailure is whether a source that can't be loaded leaves the fonts made last in place, as for an edited file.
		Taken Take(std::optional<FontRequest> request, bool keepOnFailure);

	private:
		void ThreadBody();

		void Load(const FontRequest& value, bool keepOnFailure, uint64_t serial);

		// A font that can't be made is left out with a warning. nullptr once stale.
		[[nodiscard]] static std::unique_ptr<LoadedFontSet> Build(DirectWriteServices& services, SystemFallbackFonts& fallbackFonts, const FontSettings& value, const std::function<bool()>& stale);
	};
}
