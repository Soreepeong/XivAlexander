#pragma once

#include "Config/FontReplacementConfigs.h"
#include "MainApp/FontReplacement/Game/KernelTextureWrapper.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	using AtlasTextureFormat = FontReplacementAtlasTextureFormat;

	// A game font family's pages: B8G8R8A8 or B4G4R4A4 (16 coverage levels), a glyph plane per channel as in the game's own atlases; may be smaller
	// than the game's, as shaders divide UVs by the bound texture's size. Only the edge/glare texel step follows the font's claimed texture width.
	// A page's index is the texture index its glyphs have in a copy. Glyphs go to a CPU copy, uploaded by Upload.
	// Subpixel atlases have odd widths (how FontSubpixelPS tells them apart); glyphs take R, G, B for subpixel coverage and A (SubpixelPlane) for grayscale.
	class GlyphAtlas {
	public:
		static constexpr int PlanesPerPage = 4;

		static constexpr int SubpixelPlane = 3;

		// A font's texture slots.
		[[nodiscard]] static int MaxTextureSlots();

		[[nodiscard]] static int PaddedArea(int width, int height);

		// Of a plane (R, G, B, A channel) in a B4G4R4A4 texel, as 8-bit coverage.
		[[nodiscard]] static uint8_t Get4444(uint16_t texel, int plane);

		// Of a plane in a B8G8R8A8 texel.
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

		// Planes are numbered page * PlanesPerPage + plane. Glyphs go in m_current; planes from m_frontier on are unused since Clear, others full.
		int m_current = 0;
		int m_frontier = 1;

	public:
		// Sides up to 4096 (glyph positions take 12 bits); an odd width for a subpixel atlas. maxPages is clamped to the texture slots.
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

		[[nodiscard]] int MaxPages() const { return m_maxPages; }

		[[nodiscard]] int MaxPlanes() const { return m_maxPages * PlanesPerPage; }

		[[nodiscard]] int PageCount() const { return m_pageCount.load(std::memory_order_acquire); }

		// page < PageCount().
		[[nodiscard]] uintptr_t GetKernelTexture(int page) const { return m_pages[page]->Texture.Kernel(); }

		// With its padding, in one empty plane.
		[[nodiscard]] bool CanHold(int width, int height) const;

		// Keeps the textures (fonts refer to them). Game thread, between frames: no glyph referring to the old contents may be drawn afterwards.
		void Clear();

		// Between frames, as for Clear; the page is uploaded whole. Glyphs are allocated in it from now on.
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

		// Adds pages up to count; the textures are made without the lock. false if count is past MaxPages().
		bool TryEnsurePages(int count);

	private:
		[[nodiscard]] int Pitch() const { return m_width * m_bytesPerPixel; }
		[[nodiscard]] int FirstPlane() const { return m_subpixel ? SubpixelPlane : 0; }
		[[nodiscard]] int PlaneStep() const { return m_subpixel ? PlanesPerPage : 1; }

		void SetChannel(uint8_t* texel, int plane, uint8_t value) const;
		[[nodiscard]] uint8_t GetChannel(const uint8_t* texel, int plane) const;
	};
}
