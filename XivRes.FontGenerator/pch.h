#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <ranges>

#include <Windows.h>
#include <windowsx.h>
#include <CommCtrl.h>
#include <ShObjIdl.h>

#pragma comment(lib, "Comctl32.lib")

#include <zlib.h>
#include <minizip/zip.h>
#include <minizip/iowin32.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_BITMAP_H
#include FT_OUTLINE_H 
#include FT_GLYPH_H

#include <nlohmann/json.hpp>

#include "XivRes/FontdataStream.h"
#include "XivRes/GameReader.h"
#include "XivRes/MipmapStream.h"
#include "XivRes/PackedFileUnpackingStream.h"
#include "XivRes/PixelFormats.h"
#include "XivRes/TextureStream.h"
#include "XivRes/Internal/TexturePreview.Windows.h"

extern const char8_t* const g_pszTestString;
extern HINSTANCE g_hInstance;
