#pragma once

#include "MainApp/FontReplacement/Fonts/Text/ShapingContext.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class GlyphFactory;
	class GlyphStore;

	// Clusters' rasters, and the cells and spacers made of them.
	class CellCache {
		GlyphStore& m_store;
		GlyphFactory& m_factory;

		// Keyed by sized font too, as families may share a font but each places cells in its own atlas with its own metrics.
		AgedMap<std::tuple<RasterKey, const SizedFont*, int, int>, GameGlyph*> m_cells;
		AgedMap<std::pair<const SizedFont*, int>, GameGlyph*> m_spacers;

		// Of outline sources only, as bitmaps are only scaled.
		AgedMap<RasterKey, std::shared_ptr<const RasterGlyph>> m_rasters;

	public:
		CellCache(GlyphStore& store, GlyphFactory& factory, const FrameState& state);
		CellCache(const CellCache&) = delete;
		CellCache& operator=(const CellCache&) = delete;

		GameGlyph* GetSpacer(const SizedFont& sized, int advance);

		[[nodiscard]] int GetEdgeMargin(const SizedFont& sized) const;

		const RasterKey& KeyOf(const SizedFont& sized, Cluster& c);

		// Whether the cluster still needs rasterizing, which the shaper may do ahead on the thread pool.
		bool NeedsRaster(const SizedFont& sized, Cluster& c);

		// Relative to the pen. The advance is final only for a monospaced cluster, as GetCell sets the others'.
		const RasterGlyph& GetRaster(const SizedFont& sized, Cluster& c);

		// Places what the source rasterized as the element does, and caches it.
		const RasterGlyph& AddRaster(const SizedFont& sized, Cluster& c, std::optional<RasterGlyph> raw);

		// The cell's box starts pad pixels left of the cluster's pen.
		GameGlyph* GetCell(const SizedFont& sized, Cluster& c, const uint8_t* bytes, int b, int pad, int advance);

		void Sweep();

		void Clear();
	};
}
