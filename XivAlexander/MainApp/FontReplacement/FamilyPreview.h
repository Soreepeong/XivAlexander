#pragma once

#include <xivres/util.pixel_formats.h>

#include "Config/FontReplacementConfigs.h"
#include "MainApp/FontReplacement/Presets.h"

namespace FontChanger::FixedSizeFont {
	class fixed_size_font;
}

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class PreviewRenderer;

	// Draws sample text with a face of a game font family as its sources make it, for the family's page in the settings.
	// The faces are made as the replacer gets them (PresetController::MakeFamilyFaces), but on their own, so the live
	// replacement is untouched; a face's elements are merged as FontChanger's font editor previews them
	// (Face::GetMergedFont), and characters the face lacks are drawn with the game's own glyphs, read from its installation.
	//
	// Text is drawn at the size asked for, as the replacement draws it at any size: with the face the game would pick, its
	// elements scaled from its first element's size (as ReplacementFace scales them), and the game's glyphs, which come at
	// the game's sizes only, scaled from theirs. The edge is drawn around the glyphs with the replacement's own edge shader,
	// at the width the edge settings give for the size (PreviewRenderer).
	//
	// Opening fonts and reading presets can take hundreds of milliseconds, so drawing happens on a thread of its own, once
	// requests have stopped coming for a while. Only the latest request's image is kept: one asked for before it is
	// dropped, drawn or not. The faces are kept for the next request of the same sources, so typing redraws only the text.
	class FamilyPreview {
	public:
		struct DrawRequest {
			std::string Family;
			std::string FaceName;  // The face of the family the game picks for the size (AXIS_12).
			float Size = 0;  // Drawn at, rounded as the replacement rounds it (FontReplacer::GetDrawnPx).
			FontReplacementFamily Settings;  // Its sources, and what else its faces are made with.
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
			std::string EdgeFailure;  // Why the edge isn't drawn, if it can't be; empty if it is.
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

		// Of the drawing thread only: the faces of the last request, and what they were made from.
		std::optional<DrawRequest> m_facesMadeFor;
		Presets::Faces m_faces;
		std::vector<std::string> m_faceFailures;

		// Of the drawing thread only: the font of the last request's face at its size, kept while the faces are; and its
		// elements that have no glyphs.
		std::string m_fontFace;
		float m_fontSize = 0;
		std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> m_font;
		std::vector<std::string> m_fontFailures;

		// Of the drawing thread only, and released by it: what draws the edge, made at the first image; or why it can't be.
		std::unique_ptr<PreviewRenderer> m_renderer;
		std::optional<std::string> m_rendererFailure;

		std::thread m_thread;

	public:
		explicit FamilyPreview(Notify notify);
		FamilyPreview(const FamilyPreview&) = delete;
		FamilyPreview& operator=(const FamilyPreview&) = delete;

		// Waits for an image being drawn, if any.
		~FamilyPreview();

		// Asks for an image, drawn once no other request has come for the delay; returns the request's number.
		uint64_t Request(DrawRequest request, std::chrono::milliseconds delay);

		// Takes the image of a request, if it is done and no request has come after it.
		[[nodiscard]] std::optional<Image> Take(uint64_t generation);

	private:
		void ThreadBody();

		Image Draw(const DrawRequest& request);

		// Makes the font a face is drawn with at a size: the face's elements scaled, then the game's glyphs. Elements whose
		// fonts have no glyphs at all (not installed, or the game's unreadable) are added to failures.
		[[nodiscard]] std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> MakeFont(const std::string& faceName, float px, std::vector<std::string>& failures) const;
	};
}
