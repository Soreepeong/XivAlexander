#pragma once

#include "MainApp/FontReplacement/Fonts/Records/FontAlignment.h"
#include "MainApp/FontReplacement/Fonts/Records/RasterGlyph.h"
#include "MainApp/FontReplacement/Fonts/Records/ShapedCluster.h"
#include "MainApp/FontReplacement/Fonts/Records/TextRun.h"
#include "MainApp/FontReplacement/Fonts/SizedFont.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class IFaceSource;
	struct Cluster;

	// What a cluster's raster is made from, compared by value.
	// The element implies the font, and the source includes its bold or italic. The edge margin, subpixel order and vertical renderer aren't in
	// it, as changing any makes every glyph again.
	struct RasterKey {
		float Px;
		const MaterializedFontElement* Element;
		const IFaceSource* Source;
		float EmSize;
		float Origin;
		int RawAdvance;

		// Of the picked font, whose game font's metrics it may take.
		FontAlignment Alignment;

		std::vector<uint32_t> Glyphs;
		std::vector<float> Advances;
		std::vector<std::pair<float, float>> Offsets;

		auto operator<=>(const RasterKey&) const = default;

		[[nodiscard]] static RasterKey Of(const SizedFont& sized, const Cluster& c);
	};

	// A shaped cluster, placed one after another by advances on screen (with the elements' transformations, emboldening, letter spacing and
	// monospacing).
	struct Cluster {
		// nullptr if the cluster is left to the per-character path.
		IFaceSource* Source{};

		// The element's size the source draws at.
		float Px{};

		std::vector<uint32_t> GlyphIds;
		std::vector<float> Advances;
		std::vector<GlyphOffset> Offsets;

		// A monospaced cluster's own advance in whole pixels, which its cell is made from, or 0 for the others.
		int RawAdvance{};

		// In pixels on screen, towards the next cluster.
		float Kerning{};

		// Made on first use.
		std::optional<FontReplacement::RasterKey> Key;

		// Once made in this shaping.
		std::shared_ptr<const RasterGlyph> Raster;

		// The fraction of a pixel the pen is at, in ShapingContext::SubpixelSteps.
		float Origin{};

		// Whole-pixel pen positions the cluster starts and ends at.
		int X0{};
		int X1{};

		int TextStart{};
		int TextEnd{};

		// Of the cluster's first character.
		std::optional<FontReplacement::GlyphSource> GlyphSource;

		// A real italic, which the game must not shear.
		bool RealItalic{};

		[[nodiscard]] bool Missing() const { return !Source; }

		[[nodiscard]] std::optional<RasterGlyph> Rasterize() const;

		[[nodiscard]] Cluster MovedBy(float start, float end) const;
	};

	// The run being shaped, reused from run to run.
	class ShapingContext {
	public:
		static constexpr int MaxRunBytes = 4096;

		// Cells are made at a quarter-pixel resolution of the cluster's position.
		static constexpr float SubpixelSteps = 4;

		// The sized font the run is shaped in.
		SizedFont* Sized = nullptr;

		// The run's UTF-16 text. Per code unit, a byte offset of -1 (a low surrogate or replaced character) gets no glyph.
		std::vector<wchar_t> Text = std::vector<wchar_t>(256);
		std::vector<int> TextToByte = std::vector<int>(256);
		std::vector<std::optional<GlyphSource>> TextSource = std::vector<std::optional<GlyphSource>>(256);
		std::vector<uint8_t> TextItalic = std::vector<uint8_t>(256);

		// Per byte of the run, whether an italic macro has it in italics.
		std::array<uint8_t, MaxRunBytes> ByteItalic{};

		// The bytes of characters composed into the one before, with that one's byte.
		std::vector<std::pair<int, int>> Absorbed;

		std::vector<Cluster> Clusters;
		std::vector<TextRun> TextRuns;
		std::vector<ShapedCluster> Shaped;
		std::vector<int> ElementStarts;

		void EnsureCapacity(int n);

		[[nodiscard]] int NextGrapheme(int t, int end) const;

		// The length is in code units. nullopt for a lone surrogate.
		[[nodiscard]] std::optional<std::pair<char32_t, int>> DecodeAt(int u, int end) const;
	};
}
