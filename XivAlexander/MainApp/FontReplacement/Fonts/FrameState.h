#pragma once

#include "Config/FontReplacementConfigs.h"
#include "MainApp/FontReplacement/Fonts/Enums/Part.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// What the parts share: the frame that caches and atlas planes are aged by, whether fonts are replaced, the edge, and what hooks and
	// Present saw that other parts must react to at the next update.
	class FrameState {
		std::set<Part> m_failed;
		bool m_gameFontsChanged = false;
		bool m_planeReused = false;
		uint64_t m_frame = 0;
		bool m_isOn = true;
		FontReplacementEdgeConfig m_edge;

	public:
		struct Snapshot {
			bool GameFontsChanged;
			bool PlaneReused;
		};

		// Advanced at the end of each Present.
		[[nodiscard]] uint64_t Frame() const { return m_frame; }

		// Read by every hook.
		[[nodiscard]] bool IsOn() const { return m_isOn; }
		void SetOn(bool value) { m_isOn = value; }

		// The edge as drawn, which glyphs get margins for and copies claim their texture width by.
		[[nodiscard]] const FontReplacementEdgeConfig& Edge() const { return m_edge; }
		void SetEdge(const FontReplacementEdgeConfig& value) { m_edge = value; }

		void EndFrame() { m_frame++; }

		// The game built or unloaded fonts.
		void GameFontChanged() { m_gameFontsChanged = true; }

		// A full atlas made room, so baked text may lack the glyphs that found none.
		void PlaneReused() { m_planeReused = true; }

		// Stops replacing at once, and has the rest turned off at the next update.
		void Fail(std::string_view message, const std::exception& e);

		void Fail(Part part, std::string_view message, const std::exception& e);

		// Until font replacement is set up again.
		[[nodiscard]] bool Failed(Part part) const { return m_failed.contains(part); }

		Snapshot Drain();
	};
}
