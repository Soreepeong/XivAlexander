#pragma once

#include "Config/FontReplacementConfigs.h"
#include "MainApp/FontReplacement/Game/KernelTextureWrapper.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	using AtlasTextureFormat = FontReplacementAtlasTextureFormat;

	// The font shaders take UVs in texels of the bound texture, so pages may be smaller than the game's. Subpixel atlases have odd widths (how
	// FontSubpixelPS tells them apart) and take R, G, B for subpixel coverage and A for grayscale.
	class GlyphAtlas {
	public:
		static constexpr int PlanesPerPage = 4;
		static constexpr int SubpixelPlane = 3;

		// The most pages an atlas has, which leaves every copy a texture slot for BlankSlot.
		[[nodiscard]] static int MaxTextureSlots();

		[[nodiscard]] static uint8_t Get4444(uint16_t texel, int plane);
		[[nodiscard]] static int BgraOffset(int plane);

		struct Allocation {
			int Page = 0;

			// A channel index.
			int Plane = 0;

			// The top left inside the padding.
			int X = 0;
			int Y = 0;

			// Of the plane, which a glyph placed there keeps for Touch.
			uint32_t Generation = 0;
		};

		enum class AllocationResult {
			Placed,

			// Placed on a page added for it, which the copies drawing from the atlas have no texture slot of yet.
			PlacedOnNewPage,

			Full,
		};

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

			// Bumped each time the plane is emptied or reused.
			uint32_t Generations[PlanesPerPage]{};

			// Reused or cleared, keeping its earlier pixels where nothing was placed since.
			bool Recycled[PlanesPerPage]{};

			// The frame a glyph in the plane was last drawn or placed in.
			uint64_t Drawn[PlanesPerPage]{};

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
		const int m_maxPages;

		std::vector<std::unique_ptr<Page>> m_pages;
		ID3D11DeviceContextPtr m_context;

		// Planes are numbered page * PlanesPerPage + plane. Those from m_frontier on are unused since Clear, those before it full.
		int m_current = 0;
		int m_frontier = 1;

	public:
		// Sides up to 4096, as glyph positions take 12 bits. maxPages is clamped to the texture slots.
		GlyphAtlas(int width, int height, int maxPages, std::string name, bool subpixel, AtlasTextureFormat format);
		GlyphAtlas(const GlyphAtlas&) = delete;
		GlyphAtlas& operator=(const GlyphAtlas&) = delete;
		~GlyphAtlas();

		[[nodiscard]] int Width() const { return m_width; }
		[[nodiscard]] const std::string& Name() const { return m_name; }
		[[nodiscard]] bool Subpixel() const { return m_subpixel; }
		[[nodiscard]] int MaxPlanes() const { return m_maxPages * PlanesPerPage; }
		[[nodiscard]] int PageCount() const { return static_cast<int>(m_pages.size()); }

		// The first texture slot none of the atlas's pages takes, which holds the blank texture in every copy drawing from it. A glyph with a
		// box but no pixels in the atlas samples it. Kept low, as a texture index past a font's texture count has no vertex buffer.
		[[nodiscard]] int BlankSlot() const { return m_maxPages; }

		// page < PageCount().
		[[nodiscard]] uintptr_t GetKernelTexture(int page) const { return m_pages[page]->Texture.Kernel(); }

		[[nodiscard]] bool CanHold(int width, int height) const;

		// Keeps the textures, which fonts refer to; between frames, as no glyph of the old contents may be drawn afterwards.
		// The old pixels stay until each area is allocated again, which empties it first.
		void Clear();

		// Between frames. The least recently drawn plane is allocated in from its start, and its glyphs are of an older generation from now
		// on. Returns the plane, numbered page * PlanesPerPage + plane.
		int RecycleLeastRecentlyDrawn();

		// The area is emptied first in a reused plane, and frame marks the plane drawn.
		AllocationResult TryAllocate(int width, int height, uint64_t frame, Allocation& res);

		// Marks the plane drawn in frame. False if generation is no longer the plane's, as it was reused since.
		bool Touch(int page, int plane, uint32_t generation, uint64_t frame);

		// Coverage goes at alphaX in the box, cut off past its right edge. In a subpixel atlas, lcd (3 bytes a pixel) fills R, G and B, else they
		// get alpha.
		void Write(int page, int plane, int x, int y, int width, int height, std::span<const uint8_t> alpha, int alphaStride, int alphaX, std::span<const uint8_t> lcd = {});

		// At Present.
		void Upload();

		bool TryEnsurePages(int count);

	private:
		[[nodiscard]] int Pitch() const { return m_width * m_bytesPerPixel; }
		[[nodiscard]] int FirstPlane() const { return m_subpixel ? SubpixelPlane : 0; }
		[[nodiscard]] int PlaneStep() const { return m_subpixel ? PlanesPerPage : 1; }
		[[nodiscard]] uint64_t DrawnOf(int plane) const { return m_pages[plane / PlanesPerPage]->Drawn[plane % PlanesPerPage]; }

		void SetChannel(uint8_t* texel, int plane, uint8_t value) const;

		// Every channel in a subpixel atlas.
		void ZeroRect(Page& p, int plane, int x, int y, int width, int height);
	};
}
