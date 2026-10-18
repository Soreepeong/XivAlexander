#pragma once

#include <FontChanger.FixedSizeFont/freetype_fixed_size_font.h>

#include "MainApp/FontReplacement/Fonts/Enums/SubpixelOrder.h"
#include "MainApp/FontReplacement/Fonts/Records/RasterGlyph.h"
#include "MainApp/FontReplacement/Utilities/Misc.h"
#include "MainApp/FontReplacement/Utilities/Matrix2x2.h"

struct FT_LibraryRec_;
struct FT_FaceRec_;
struct FT_GlyphSlotRec_;

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// FT_LOAD_* flags and an FT_Render_Mode.
	struct FreeTypeParams {
		int LoadFlags = 0;
		int RenderMode = 1;

		static FreeTypeParams Of(const FontChanger::FixedSizeFont::freetype_fixed_size_font::create_struct& c) {
			return {c.LoadFlags, static_cast<int>(c.RenderMode)};
		}
	};

	// Also draws fonts with only bitmaps, which DirectWrite doesn't; faces are opened from DirectWrite's files so that its shaping's glyph indices are
	// FreeType's.
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

		// Declared before m_faces so that the faces are closed before the library.
		std::unique_ptr<FT_LibraryRec_, LibraryCloser> m_library;
		std::map<std::pair<std::wstring, uint32_t>, std::unique_ptr<FT_FaceRec_, FaceCloser>> m_faces;

		std::map<FT_FaceRec_*, float> m_faceSizes;

		// Tags are DWRITE_FONT_AXIS_TAG, and values 16.16 fixed point.
		std::map<FT_FaceRec_*, std::vector<Axis>> m_faceAxes;
		std::map<FT_FaceRec_*, std::vector<long>> m_faceCoordinates;

		SubpixelOrder m_subpixelOrder = SubpixelOrder::None;

	public:
		// Throws if FreeType can't be started.
		FreeTypeGlyphRasterizer();
		FreeTypeGlyphRasterizer(const FreeTypeGlyphRasterizer&) = delete;
		FreeTypeGlyphRasterizer& operator=(const FreeTypeGlyphRasterizer&) = delete;

		// The face is kept open and shared; nullptr if it isn't a local file FreeType can open.
		FT_FaceRec_* Open(IDWriteFontFace* fontFace);

		static bool IsScalable(FT_FaceRec_* face);

		// Game thread, before glyphs are made again.
		void SetSubpixelOrder(SubpixelOrder order) { m_subpixelOrder = order; }

		// Also reorders lcd in place from R, G, B to the stripes' order.
		[[nodiscard]] static std::vector<uint8_t> TakeSubpixelMean(std::span<uint8_t> lcd, bool blueFirst);

		// axes is keyed by DWRITE_FONT_AXIS_TAG, missing axes at their defaults, embolden is in ems, applied before the transformation, and originY places
		// the baseline for glyph merging.
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

	private:
		void SetAxes(FT_FaceRec_* face, const std::map<uint32_t, float>* values);

		// Returns the scale from the bitmap size chosen (the smallest not below px, else the largest) to px; 1 for an outline face.
		float SetSize(FT_FaceRec_* face, float px);

		RasterGlyph ReadBitmap(FT_GlyphSlotRec_* slot);
	};
}
