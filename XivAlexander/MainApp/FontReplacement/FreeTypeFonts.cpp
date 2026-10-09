#include "pch.h"
#include "MainApp/FontReplacement/FreeTypeFonts.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_BITMAP_H
#include FT_MULTIPLE_MASTERS_H
#include FT_OUTLINE_H

#include "MainApp/FontReplacement/Host.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// FT_LOAD_TARGET_(mode): the hinting the render mode is for.
	constexpr int LoadTargetShift = 16;

	void Check(FT_Error error) {
		if (error)
			throw std::runtime_error(std::format("FreeType failed: {}", error));
	}

	// Gets the file and face index of a DirectWrite font face, if it is a local file.
	std::optional<std::pair<std::wstring, uint32_t>> GetFile(IDWriteFontFace* fontFace) {
		const auto file = FontReplacement::FontFileKey::Of(fontFace);
		IDWriteLocalFontFileLoaderPtr local;
		if (!file.Loader || FAILED(file.Loader->QueryInterface(__uuidof(IDWriteLocalFontFileLoader), reinterpret_cast<void**>(&local))))
			return std::nullopt;

		UINT32 length;
		if (FAILED(local->GetFilePathLengthFromKey(file.Key, file.KeySize, &length)))
			return std::nullopt;
		std::wstring path(length + 1, L'\0');
		if (FAILED(local->GetFilePathFromKey(file.Key, file.KeySize, path.data(), length + 1)))
			return std::nullopt;
		path.resize(length);
		return std::make_pair(std::move(path), fontFace->GetIndex());
	}
}

FontReplacement::FreeTypeFonts::FreeTypeFonts(FT_LibraryRec_* library)
	: m_library(library) {
}

void FontReplacement::FreeTypeFonts::LibraryCloser::operator()(FT_LibraryRec_* library) const {
	FT_Done_FreeType(library);
}

void FontReplacement::FreeTypeFonts::FaceCloser::operator()(FT_FaceRec_* face) const {
	FT_Done_Face(face);
}

std::unique_ptr<FontReplacement::FreeTypeFonts> FontReplacement::FreeTypeFonts::Create() {
	FT_Library library;
	if (const auto error = FT_Init_FreeType(&library)) {
		Host::Warning("FreeType can't be used; its elements are drawn with DirectWrite (error {})", error);
		return nullptr;
	}
	return std::unique_ptr<FreeTypeFonts>(new FreeTypeFonts(library));
}

FT_FaceRec_* FontReplacement::FreeTypeFonts::Open(IDWriteFontFace* fontFace) {
	const auto file = GetFile(fontFace);
	if (!file)
		return nullptr;
	if (const auto it = m_faces.find(*file); it != m_faces.end())
		return it->second.get();

	// Read through a stream of a file opened by its wide path: FT_New_Face takes paths in the ANSI code page.
	FT_Face face = nullptr;
	if (FILE* fp; _wfopen_s(&fp, file->first.c_str(), L"rb") == 0 && fp) {
		_fseeki64(fp, 0, SEEK_END);
		const auto size = _ftelli64(fp);
		const auto stream = new FT_StreamRec{};
		stream->descriptor.pointer = fp;
		stream->size = static_cast<unsigned long>(size);
		stream->read = [](FT_Stream s, unsigned long offset, unsigned char* buffer, unsigned long count) -> unsigned long {
			const auto f = static_cast<FILE*>(s->descriptor.pointer);
			if (_fseeki64(f, static_cast<__int64>(offset), SEEK_SET))
				return count ? 0 : 1;
			return count ? static_cast<unsigned long>(fread(buffer, 1, count, f)) : 0;
		};
		stream->close = [](FT_Stream s) {
			fclose(static_cast<FILE*>(s->descriptor.pointer));
			delete s;
		};
		const FT_Open_Args args{.flags = FT_OPEN_STREAM, .stream = stream};
		if (FT_Open_Face(m_library.get(), &args, static_cast<FT_Long>(file->second), &face))
			face = nullptr;
	}
	m_faces.emplace(*file, face);
	return face;
}

bool FontReplacement::FreeTypeFonts::IsScalable(FT_FaceRec_* face) {
	return (face->face_flags & FT_FACE_FLAG_SCALABLE) != 0;
}

FontReplacement::RasterGlyph FontReplacement::FreeTypeFonts::RasterizeRun(
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
	const std::map<uint32_t, float>* axes,
	float embolden,
	float originY) {
	SetAxes(face, axes);
	const auto scale = SetSize(face, px);
	const auto strength = static_cast<FT_Pos>(std::round(embolden * px * 64));

	// Emboldening needs outlines.
	const auto loadFlags = parameters.LoadFlags | ((parameters.RenderMode & 15) << LoadTargetShift) | (strength ? FT_LOAD_NO_BITMAP : 0);

	// FreeType transforms column vectors with y growing upwards (16.16 fixed point).
	FT_Matrix matrix{
		.xx = static_cast<FT_Fixed>(std::round(transform.M11 * 65536)),
		.xy = static_cast<FT_Fixed>(std::round(-transform.M12 * 65536)),
		.yx = static_cast<FT_Fixed>(std::round(-transform.M21 * 65536)),
		.yy = static_cast<FT_Fixed>(std::round(transform.M22 * 65536)),
	};

	// The glyphs' coverage, placed relative to the pen, merged into one box.
	std::vector<RasterGlyph> pieces;
	pieces.reserve(count);
	auto x = 0.f;
	for (uint32_t i = 0; i < count; i++) {
		// The glyph's origin on screen (y down): its place in the run, transformed, after the pen's origin.
		const auto lx = x + (offsets ? offsets[i].advanceOffset : 0.f);
		const auto ly = offsets ? -offsets[i].ascenderOffset : 0.f;
		const auto gx = originX + transform.M11 * lx + transform.M12 * ly;
		const auto gy = -(transform.M21 * lx + transform.M22 * ly) - originY;
		x += advances[i];

		if (FT_Load_Glyph(face, glyphs[i], loadFlags))
			continue;
		const auto slot = face->glyph;
		const auto bitmapGlyph = slot->format == FT_GLYPH_FORMAT_BITMAP;
		if (!bitmapGlyph) {
			// Outlines are emboldened, transformed, and moved by the fraction; bitmaps (of a bitmap face, scaled) are placed
			// afterwards.
			if (strength)
				FT_Outline_EmboldenXY(&slot->outline, strength, strength);
			FT_Outline_Transform(&slot->outline, &matrix);
			FT_Outline_Translate(&slot->outline, static_cast<FT_Pos>(std::round((gx - std::floor(gx)) * 64)), static_cast<FT_Pos>(std::round(gy * 64)));
			if (FT_Render_Glyph(slot, static_cast<FT_Render_Mode>(parameters.RenderMode)))
				continue;
		}

		auto piece = ReadBitmap(slot);
		if (piece.Width == 0)
			continue;

		// Bitmaps ignore the transform: scaled with the fraction, or at their own size moved by whole pixels.
		const auto fraction = gx - std::floor(gx);
		if (scale != 1) {
			piece = piece.Scaled(scale, fraction, -gy);
		} else if (bitmapGlyph) {
			piece.Left += static_cast<int>(std::round(fraction));
			piece.Top -= static_cast<int>(std::round(gy));
		}
		piece.Left += static_cast<int>(std::floor(gx));
		pieces.push_back(std::move(piece));
	}

	return RasterGlyph::Merge(std::move(pieces), advance);
}

void FontReplacement::FreeTypeFonts::SetAxes(FT_FaceRec_* face, const std::map<uint32_t, float>* values) {
	if (!(face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS))
		return;

	auto it = m_faceAxes.find(face);
	if (it == m_faceAxes.end()) {
		std::vector<Axis> axes;
		if (FT_MM_Var* mm; !FT_Get_MM_Var(face, &mm)) {
			for (FT_UInt i = 0; i < mm->num_axis; i++) {
				// FreeType's tags have the first letter highest; DirectWrite's lowest.
				const auto& a = mm->axis[i];
				axes.push_back({_byteswap_ulong(static_cast<uint32_t>(a.tag)), a.minimum, a.def, a.maximum});
			}
			FT_Done_MM_Var(m_library.get(), mm);
		}
		it = m_faceAxes.emplace(face, std::move(axes)).first;
	}

	const auto& axes = it->second;
	if (axes.empty())
		return;

	std::vector<long> coordinates(axes.size());
	for (size_t i = 0; i < axes.size(); i++) {
		const auto& a = axes[i];
		const auto v = values ? values->find(a.Tag) : decltype(values->find(a.Tag)){};
		coordinates[i] = values && v != values->end()
			? std::clamp(static_cast<long>(std::round(v->second * 65536)), a.Min, a.Max)
			: a.Default;
	}

	if (const auto current = m_faceCoordinates.find(face); current != m_faceCoordinates.end() && current->second == coordinates)
		return;
	std::vector<FT_Fixed> fixed(coordinates.begin(), coordinates.end());
	Check(FT_Set_Var_Design_Coordinates(face, static_cast<FT_UInt>(fixed.size()), fixed.data()));
	m_faceCoordinates[face] = std::move(coordinates);
}

float FontReplacement::FreeTypeFonts::SetSize(FT_FaceRec_* face, float px) {
	if (IsScalable(face)) {
		if (const auto it = m_faceSizes.find(face); it == m_faceSizes.end() || it->second != px) {
			Check(FT_Set_Char_Size(face, 0, static_cast<FT_F26Dot6>(std::round(px * 64)), 72, 72));
			m_faceSizes[face] = px;
		}
		return 1;
	}

	auto best = -1;
	auto bestPx = 0.f;
	for (auto i = 0; i < face->num_fixed_sizes; i++) {
		const auto strike = static_cast<float>(face->available_sizes[i].y_ppem) / 64.f;
		if (best < 0 || (bestPx < px ? strike > bestPx : strike >= px && strike < bestPx))
			best = i, bestPx = strike;
	}

	if (best < 0)
		throw std::runtime_error("The font has neither outlines nor bitmaps.");
	Check(FT_Select_Size(face, best));
	m_faceSizes.erase(face);
	return px / bestPx;
}

FontReplacement::RasterGlyph FontReplacement::FreeTypeFonts::ReadBitmap(void* slotPtr) {
	const auto slot = static_cast<FT_GlyphSlot>(slotPtr);
	const auto bitmap = &slot->bitmap;
	const auto w = static_cast<int>(bitmap->width), h = static_cast<int>(bitmap->rows);
	if (w == 0 || h == 0)
		return {};

	std::vector<uint8_t> alpha(static_cast<size_t>(w) * h);
	if (bitmap->pixel_mode == FT_PIXEL_MODE_BGRA) {
		for (auto y = 0; y < h; y++) {
			for (auto x = 0; x < w; x++)
				alpha[static_cast<size_t>(y) * w + x] = bitmap->buffer[y * bitmap->pitch + x * 4 + 3];
		}
	} else {
		FT_Bitmap converted;
		FT_Bitmap_Init(&converted);
		Check(FT_Bitmap_Convert(m_library.get(), bitmap, &converted, 1));
		const auto max = (std::max)(1, converted.num_grays - 1);
		for (auto y = 0; y < h; y++) {
			for (auto x = 0; x < w; x++)
				alpha[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(converted.buffer[y * converted.pitch + x] * 255 / max);
		}
		FT_Bitmap_Done(m_library.get(), &converted);
	}

	return {0, slot->bitmap_left, -slot->bitmap_top, w, h, std::move(alpha)};
}
