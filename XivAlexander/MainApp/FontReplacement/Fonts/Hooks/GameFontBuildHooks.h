#pragma once

#include "MainApp/FontReplacement/Fonts/ResetScope.h"
#include "MainApp/FontReplacement/Host.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FrameState;

	// The game unloads its fonts and builds them again (a switch between the lobby's fonts and the game's, a UI shader reload), possibly from
	// other files, so everything made from them is made again.
	// It is done at once rather than at the next update, as a rebuilt glyph map may reuse addresses that cached glyphs are keyed by, and copies
	// borrow the map. Both run in the game's update, outside any layout, so resetting there is as safe as between frames.
	class FontLifecycleHooks {
		std::function<void(ResetScope)> m_reset;
		FrameState& m_state;
		std::optional<Host::Hook<int, uintptr_t, uint16_t>> m_buildFontHook;
		std::optional<Host::Hook<uintptr_t, uintptr_t>> m_unloadFontsHook;

	public:
		// reset makes what was made from the fonts again.
		FontLifecycleHooks(std::function<void(ResetScope)> reset, FrameState& state);
		FontLifecycleHooks(const FontLifecycleHooks&) = delete;
		FontLifecycleHooks& operator=(const FontLifecycleHooks&) = delete;

	private:
		int BuildFontDetour(uintptr_t manager, uint16_t index);

		uintptr_t UnloadFontsDetour(uintptr_t manager);

		// After the original, so that copies borrow what the game left, not what it was about to free.
		void FontsChanged(const char* failure);
	};
}
