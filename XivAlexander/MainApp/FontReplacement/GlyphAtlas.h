#pragma once

#include "MainApp/FontReplacement/Host.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Atlas pages for the rasterized glyphs of a game font family's replaced fonts. A page is a square B8G8R8A8 texture
	// holding four glyph planes, one per channel, as the game's own atlases do; glyphs are written to a CPU copy, and the
	// changed rectangle goes to the GPU from Upload.
	//
	// The font shaders take UVs as texels over the bound texture's size, so pages may be smaller than the game's atlases;
	// only the edge and glare shaders' texel step comes from the font's claimed texture width, which a replaced font sets
	// from its atlas's size. Pages take texture indices FirstTextureIndex and up in every replaced font; the game's own
	// textures stay below, for the glyphs left to the game (the private-use icons).
	class GlyphAtlas {
	public:
		// The planes of a page: one per channel.
		static constexpr int PlanesPerPage = 4;

		// Gets the texture index of the first page in a replaced font: past the textures of every font of the game's tables
		// (7 in the global client). Set before the first atlas is made.
		static int FirstTextureIndex;

		[[nodiscard]] static int MaxPages();

		// Gets the number of planes there can be.
		[[nodiscard]] static int MaxPlanes() { return MaxPages() * PlanesPerPage; }

		// Gets the area a glyph's box takes in a plane, with its padding.
		[[nodiscard]] static int PaddedArea(int width, int height);

	private:
		// A plane's shelf: glyphs go left to right from (X, Y), and the next shelf starts below the tallest.
		struct Shelf {
			int X = 0;
			int Y = 0;
			int Height = 0;
		};

		struct Page {
			int Size;
			Host::Texture Texture;
			std::vector<uint8_t> Shadow;

			// The shelf glyphs are put on in each plane.
			Shelf Shelves[PlanesPerPage]{};

			int DirtyLeft;
			int DirtyTop;
			int DirtyRight = 0;
			int DirtyBottom = 0;

			explicit Page(int size);

			void MarkDirty(int left, int top, int right, int bottom);
			void ClearDirty();
		};

		const int m_size;
		const std::string m_name;
		std::vector<std::unique_ptr<Page>> m_pages;
		std::mutex m_mutex;
		ID3D11DeviceContextPtr m_context;

		// Planes are numbered page * PlanesPerPage + plane. Glyphs are allocated in the current one; those from the frontier
		// on haven't been used yet (since Clear), the others before it are full.
		int m_current = 0;
		int m_frontier = 1;

	public:
		// Makes an atlas of square pages of a side length (up to 4096: glyph positions take 12 bits).
		GlyphAtlas(int size, std::string name);
		GlyphAtlas(const GlyphAtlas&) = delete;
		GlyphAtlas& operator=(const GlyphAtlas&) = delete;
		~GlyphAtlas();

		// Called on the thread that allocates, after a page was added.
		std::function<void()> PageAdded;

		// Gets the pages' side length.
		[[nodiscard]] int Size() const { return m_size; }

		[[nodiscard]] const std::string& Name() const { return m_name; }

		[[nodiscard]] int PageCount() const { return static_cast<int>(m_pages.size()); }

		[[nodiscard]] uintptr_t GetKernelTexture(int page) const { return m_pages[page]->Texture.Kernel(); }

		// Adds the first page if there is none. Glyphs refer to page texture indices, and the renderer only has vertex buffers
		// for indices below a font's texture count, so the page must be there before the first glyph is.
		void EnsurePage();

		// Empties every page, keeping the textures (fonts refer to them). Game thread, between frames: no glyph referring to
		// the old contents may be drawn afterwards.
		void Clear();

		// Empties one plane, and allocates glyphs in it from now on. Between frames, as for Clear; the page is uploaded whole.
		void ClearPlane(int page, int plane);

		// Reserves a width x height rectangle in the current plane, or the next one not used yet (adding a page if needed).
		// Returns the page, the plane (channel index) and the top-left corner inside the padding; false if neither has room.
		bool TryAllocate(int width, int height, int& page, int& plane, int& x, int& y);

		// Writes 8-bit coverage into a plane of a page, at alphaX in the width by height box at (x, y); coverage past the
		// box's right edge is cut off. Marks the box for upload.
		void Write(int page, int plane, int x, int y, int width, int height, std::span<const uint8_t> alpha, int alphaStride, int alphaX);

		// Reads back the 8-bit coverage of a width by height box of a plane.
		[[nodiscard]] std::vector<uint8_t> Read(int page, int plane, int x, int y, int width, int height);

		// Copies the changed rectangle of each page to its texture. On the thread that calls Present.
		void Upload();

	private:
		bool TryAddPage();
	};
}
