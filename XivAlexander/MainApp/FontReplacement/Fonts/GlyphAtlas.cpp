#include "pch.h"
#include "MainApp/FontReplacement/Fonts/GlyphAtlas.h"

#include "MainApp/FontReplacement/FontStructs.h"
#include "MainApp/FontReplacement/Host.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// The edge and outline passes sample a texel around a glyph.
	constexpr int Padding = 1;

	// Planes are the R, G, B and A channels.
	constexpr int Bgra8888ChannelOffsets[]{2, 1, 0, 3};
	constexpr int Bgra4444ChannelShifts[]{8, 4, 0, 12};

	constexpr auto Lookup8To4 = [] {
		std::array<uint8_t, 256> res{};
		for (auto v = 0; v < 256; v++)
			res[v] = static_cast<uint8_t>((v * 15 + 127) / 255);
		return res;
	}();
}

uint8_t FontReplacement::GlyphAtlas::Get4444(uint16_t texel, int plane) {
	return static_cast<uint8_t>(((texel >> Bgra4444ChannelShifts[plane]) & 0xF) * 17);
}

int FontReplacement::GlyphAtlas::BgraOffset(int plane) {
	return Bgra8888ChannelOffsets[plane];
}

FontReplacement::GlyphAtlas::Page::Page(int width, int height, bool format4444)
	: Width(width)
	, Height(height)
	, Texture(width, height, format4444)
	, Shadow(static_cast<size_t>(width) * (format4444 ? 2 : 4) * height)
	, DirtyLeft(width)
	, DirtyTop(height) {
	// The texture's contents are undefined until written, so the cleared copy is uploaded once.
	MarkDirty(0, 0, width, height);
}

void FontReplacement::GlyphAtlas::Page::MarkDirty(int left, int top, int right, int bottom) {
	DirtyLeft = (std::min)(DirtyLeft, left);
	DirtyTop = (std::min)(DirtyTop, top);
	DirtyRight = (std::max)(DirtyRight, right);
	DirtyBottom = (std::max)(DirtyBottom, bottom);
}

void FontReplacement::GlyphAtlas::Page::ClearDirty() {
	DirtyLeft = Width;
	DirtyTop = Height;
	DirtyRight = DirtyBottom = 0;
}

FontReplacement::GlyphAtlas::GlyphAtlas(int width, int height, int maxPages, std::string name, bool subpixel, AtlasTextureFormat format)
	: m_width(width)
	, m_height(height)
	, m_name(std::move(name))
	, m_subpixel(subpixel)
	, m_format4444(format == AtlasTextureFormat::Bgra4444)
	, m_bytesPerPixel(m_format4444 ? 2 : 4)
	, m_maxPages(std::clamp(maxPages, 1, (std::max)(1, GameFont::MaxTextures)))
	, m_pages(std::make_unique<std::unique_ptr<Page>[]>(m_maxPages))
	, m_current(FirstPlane())
	, m_frontier(FirstPlane() + PlaneStep()) {
}

FontReplacement::GlyphAtlas::~GlyphAtlas() {
	const auto lock = std::scoped_lock(m_mutex);
	for (auto i = m_pageCount.exchange(0); i-- > 0;)
		m_pages[i].reset();
	m_context = nullptr;
}

void FontReplacement::GlyphAtlas::SetChannel(uint8_t* texel, int plane, uint8_t value) const {
	if (!m_format4444) {
		texel[BgraOffset(plane)] = value;
		return;
	}

	const auto shift = Bgra4444ChannelShifts[plane];
	uint16_t t;
	std::memcpy(&t, texel, sizeof t);
	t = static_cast<uint16_t>((t & ~(0xF << shift)) | (Lookup8To4[value] << shift));
	std::memcpy(texel, &t, sizeof t);
}

void FontReplacement::GlyphAtlas::ZeroRect(Page& p, int plane, int x, int y, int width, int height) {
	for (auto row = y; row < y + height; row++) {
		const auto d = p.Shadow.data() + static_cast<size_t>(row) * Pitch() + static_cast<size_t>(x) * m_bytesPerPixel;
		if (m_subpixel) {
			std::fill_n(d, static_cast<size_t>(width) * m_bytesPerPixel, static_cast<uint8_t>(0));
			continue;
		}
		for (auto col = 0; col < width; col++)
			SetChannel(d + static_cast<size_t>(col) * m_bytesPerPixel, plane, 0);
	}
	p.MarkDirty(x, y, x + width, y + height);
}

bool FontReplacement::GlyphAtlas::CanHold(int width, int height) const {
	return width + 2 * Padding <= m_width && height + 2 * Padding <= m_height;
}

void FontReplacement::GlyphAtlas::Clear() {
	const auto lock = std::scoped_lock(m_mutex);
	for (auto i = 0, count = PageCount(); i < count; i++) {
		auto& p = *m_pages[i];
		std::ranges::fill(p.Shadow, 0);
		std::ranges::fill(p.CursorsPerPlane, RowCursor{});
		for (auto& generation : p.Generations)
			generation++;
		std::ranges::fill(p.Recycled, false);
		std::ranges::fill(p.Drawn, 0);
		p.MarkDirty(0, 0, m_width, m_height);
	}
	m_current = FirstPlane();
	m_frontier = FirstPlane() + PlaneStep();
}

int FontReplacement::GlyphAtlas::RecycleLeastRecentlyDrawn() {
	const auto lock = std::scoped_lock(m_mutex);
	const auto drawn = [this](int i) { return m_pages[i / PlanesPerPage]->Drawn[i % PlanesPerPage]; };
	auto victim = FirstPlane();
	for (auto i = victim + PlaneStep(); i < m_frontier && i / PlanesPerPage < PageCount(); i += PlaneStep()) {
		if (drawn(i) < drawn(victim))
			victim = i;
	}

	auto& p = *m_pages[victim / PlanesPerPage];
	const auto plane = victim % PlanesPerPage;
	p.CursorsPerPlane[plane] = {};
	p.Generations[plane]++;
	p.Recycled[plane] = true;
	m_current = victim;
	return victim;
}

bool FontReplacement::GlyphAtlas::Touch(int page, int plane, uint32_t generation, uint64_t tick) {
	const auto lock = std::scoped_lock(m_mutex);
	auto& p = *m_pages[page];
	if (p.Generations[plane] != generation)
		return false;
	p.Drawn[plane] = tick;
	return true;
}

bool FontReplacement::GlyphAtlas::TryAllocate(int width, int height, uint64_t tick, Allocation& res) {
	res = {};
	auto& page = res.Page;
	auto& plane = res.Plane;
	const auto w = width + 2 * Padding;
	const auto h = height + 2 * Padding;
	if (!CanHold(width, height))
		return false;

	if (!TryEnsurePages(1))
		return false;

	auto lock = std::unique_lock(m_mutex);
	while (true) {
		page = m_current / PlanesPerPage;
		plane = m_current % PlanesPerPage;
		auto& p = *m_pages[page];
		auto& s = p.CursorsPerPlane[plane];
		if (s.X + w > m_width)
			s = {.Y = s.Y + s.Height};
		if (s.Y + h <= m_height) {
			res.X = s.X + Padding;
			res.Y = s.Y + Padding;
			res.Generation = p.Generations[plane];
			if (p.Recycled[plane])
				ZeroRect(p, plane, s.X, s.Y, w, h);
			p.Drawn[plane] = tick;
			s.X += w;
			s.Height = (std::max)(s.Height, h);
			return true;
		}

		if (m_frontier >= MaxPlanes())
			return false;

		// A new page is made without the lock (it raises PageAdded); Clear may have run meanwhile, so the cursors are looked at again.
		if (const auto needed = m_frontier / PlanesPerPage + 1; needed > PageCount()) {
			lock.unlock();
			if (!TryEnsurePages(needed))
				return false;
			lock.lock();
			continue;
		}
		m_current = m_frontier;
		m_frontier += PlaneStep();
	}
}

void FontReplacement::GlyphAtlas::Write(int page, int plane, int x, int y, int width, int height, std::span<const uint8_t> alpha, int alphaStride, int alphaX, std::span<const uint8_t> lcd) {
	const auto columns = (std::min)(alphaStride, width - alphaX);
	const auto lock = std::scoped_lock(m_mutex);
	auto& p = *m_pages[page];
	const auto dst = p.Shadow.data() + static_cast<size_t>(y) * Pitch() + static_cast<size_t>(x) * m_bytesPerPixel;
	if (m_subpixel) {
		const auto hasLcd = !lcd.empty() && lcd.size() >= alpha.size() * 3;
		for (auto row = 0; row < height; row++) {
			const auto d = dst + static_cast<size_t>(row) * Pitch() + static_cast<size_t>(alphaX) * m_bytesPerPixel;
			for (auto col = 0; col < columns; col++) {
				const auto i = static_cast<size_t>(row) * alphaStride + col;
				const auto texel = d + static_cast<size_t>(col) * m_bytesPerPixel;
				SetChannel(texel, SubpixelPlane, alpha[i]);
				for (auto c = 0; c < 3; c++)
					SetChannel(texel, c, hasLcd ? lcd[i * 3 + c] : alpha[i]);
			}
		}
	} else {
		for (auto row = 0; row < height; row++) {
			const auto src = alpha.subspan(static_cast<size_t>(row) * alphaStride, alphaStride);
			const auto d = dst + static_cast<size_t>(row) * Pitch() + static_cast<size_t>(alphaX) * m_bytesPerPixel;
			for (auto col = 0; col < columns; col++)
				SetChannel(d + static_cast<size_t>(col) * m_bytesPerPixel, plane, src[col]);
		}
	}
	p.MarkDirty(x, y, x + width, y + height);
}

void FontReplacement::GlyphAtlas::Upload() {
	const auto lock = std::scoped_lock(m_mutex);
	if (!m_context)
		return;
	for (auto i = 0, count = PageCount(); i < count; i++) {
		const auto& p = m_pages[i];
		if (p->DirtyRight <= p->DirtyLeft)
			continue;
		const D3D11_BOX box{
			.left = static_cast<UINT>(p->DirtyLeft),
			.top = static_cast<UINT>(p->DirtyTop),
			.front = 0,
			.right = static_cast<UINT>(p->DirtyRight),
			.bottom = static_cast<UINT>(p->DirtyBottom),
			.back = 1,
		};
		m_context->UpdateSubresource(
			p->Texture.Resource(),
			0,
			&box,
			p->Shadow.data() + static_cast<size_t>(p->DirtyTop) * Pitch() + static_cast<size_t>(p->DirtyLeft) * m_bytesPerPixel,
			static_cast<UINT>(Pitch()),
			0);
		p->ClearDirty();
	}
}

bool FontReplacement::GlyphAtlas::TryEnsurePages(int count) {
	if (count > m_maxPages)
		return false;

	while (PageCount() < count) {
		auto page = std::make_unique<Page>(m_width, m_height, m_format4444);
		auto context = GetImmediateContext(page->Texture.Resource());

		int added;
		{
			const auto lock = std::scoped_lock(m_mutex);

			// Another thread may have added it meanwhile; the page made here is then dropped.
			added = PageCount();
			if (added >= count)
				break;
			if (!m_context)
				m_context = std::move(context);
			m_pages[added] = std::move(page);
			m_pageCount.store(added + 1, std::memory_order_release);
		}

		Host::Information("Added {} atlas page {} of {} ({} x {})", m_name, added + 1, m_maxPages, m_width, m_height);
		if (PageAdded)
			PageAdded();
	}
	return true;
}
