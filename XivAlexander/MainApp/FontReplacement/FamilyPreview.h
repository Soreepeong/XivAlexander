#pragma once

#include <xivres/util.pixel_formats.h>

#include "Config/FontReplacementConfigs.h"
#include "MainApp/FontReplacement/Presets.h"

namespace FontChanger::FixedSizeFont {
	class fixed_size_font;
}

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class PreviewRenderer;

	// Settings-page preview of a font family, apart from the live replacement: faces as PresetController::MakeFamilyFaces makes them,
	// elements merged as Face::GetMergedFont and scaled as ReplacementFace, the game's glyphs (from its installation) for what's missing.
	// Opening fonts can take hundreds of ms, so drawing runs on its own thread once requests stop coming; only the latest request's image is kept.
	class FamilyPreview {
	public:
		struct DrawRequest {
			std::string Family;
			std::string FaceName;  // The face of the family the game picks for the size (AXIS_12).
			float Size = 0;  // Drawn at, rounded as the replacement rounds it (FontReplacer::GetDrawnPx).
			FontReplacementFamily Settings;
			std::filesystem::path PresetFolder;  // Presets::Folder, which relative presets are in.
			bool Reload = false;  // Makes the faces anew even if the sources are the same: their files may have changed.
			FontReplacementEdgeConfig Edge;
			std::wstring Text;
			int Width = 0;
			int Height = 0;
			COLORREF Foreground = 0;
			COLORREF EdgeColor = 0;
			COLORREF Background = 0;
		};

		struct Image {
			int Width = 0;
			int Height = 0;
			std::vector<xivres::util::b8g8r8a8> Pixels;  // Rows from the top, as a 32-bit DIB of negative height has them.
			std::string Failures;  // Of the sources that couldn't be read, and of drawing; empty if none.
			std::string EdgeFailure;
		};

		// Called on the drawing thread when an image is done, with the number of its request.
		using Notify = std::function<void(uint64_t generation)>;

	private:
		const Notify m_notify;

		std::mutex m_mutex;
		std::condition_variable m_wake;
		bool m_stop = false;
		uint64_t m_generation = 0;
		std::optional<DrawRequest> m_pending;
		std::chrono::steady_clock::time_point m_due;
		std::optional<std::pair<uint64_t, Image>> m_done;

		// Drawing thread only: the faces of the last request, and what they were made from.
		std::optional<DrawRequest> m_facesMadeFor;
		Presets::Faces m_faces;
		std::vector<std::string> m_faceFailures;

		// Drawing thread only: the last request's face font at its size, kept while the faces are, and its glyphless elements.
		std::string m_fontFace;
		float m_fontSize = 0;
		std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> m_font;
		std::vector<std::string> m_fontFailures;

		// Drawing thread only, which also releases it: made at the first image, or why it couldn't be.
		std::unique_ptr<PreviewRenderer> m_renderer;
		std::optional<std::string> m_rendererFailure;

		std::thread m_thread;

	public:
		explicit FamilyPreview(Notify notify);
		FamilyPreview(const FamilyPreview&) = delete;
		FamilyPreview& operator=(const FamilyPreview&) = delete;

		// Waits for an image being drawn, if any.
		~FamilyPreview();

		// Drawn once no other request has come for the delay; returns the request's number.
		uint64_t Request(DrawRequest request, std::chrono::milliseconds delay);

		// Takes the image of a request, if it is done and no request has come after it.
		[[nodiscard]] std::optional<Image> Take(uint64_t generation);

	private:
		void ThreadBody();

		Image Draw(const DrawRequest& request);

		// The face's elements scaled, then the game's glyphs; elements with no glyphs at all (not installed, or unreadable) are added to failures.
		[[nodiscard]] std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> MakeFont(const std::string& faceName, float px, std::vector<std::string>& failures) const;
	};
}
