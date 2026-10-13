#pragma once

#include "MainApp/FontReplacement/Host.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Square B8G8R8A8 pages, a glyph plane per channel; may be smaller than the game's as shaders divide UVs by the bound texture's size.
	// Only the edge/glare texel step follows the font's claimed texture width. A page's index is the texture index its glyphs have in a copy.
	// Subpixel atlases have odd sizes (how FontSubpixelPS tells them apart); glyphs take R, G, B for subpixel coverage and A (SubpixelPlane) for grayscale.
	class GlyphAtlas {
	public:
		static constexpr int PlanesPerPage = 4;

		static constexpr int SubpixelPlane = 3;

		[[nodiscard]] static int PaddedArea(int width, int height);

		[[nodiscard]] bool CanHold(int width, int height) const;

	private:
		struct Shelf {
			int X = 0;
			int Y = 0;
			int Height = 0;
		};

		struct Page {
			int Size;
			Host::Texture Texture;
			std::vector<uint8_t> Shadow;

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
		const bool m_subpixel;

		// Slots never move and a page is stored before the count that publishes it, so pages below PageCount() can be read without the lock.
		const int m_maxPages;
		const std::unique_ptr<std::unique_ptr<Page>[]> m_pages;
		std::atomic<int> m_pageCount = 0;

		// Guards the pages' contents, shelves and dirty rects, m_current, m_frontier and m_context.
		std::mutex m_mutex;
		ID3D11DeviceContextPtr m_context;

		// Planes are numbered page * PlanesPerPage + plane. Glyphs go in m_current; planes from m_frontier on are unused since Clear, others full.
		int m_current = 0;
		int m_frontier = 1;

	public:
		// size: page side length, up to 4096 (glyph positions take 12 bits); odd for a subpixel atlas. maxPages: at most a font's texture slots.
		GlyphAtlas(int size, int maxPages, std::string name, bool subpixel = false);
		GlyphAtlas(const GlyphAtlas&) = delete;
		GlyphAtlas& operator=(const GlyphAtlas&) = delete;
		~GlyphAtlas();

		[[nodiscard]] int Size() const { return m_size; }

		[[nodiscard]] const std::string& Name() const { return m_name; }

		[[nodiscard]] bool Subpixel() const { return m_subpixel; }

		[[nodiscard]] int MaxPages() const { return m_maxPages; }

		[[nodiscard]] int MaxPlanes() const { return m_maxPages * PlanesPerPage; }

		[[nodiscard]] int PageCount() const { return m_pageCount.load(std::memory_order_acquire); }

		// page < PageCount().
		[[nodiscard]] uintptr_t GetKernelTexture(int page) const { return m_pages[page]->Texture.Kernel(); }

		// Keeps the textures (fonts refer to them). Game thread, between frames: no glyph referring to the old contents may be drawn afterwards.
		void Clear();

		// Between frames, as for Clear; the page is uploaded whole.
		void ClearPlane(int page, int plane);

		// In the current plane or the next unused one (adding a page if needed); x, y are inside the padding. false if neither has room.
		bool TryAllocate(int width, int height, int& page, int& plane, int& x, int& y);

		// 8-bit coverage from alphaX in each row; coverage past the box's right edge is cut off. Marks the box for upload.
		// In a subpixel atlas, lcd (3 bytes a pixel, same layout) fills R, G and B; without it they get alpha.
		void Write(int page, int plane, int x, int y, int width, int height, std::span<const uint8_t> alpha, int alphaStride, int alphaX, std::span<const uint8_t> lcd = {});

		[[nodiscard]] std::vector<uint8_t> Read(int page, int plane, int x, int y, int width, int height);

		// 3 bytes a pixel.
		[[nodiscard]] std::vector<uint8_t> ReadLcd(int page, int x, int y, int width, int height);

		// Present thread.
		void Upload();

	private:
		// Adds pages up to count; the textures are made without the lock. false if count is past m_maxPages.
		bool TryEnsurePages(int count);

		[[nodiscard]] int FirstPlane() const { return m_subpixel ? SubpixelPlane : 0; }
		[[nodiscard]] int PlaneStep() const { return m_subpixel ? PlanesPerPage : 1; }
	};
}
