#include "pch.h"
#include "MainApp/FontReplacement/GlyphAtlas.h"

#include "MainApp/FontReplacement/FontStructs.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// Glyph rectangles are padded by this much on each side: the edge and outline passes sample a texel around a glyph.
	constexpr int Padding = 1;

	// Byte offset in a B8G8R8A8 texel of the channel a glyph's plane index selects (the shader masks R, G, B, A).
	constexpr int ChannelOffsets[]{2, 1, 0, 3};
}

int FontReplacement::GlyphAtlas::FirstTextureIndex = 0;

int FontReplacement::GlyphAtlas::MaxPages() {
	return GameFont::MaxTextures - FirstTextureIndex;
}

int FontReplacement::GlyphAtlas::PaddedArea(int width, int height) {
	return (width + 2 * Padding) * (height + 2 * Padding);
}

FontReplacement::GlyphAtlas::Page::Page(int size)
	: Size(size)
	, Texture(size, size)
	, Shadow(static_cast<size_t>(size) * 4 * size)
	, DirtyLeft(size)
	, DirtyTop(size) {
	// The texture's contents are undefined until written: upload the cleared copy once.
	MarkDirty(0, 0, size, size);
}

void FontReplacement::GlyphAtlas::Page::MarkDirty(int left, int top, int right, int bottom) {
	DirtyLeft = (std::min)(DirtyLeft, left);
	DirtyTop = (std::min)(DirtyTop, top);
	DirtyRight = (std::max)(DirtyRight, right);
	DirtyBottom = (std::max)(DirtyBottom, bottom);
}

void FontReplacement::GlyphAtlas::Page::ClearDirty() {
	DirtyLeft = DirtyTop = Size;
	DirtyRight = DirtyBottom = 0;
}

FontReplacement::GlyphAtlas::GlyphAtlas(int size, std::string name)
	: m_size(size)
	, m_name(std::move(name)) {
}

FontReplacement::GlyphAtlas::~GlyphAtlas() {
	const auto lock = std::scoped_lock(m_mutex);
	m_pages.clear();
}

void FontReplacement::GlyphAtlas::EnsurePage() {
	if (m_pages.empty() && !TryAddPage())
		throw std::runtime_error("Could not add an atlas page.");
}

void FontReplacement::GlyphAtlas::Clear() {
	const auto lock = std::scoped_lock(m_mutex);
	for (const auto& p : m_pages) {
		std::ranges::fill(p->Shadow, 0);
		std::ranges::fill(p->Shelves, Shelf{});
		p->MarkDirty(0, 0, m_size, m_size);
	}
	m_current = 0;
	m_frontier = 1;
}

void FontReplacement::GlyphAtlas::ClearPlane(int page, int plane) {
	const auto lock = std::scoped_lock(m_mutex);
	auto& p = *m_pages[page];
	const auto channel = ChannelOffsets[plane];
	const auto pitch = m_size * 4;
	for (auto row = 0; row < m_size; row++) {
		const auto d = &p.Shadow[static_cast<size_t>(row) * pitch + channel];
		for (auto col = 0; col < m_size; col++)
			d[col * 4] = 0;
	}
	p.Shelves[plane] = {};
	p.MarkDirty(0, 0, m_size, m_size);
	m_current = page * PlanesPerPage + plane;
}

bool FontReplacement::GlyphAtlas::TryAllocate(int width, int height, int& page, int& plane, int& x, int& y) {
	page = plane = x = y = 0;
	const auto w = width + 2 * Padding;
	const auto h = height + 2 * Padding;
	if (w > m_size || h > m_size)
		return false;

	if (m_pages.empty() && !TryAddPage())
		return false;

	while (true) {
		page = m_current / PlanesPerPage;
		plane = m_current % PlanesPerPage;
		auto& s = m_pages[page]->Shelves[plane];
		if (s.X + w > m_size)
			s = {.Y = s.Y + s.Height};
		if (s.Y + h <= m_size) {
			x = s.X + Padding;
			y = s.Y + Padding;
			s.X += w;
			s.Height = (std::max)(s.Height, h);
			return true;
		}

		// The current plane is full: on to one not used yet.
		if (m_frontier >= MaxPlanes())
			return false;
		if (m_frontier / PlanesPerPage >= static_cast<int>(m_pages.size()) && !TryAddPage())
			return false;
		m_current = m_frontier++;
	}
}

void FontReplacement::GlyphAtlas::Write(int page, int plane, int x, int y, int width, int height, std::span<const uint8_t> alpha, int alphaStride, int alphaX) {
	auto& p = *m_pages[page];
	const auto columns = (std::min)(alphaStride, width - alphaX);
	const auto pitch = m_size * 4;
	const auto lock = std::scoped_lock(m_mutex);
	const auto dst = &p.Shadow[static_cast<size_t>(y) * pitch + static_cast<size_t>(x) * 4 + ChannelOffsets[plane]];
	for (auto row = 0; row < height; row++) {
		const auto src = &alpha[static_cast<size_t>(row) * alphaStride];
		const auto d = dst + static_cast<size_t>(row) * pitch + static_cast<size_t>(alphaX) * 4;
		for (auto col = 0; col < columns; col++)
			d[col * 4] = src[col];
	}
	p.MarkDirty(x, y, x + width, y + height);
}

std::vector<uint8_t> FontReplacement::GlyphAtlas::Read(int page, int plane, int x, int y, int width, int height) {
	std::vector<uint8_t> alpha(static_cast<size_t>(width) * height);
	const auto pitch = m_size * 4;
	const auto lock = std::scoped_lock(m_mutex);
	const auto src = &m_pages[page]->Shadow[static_cast<size_t>(y) * pitch + static_cast<size_t>(x) * 4 + ChannelOffsets[plane]];
	for (auto row = 0; row < height; row++) {
		const auto s = src + static_cast<size_t>(row) * pitch;
		for (auto col = 0; col < width; col++)
			alpha[static_cast<size_t>(row) * width + col] = s[col * 4];
	}
	return alpha;
}

void FontReplacement::GlyphAtlas::Upload() {
	if (!m_context)
		return;
	const auto pitch = m_size * 4;
	const auto lock = std::scoped_lock(m_mutex);
	for (const auto& p : m_pages) {
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
			&p->Shadow[static_cast<size_t>(p->DirtyTop) * pitch + static_cast<size_t>(p->DirtyLeft) * 4],
			static_cast<UINT>(pitch),
			0);
		p->ClearDirty();
	}
}

bool FontReplacement::GlyphAtlas::TryAddPage() {
	if (static_cast<int>(m_pages.size()) >= MaxPages())
		return false;

	auto page = std::make_unique<Page>(m_size);
	{
		const auto lock = std::scoped_lock(m_mutex);
		if (!m_context) {
			ID3D11DevicePtr device;
			page->Texture.Resource()->GetDevice(&device);
			device->GetImmediateContext(&m_context);
		}
		m_pages.push_back(std::move(page));
	}

	Host::Information("Added {} atlas page {} ({} x {})", m_name, m_pages.size(), m_size, m_size);
	if (PageAdded)
		PageAdded();
	return true;
}
