#pragma once

#include "MainApp/FontReplacement/GlyphRasterizer.h"

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

	// Rasterizes glyphs with FreeType: for elements a preset draws with FreeType, and for fonts with only bitmaps (which
	// DirectWrite doesn't draw), whose nearest bitmap size is scaled to the size asked for. Fonts are opened from the files of
	// DirectWrite's font faces, so glyph indices from DirectWrite's shaping are FreeType's.
	class FreeTypeFonts {
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

		// The size each face was last set to.
		std::map<FT_FaceRec_*, float> m_faceSizes;

		// A variable face's axes (DWRITE_FONT_AXIS_TAG, minimum, default, maximum; 16.16), and the design coordinates it was
		// last set to.
		std::map<FT_FaceRec_*, std::vector<Axis>> m_faceAxes;
		std::map<FT_FaceRec_*, std::vector<long>> m_faceCoordinates;

		explicit FreeTypeFonts(FT_LibraryRec_* library);

	public:
		FreeTypeFonts(const FreeTypeFonts&) = delete;
		FreeTypeFonts& operator=(const FreeTypeFonts&) = delete;

		// Starts FreeType; nullptr if it can't be used.
		static std::unique_ptr<FreeTypeFonts> Create();

		// Opens the font file of a DirectWrite font face (kept open, shared); nullptr if it isn't a local file.
		FT_FaceRec_* Open(IDWriteFontFace* fontFace);

		// Gets whether a face has outlines; one with only bitmaps is drawn by scaling its nearest bitmap size.
		static bool IsScalable(FT_FaceRec_* face);

		// Rasterizes a run of glyphs (a glyph, or a shaped cluster) with the pen at originX, each glyph at its advance and
		// offset, as GlyphRasterizer::RasterizeRun does with DirectWrite. A variable face is set to axes (as
		// DWRITE_FONT_AXIS_TAG; others at their defaults), and outlines are emboldened by embolden ems before they are
		// transformed.
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
			const GlyphTransform& transform,
			const std::map<uint32_t, float>* axes = nullptr,
			float embolden = 0,
			float originY = 0);

	private:
		// Sets a variable face's design coordinates: the given axis values, the others at their defaults.
		void SetAxes(FT_FaceRec_* face, const std::map<uint32_t, float>* values);

		// Sets a face to a size: an outline face to it exactly (1 returned), a bitmap face to its nearest bitmap size, the
		// smallest not below it or else the largest, returning the scale from that size to the one asked for.
		float SetSize(FT_FaceRec_* face, float px);

		// Reads a glyph slot's bitmap as 8-bit coverage (converting 1, 2 and 4-bit, and taking BGRA's alpha).
		RasterGlyph ReadBitmap(void* slot);
	};
}
