#pragma once

#include "Config/FontReplacementConfigs.h"
#include "MainApp/FontReplacement/Game/KernelTextureWrapper.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	using AtlasTextureFormat = FontReplacementAtlasTextureFormat;

	// The font shaders take UVs in texels of the bound texture, so pages may be smaller than the game's; subpixel atlases have odd widths (how
	// FontSubpixelPS tells them apart) and take R, G, B for subpixel coverage and A for grayscale.
	class GlyphAtlas {
	public:
		static constexpr int PlanesPerPage = 4;
		static constexpr int SubpixelPlane = 3;

		[[nodiscard]] static int PaddedArea(int width, int height);
		[[nodiscard]] static uint8_t Get4444(uint16_t texel, int plane);
		[[nodiscard]] static int BgraOffset(int plane);

	private:
		struct RowCursor {
			int X = 0;
			int Y = 0;
			int Height = 0;
		};

		struct Page {
			int Width;
			int Height;
			KernelTextureWrapper Texture;
			std::vector<uint8_t> Shadow;

			RowCursor CursorsPerPlane[PlanesPerPage]{};

			int DirtyLeft;
			int DirtyTop;
			int DirtyRight = 0;
			int DirtyBottom = 0;

			Page(int width, int height, bool format4444);

			void MarkDirty(int left, int top, int right, int bottom);
			void ClearDirty();
		};

		const int m_width;
		const int m_height;
		const std::string m_name;
		const bool m_subpixel;
		const bool m_format4444;
		const int m_bytesPerPixel;

		// Slots never move and a page is stored before the count that publishes it, so pages below PageCount() can be read without the lock.
		const int m_maxPages;
		const std::unique_ptr<std::unique_ptr<Page>[]> m_pages;
		std::atomic<int> m_pageCount = 0;

		// Guards the pages' contents, cursors and dirty rects, m_current, m_frontier and m_context.
		std::mutex m_mutex;
		ID3D11DeviceContextPtr m_context;

		// Planes are numbered page * PlanesPerPage + plane; those from m_frontier on are unused since Clear, those before it full.
		int m_current = 0;
		int m_frontier = 1;

	public:
		// Sides up to 4096, as glyph positions take 12 bits; maxPages is clamped to the texture slots.
		GlyphAtlas(int width, int height, int maxPages, std::string name, bool subpixel, AtlasTextureFormat format);
		GlyphAtlas(const GlyphAtlas&) = delete;
		GlyphAtlas& operator=(const GlyphAtlas&) = delete;
		~GlyphAtlas();

		// Called outside the lock, on whichever thread added the page.
		std::function<void()> PageAdded;

		[[nodiscard]] int Width() const { return m_width; }
		[[nodiscard]] int Height() const { return m_height; }
		[[nodiscard]] const std::string& Name() const { return m_name; }
		[[nodiscard]] bool Subpixel() const { return m_subpixel; }
		[[nodiscard]] int MaxPlanes() const { return m_maxPages * PlanesPerPage; }
		[[nodiscard]] int PageCount() const { return m_pageCount.load(std::memory_order_acquire); }

		// page < PageCount().
		[[nodiscard]] uintptr_t GetKernelTexture(int page) const { return m_pages[page]->Texture.Kernel(); }

		[[nodiscard]] bool CanHold(int width, int height) const;

		// Game thread, between frames, as no glyph of the old contents may be drawn afterwards; keeps the textures, which fonts refer to.
		void Clear();

		// Between frames, as for Clear; glyphs are allocated in the plane from now on.
		void ClearPlane(int page, int plane);

		// The plane is a channel index, and (x, y) the top left inside the padding.
		bool TryAllocate(int width, int height, int& page, int& plane, int& x, int& y);

		// Coverage goes at alphaX in the box, cut off past its right edge; in a subpixel atlas, lcd (3 bytes a pixel) fills R, G and B, else they
		// get alpha.
		void Write(int page, int plane, int x, int y, int width, int height, std::span<const uint8_t> alpha, int alphaStride, int alphaX, std::span<const uint8_t> lcd = {});

		[[nodiscard]] std::vector<uint8_t> Read(int page, int plane, int x, int y, int width, int height);

		// 3 bytes a pixel.
		[[nodiscard]] std::vector<uint8_t> ReadLcd(int page, int x, int y, int width, int height);

		// Present thread.
		void Upload();

		bool TryEnsurePages(int count);

	private:
		[[nodiscard]] int Pitch() const { return m_width * m_bytesPerPixel; }
		[[nodiscard]] int FirstPlane() const { return m_subpixel ? SubpixelPlane : 0; }
		[[nodiscard]] int PlaneStep() const { return m_subpixel ? PlanesPerPage : 1; }

		void SetChannel(uint8_t* texel, int plane, uint8_t value) const;
		[[nodiscard]] uint8_t GetChannel(const uint8_t* texel, int plane) const;
	};
}
