#pragma once

#include "MainApp/FontReplacement/Host.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Pages are square B8G8R8A8 textures with one glyph plane per channel, like the game's; glyphs go to a CPU copy, the changed rect to the GPU in Upload.
	// Font shaders take UVs as texels over the bound texture's size, so pages may be smaller than the game's; only the edge and glare shaders' texel
	// step comes from the font's claimed texture width. Pages take indices FirstTextureIndex and up; the game's textures stay below (private-use icons).
	class GlyphAtlas {
	public:
		static constexpr int PlanesPerPage = 4;

		// Past the textures of every font in the game's tables (7 in the global client). Set before the first atlas is made.
		static int FirstTextureIndex;

		[[nodiscard]] static int MaxPages();

		[[nodiscard]] static int MaxPlanes() { return MaxPages() * PlanesPerPage; }

		// Including the padding.
		[[nodiscard]] static int PaddedArea(int width, int height);

	private:
		// Glyphs go left to right from (X, Y); the next shelf starts below the tallest.
		struct Shelf {
			int X = 0;
			int Y = 0;
			int Height = 0;
		};

		struct Page {
			int Size;
			Host::Texture Texture;
			std::vector<uint8_t> Shadow;

			// The current shelf of each plane.
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

		// Planes are numbered page * PlanesPerPage + plane. Glyphs go in m_current; planes from m_frontier on are unused since Clear, others full.
		int m_current = 0;
		int m_frontier = 1;

	public:
		// size: page side length, up to 4096 (glyph positions take 12 bits).
		GlyphAtlas(int size, std::string name);
		GlyphAtlas(const GlyphAtlas&) = delete;
		GlyphAtlas& operator=(const GlyphAtlas&) = delete;
		~GlyphAtlas();

		// Called on the thread that allocates, after a page was added.
		std::function<void()> PageAdded;

		// The pages' side length.
		[[nodiscard]] int Size() const { return m_size; }

		[[nodiscard]] const std::string& Name() const { return m_name; }

		[[nodiscard]] int PageCount() const { return static_cast<int>(m_pages.size()); }

		[[nodiscard]] uintptr_t GetKernelTexture(int page) const { return m_pages[page]->Texture.Kernel(); }

		// The renderer only has vertex buffers for texture indices below a font's texture count, so a page must exist before the first glyph.
		void EnsurePage();

		// Keeps the textures (fonts refer to them). Game thread, between frames: no glyph referring to the old contents may be drawn afterwards.
		void Clear();

		// Glyphs are allocated in it from now on. Between frames, as for Clear; the page is uploaded whole.
		void ClearPlane(int page, int plane);

		// In the current plane or the next unused one (adding a page if needed); x, y are inside the padding. false if neither has room.
		bool TryAllocate(int width, int height, int& page, int& plane, int& x, int& y);

		// 8-bit coverage from alphaX in each row; coverage past the box's right edge is cut off. Marks the box for upload.
		void Write(int page, int plane, int x, int y, int width, int height, std::span<const uint8_t> alpha, int alphaStride, int alphaX);

		[[nodiscard]] std::vector<uint8_t> Read(int page, int plane, int x, int y, int width, int height);

		// Present thread.
		void Upload();

	private:
		bool TryAddPage();
	};
}
