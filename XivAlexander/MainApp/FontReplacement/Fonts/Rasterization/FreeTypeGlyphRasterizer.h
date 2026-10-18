#pragma once

#include <FontChanger.FixedSizeFont/freetype_fixed_size_font.h>

#include "MainApp/FontReplacement/Fonts/Enums/SubpixelOrder.h"
#include "MainApp/FontReplacement/Fonts/Records/RasterGlyph.h"
#include "MainApp/FontReplacement/Utilities/Misc.h"
#include "MainApp/FontReplacement/Utilities/Matrix2x2.h"

struct FT_LibraryRec_;
struct FT_FaceRec_;

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// FreeType loading and rendering of an element (FontChanger's freetype settings); FT_LOAD_* flags and an FT_Render_Mode.
	struct FreeTypeParams {
		int LoadFlags = 0;
		int RenderMode = 1;

		static FreeTypeParams Of(const FontChanger::FixedSizeFont::freetype_fixed_size_font::create_struct& c) {
			return {c.LoadFlags, static_cast<int>(c.RenderMode)};
		}
	};

	// For elements a preset draws with FreeType, and bitmap-only fonts (which DirectWrite doesn't draw) scaled from their nearest size.
	// Fonts are opened from DirectWrite faces' files, so glyph indices from DirectWrite's shaping are FreeType's.
	class FreeTypeGlyphRasterizer {
		struct Axis {
			uint32_t Tag;
			long Min;
			long Default;
			long Max;
		};

		struct LibraryCloser {
			void operator()(FT_LibraryRec_* library) const;
		};
		struct FaceCloser {
			void operator()(FT_FaceRec_* face) const;
		};

		// The faces are closed before the library.
		std::unique_ptr<FT_LibraryRec_, LibraryCloser> m_library;
		std::map<std::pair<std::wstring, uint32_t>, std::unique_ptr<FT_FaceRec_, FaceCloser>> m_faces;

		std::map<FT_FaceRec_*, float> m_faceSizes;

		// Variable faces' axes (DWRITE_FONT_AXIS_TAG, min, default, max; 16.16) and the design coordinates each was last set to.
		std::map<FT_FaceRec_*, std::vector<Axis>> m_faceAxes;
		std::map<FT_FaceRec_*, std::vector<long>> m_faceCoordinates;

		SubpixelOrder m_subpixelOrder = SubpixelOrder::None;

	public:
		// Throws if FreeType can't be started.
		FreeTypeGlyphRasterizer();
		FreeTypeGlyphRasterizer(const FreeTypeGlyphRasterizer&) = delete;
		FreeTypeGlyphRasterizer& operator=(const FreeTypeGlyphRasterizer&) = delete;
		~FreeTypeGlyphRasterizer();

		// Kept open and shared; nullptr if the font face isn't a local file.
		FT_FaceRec_* Open(IDWriteFontFace* fontFace);

		// A face with only bitmaps is drawn by scaling its nearest bitmap size.
		static bool IsScalable(FT_FaceRec_* face);

		// Vertical orders render LCD_V, with R, G, B coverage; others as the parameters say. Game thread, before glyphs are made again.
		[[nodiscard]] SubpixelOrder GetSubpixelOrder() const { return m_subpixelOrder; }
		void SetSubpixelOrder(SubpixelOrder order) { m_subpixelOrder = order; }

		// As DirectWriteGlyphRasterizer::RasterizeRun does with DirectWrite; axes are DWRITE_FONT_AXIS_TAG (others at defaults), embolden is in ems,
		// before the transform. originY places the baseline (glyph merging).
		RasterGlyph RasterizeRun(
			FT_FaceRec_* face,
			float px,
			const uint16_t* glyphs,
			const float* advances,
			const DWRITE_GLYPH_OFFSET* offsets,
			uint32_t count,
			float originX,
			int advance,
			const FreeTypeParams& parameters,
			const Matrix2x2& transform,
			const std::map<uint32_t, float>* axes,
			float embolden,
			float originY = 0);

		// Nonzero winding, into width x height coverage. Points in pixels, y down; tags are FT_CURVE_TAG_*; each contour ends at its contourEnds index.
		[[nodiscard]] std::vector<uint8_t> FillOutline(std::span<const std::pair<float, float>> points, std::span<const uint8_t> tags, std::span<const short> contourEnds, int width, int height);

	private:
		// Axes not in values are set to their defaults.
		void SetAxes(FT_FaceRec_* face, const std::map<uint32_t, float>* values);

		// Outline faces get px exactly (returns 1); bitmap faces the smallest size not below it, else the largest, returning the scale to px.
		float SetSize(FT_FaceRec_* face, float px);

		// Reads a glyph slot's bitmap as 8-bit coverage (converting 1, 2 and 4-bit, and taking BGRA's alpha); an LCD or LCD_V one as R, G, B coverage
		// too, with their mean as coverage.
		RasterGlyph ReadBitmap(void* slot);
	};
}
