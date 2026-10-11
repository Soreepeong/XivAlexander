#include "pch.h"
#include "MainApp/FontReplacement/FamilyPreview.h"

#include <FontChanger.FixedSizeFont/merged_fixed_size_font.h>
#include <FontChanger.FixedSizeFont/text_measurer.h>
#include <FontChanger.FixedSizeFont/wrapping_fixed_size_font.h>
#include <FontChanger.Presets/ElementFonts.h>
#include <FontChanger.Presets/FaceFromFont.h>
#include <xivres/util.on_dtor.h>

#include "MainApp/FontReplacement/FontReplacer.h"
#include "MainApp/FontReplacement/GlyphRasterizer.h"
#include "MainApp/FontReplacement/PresetController.h"
#include "MainApp/FontReplacement/PreviewRenderer.h"
#include "Utils/Win32/Process.h"
#include "Config.h"
#include "resource.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;
namespace FixedSizeFont = FontChanger::FixedSizeFont;

namespace {
	constexpr int Padding = 8;

	// Game glyphs scaled to another size as the replacement does (GameTextureSource::Scale, RasterGlyph::Scaled), about the pen at the top of the line.
	class ScaledFont final : public FixedSizeFont::default_abstract_fixed_size_font {
		std::shared_ptr<FixedSizeFont::fixed_size_font> m_base;
		float m_size;
		float m_scale;
		std::map<std::pair<char32_t, char32_t>, int> m_kerningPairs;

	public:
		ScaledFont(std::shared_ptr<FixedSizeFont::fixed_size_font> base, float size)
			: m_base(std::move(base))
			, m_size(size)
			, m_scale(size / m_base->font_size()) {
			for (const auto& [pair, value] : m_base->all_kerning_pairs())
				m_kerningPairs.emplace(pair, static_cast<int>(std::round(static_cast<float>(value) * m_scale)));
		}

		[[nodiscard]] std::string family_name() const override { return m_base->family_name(); }
		[[nodiscard]] std::string subfamily_name() const override { return m_base->subfamily_name(); }
		[[nodiscard]] float font_size() const override { return m_size; }
		[[nodiscard]] int ascent() const override { return Scale(m_base->ascent()); }
		[[nodiscard]] int line_height() const override { return Scale(m_base->line_height()); }
		[[nodiscard]] const std::set<char32_t>& all_codepoints() const override { return m_base->all_codepoints(); }
		[[nodiscard]] const std::map<std::pair<char32_t, char32_t>, int>& all_kerning_pairs() const override { return m_kerningPairs; }

		[[nodiscard]] bool try_get_glyph_metrics(char32_t codepoint, FixedSizeFont::glyph_metrics& gm) const override {
			FixedSizeFont::glyph_metrics base;
			if (!m_base->try_get_glyph_metrics(codepoint, base))
				return false;
			int x0, y0, w, h;
			FontReplacement::RasterGlyph::ScaledBounds(base.X1, base.Y1, base.width(), base.height(), m_scale, 0, 0, x0, y0, w, h);
			gm.X1 = x0;
			gm.Y1 = y0;
			gm.X2 = base.is_effectively_empty() ? x0 : x0 + w;
			gm.Y2 = base.is_effectively_empty() ? y0 : y0 + h;
			gm.AdvanceX = Scale(base.AdvanceX);
			return true;
		}

		bool draw(char32_t codepoint, xivres::util::b8g8r8a8* pBuf, int drawX, int drawY, int destWidth, int destHeight, xivres::util::b8g8r8a8 fgColor, xivres::util::b8g8r8a8 bgColor) const override {
			return DrawScaled(codepoint, drawX, drawY, destWidth, destHeight, [&](int x, int y, uint8_t coverage) {
				auto& p = pBuf[static_cast<size_t>(y) * destWidth + x];
				const auto a = coverage * fgColor.A / 255;
				p.R = static_cast<uint8_t>((p.R * (255 - a) + fgColor.R * a) / 255);
				p.G = static_cast<uint8_t>((p.G * (255 - a) + fgColor.G * a) / 255);
				p.B = static_cast<uint8_t>((p.B * (255 - a) + fgColor.B * a) / 255);
				p.A = static_cast<uint8_t>(p.A + (255 - p.A) * a / 255);
			});
		}

		bool draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override {
			// As xivres's bitmap copy to L8 draws the foreground.
			return DrawScaled(codepoint, drawX, drawY, destWidth, destHeight, [&](int x, int y, uint8_t coverage) {
				auto& p = pBuf[(static_cast<size_t>(y) * destWidth + x) * stride];
				const auto a = coverage * fgOpacity / 255;
				p = static_cast<uint8_t>((p * (255 - a) + fgColor * a) / 255);
			});
		}

		[[nodiscard]] std::shared_ptr<FixedSizeFont::fixed_size_font> get_threadsafe_view() const override {
			return std::make_shared<ScaledFont>(m_base->get_threadsafe_view(), m_size);
		}

		[[nodiscard]] const FixedSizeFont::fixed_size_font* get_base_font(char32_t codepoint) const override { return this; }

	private:
		[[nodiscard]] int Scale(int value) const { return static_cast<int>(std::round(static_cast<float>(value) * m_scale)); }

		template<typename TBlend>
		bool DrawScaled(char32_t codepoint, int drawX, int drawY, int destWidth, int destHeight, TBlend&& blend) const {
			FixedSizeFont::glyph_metrics gm;
			if (!m_base->try_get_glyph_metrics(codepoint, gm))
				return false;
			if (gm.is_effectively_empty())
				return true;
			std::vector<uint8_t> alpha(static_cast<size_t>(gm.width()) * gm.height());
			m_base->draw(codepoint, alpha.data(), 1, -gm.X1, -gm.Y1, gm.width(), gm.height(), 255, 0, 255, 0);
			const auto scaled = FontReplacement::RasterGlyph{0, gm.X1, gm.Y1, gm.width(), gm.height(), std::move(alpha)}.Scaled(m_scale, 0, 0);
			for (auto y = 0; y < scaled.Height; y++) {
				const auto ty = drawY + scaled.Top + y;
				if (ty < 0 || ty >= destHeight)
					continue;
				for (auto x = 0; x < scaled.Width; x++) {
					const auto tx = drawX + scaled.Left + x;
					if (tx >= 0 && tx < destWidth)
						blend(tx, ty, scaled.Alpha[static_cast<size_t>(y) * scaled.Width + x]);
				}
			}
			return true;
		}
	};

	std::shared_ptr<FixedSizeFont::fixed_size_font> AtSize(std::shared_ptr<FixedSizeFont::fixed_size_font> font, float size) {
		if (font->font_size() <= 0 || font->font_size() == size)
			return font;
		return std::make_shared<ScaledFont>(std::move(font), size);
	}

	class SubsetFont final : public FixedSizeFont::default_abstract_fixed_size_font {
		std::shared_ptr<FixedSizeFont::fixed_size_font> m_base;
		std::set<char32_t> m_codepoints;

	public:
		SubsetFont(std::shared_ptr<FixedSizeFont::fixed_size_font> base, std::set<char32_t> codepoints)
			: m_base(std::move(base))
			, m_codepoints(std::move(codepoints)) {}

		[[nodiscard]] std::string family_name() const override { return m_base->family_name(); }
		[[nodiscard]] std::string subfamily_name() const override { return m_base->subfamily_name(); }
		[[nodiscard]] float font_size() const override { return m_base->font_size(); }
		[[nodiscard]] int ascent() const override { return m_base->ascent(); }
		[[nodiscard]] int line_height() const override { return m_base->line_height(); }
		[[nodiscard]] const std::set<char32_t>& all_codepoints() const override { return m_codepoints; }
		[[nodiscard]] const std::map<std::pair<char32_t, char32_t>, int>& all_kerning_pairs() const override { return m_base->all_kerning_pairs(); }
		[[nodiscard]] std::optional<float> get_baseline(uint32_t baselineTag) const override { return m_base->get_baseline(baselineTag); }

		[[nodiscard]] bool try_get_glyph_metrics(char32_t codepoint, FixedSizeFont::glyph_metrics& gm) const override {
			return m_codepoints.contains(codepoint) && m_base->try_get_glyph_metrics(codepoint, gm);
		}

		bool draw(char32_t codepoint, xivres::util::b8g8r8a8* pBuf, int drawX, int drawY, int destWidth, int destHeight, xivres::util::b8g8r8a8 fgColor, xivres::util::b8g8r8a8 bgColor) const override {
			return m_codepoints.contains(codepoint) && m_base->draw(codepoint, pBuf, drawX, drawY, destWidth, destHeight, fgColor, bgColor);
		}

		bool draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override {
			return m_codepoints.contains(codepoint) && m_base->draw(codepoint, pBuf, stride, drawX, drawY, destWidth, destHeight, fgColor, bgColor, fgOpacity, bgOpacity);
		}

		[[nodiscard]] std::shared_ptr<FixedSizeFont::fixed_size_font> get_threadsafe_view() const override {
			return std::make_shared<SubsetFont>(m_base->get_threadsafe_view(), m_codepoints);
		}

		[[nodiscard]] const FixedSizeFont::fixed_size_font* get_base_font(char32_t codepoint) const override {
			return m_codepoints.contains(codepoint) ? m_base->get_base_font(codepoint) : nullptr;
		}
	};

	// FontChanger silently makes an unloadable font (not installed, or an unreadable game font) as one without glyphs; reported here instead.
	void NoteIfEmpty(const FontChanger::Structs::FaceElement& element, const FixedSizeFont::fixed_size_font& base, std::vector<std::string>& failures) {
		if (element.Renderer == FontChanger::Structs::RendererEnum::Empty || !base.all_codepoints().empty())
			return;
		const auto& name = element.Lookup.Name.empty() ? element.RendererSpecific.GlyphImages.Path : element.Lookup.Name;
		auto failure = XivAlexander::Config::Acquire()->Runtime.FormatStringResUtf8(IDS_FONTPREVIEW_ERROR_EMPTYELEMENT, xivres::util::unicode::convert<std::wstring>(name));
		if (std::ranges::find(failures, failure) == failures.end())
			failures.emplace_back(std::move(failure));
	}
}

FontReplacement::FamilyPreview::FamilyPreview(Notify notify)
	: m_notify(std::move(notify)) {
	// FontChanger reads the game's fonts from an installation, here this process's own; nothing else here makes element fonts, so it's safe to set globally.
	static std::once_flag s_gamePathsSet;
	std::call_once(s_gamePathsSet, [] {
		FontChanger::ElementFonts::SetGameInstallationPathsProvider([](xivres::font_type) {
			return std::vector{Utils::Win32::Process::Current().PathOf().parent_path()};
		});
	});
	m_thread = std::thread([this] { ThreadBody(); });
}

FontReplacement::FamilyPreview::~FamilyPreview() {
	{
		const auto lock = std::scoped_lock(m_mutex);
		m_stop = true;
	}
	m_wake.notify_all();
	m_thread.join();
}

uint64_t FontReplacement::FamilyPreview::Request(DrawRequest request, std::chrono::milliseconds delay) {
	uint64_t generation;
	{
		const auto lock = std::scoped_lock(m_mutex);
		// A pending reload carries over to the request replacing it.
		request.Reload |= m_pending && m_pending->Reload;
		m_pending = std::move(request);
		m_due = std::chrono::steady_clock::now() + delay;
		generation = ++m_generation;
		m_done.reset();
	}
	m_wake.notify_all();
	return generation;
}

std::optional<FontReplacement::FamilyPreview::Image> FontReplacement::FamilyPreview::Take(uint64_t generation) {
	const auto lock = std::scoped_lock(m_mutex);
	if (!m_done || m_done->first != generation || generation != m_generation)
		return std::nullopt;
	auto image = std::move(m_done->second);
	m_done.reset();
	return image;
}

void FontReplacement::FamilyPreview::ThreadBody() {
	// The device is this thread's: it goes before the thread does.
	const auto releaseRenderer = xivres::util::on_dtor([this] { m_renderer.reset(); });

	while (true) {
		DrawRequest request;
		uint64_t generation;
		{
			auto lock = std::unique_lock(m_mutex);
			m_wake.wait(lock, [this] { return m_stop || m_pending; });
			if (m_stop)
				return;
			if (std::chrono::steady_clock::now() < m_due) {
				// A later request moves m_due on; wake for it or for the time, then check again.
				m_wake.wait_until(lock, m_due);
				continue;
			}
			request = std::move(*m_pending);
			m_pending.reset();
			generation = m_generation;
		}

		auto image = Draw(request);

		{
			const auto lock = std::scoped_lock(m_mutex);
			// Dropped if another request came while drawing; that one is drawn next.
			if (m_stop || generation != m_generation)
				continue;
			m_done.emplace(generation, std::move(image));
		}
		m_notify(generation);
	}
}

std::shared_ptr<FixedSizeFont::fixed_size_font> FontReplacement::FamilyPreview::MakeFace(const std::string& faceName, float px, std::vector<std::string>& failures, std::set<char32_t>* own) const {
	const auto it = m_faces.find(faceName);
	if (it == m_faces.end() || it->second->Elements.empty())
		return nullptr;

	// Scaled as ReplacementFace does: sizes and pixel values relative to the first element's size.
	const auto& face = *it->second;
	const auto reference = face.Elements[0]->Size > 0 ? face.Elements[0]->Size : 1.f;
	const auto factor = px / reference;
	std::vector<std::pair<std::shared_ptr<FixedSizeFont::fixed_size_font>, FixedSizeFont::codepoint_merge_mode>> elements;
	std::map<char32_t, bool> drawsGame;
	for (const auto& element : face.Elements) {
		FontChanger::Structs::FaceElement scaled = *element;
		scaled.OnFontCreateParametersChange();
		scaled.Scale(factor);
		const auto game = element->Renderer == FontChanger::Structs::RendererEnum::PrerenderedGameInstallation;
		if (game) {
			// The game's glyphs, of the game's font nearest the element's own size, scaled.
			NoteIfEmpty(*element, *element->GetBaseFont(), failures);
			elements.emplace_back(std::make_shared<FixedSizeFont::wrapping_fixed_size_font>(AtSize(element->GetBaseFont(), scaled.Size), scaled.WrapModifiers), element->MergeMode);
		} else {
			NoteIfEmpty(*element, *scaled.GetBaseFont(), failures);
			elements.emplace_back(scaled.GetWrappedFont(), element->MergeMode);
		}

		// Which element draws a codepoint, as merged_fixed_size_font decides it.
		for (const auto c : elements.back().first->all_codepoints()) {
			switch (element->MergeMode) {
				case FixedSizeFont::codepoint_merge_mode::AddNew:
					drawsGame.try_emplace(c, game);
					break;
				case FixedSizeFont::codepoint_merge_mode::Replace:
					if (const auto d = drawsGame.find(c); d != drawsGame.end())
						d->second = game;
					break;
				default:
					drawsGame.insert_or_assign(c, game);
					break;
			}
		}
	}

	if (own) {
		own->clear();
		for (const auto& [c, game] : drawsGame) {
			if (!game)
				own->insert(own->end(), c);
		}
	}
	return std::make_shared<FixedSizeFont::merged_fixed_size_font>(std::move(elements), face.VerticalAlignment);
}

std::shared_ptr<FixedSizeFont::fixed_size_font> FontReplacement::FamilyPreview::MakeFont(std::vector<std::pair<std::string, float>> faces, float px, std::vector<std::string>& failures) const {
	if (faces.empty())
		throw std::runtime_error("The family has no faces.");
	std::ranges::stable_sort(faces, std::ranges::greater{}, [](const auto& f) { return f.second; });

	// The game's glyphs of a face, all of them (a wrapped font has only the codepoints in its ranges), scaled to px.
	const auto gameGlyphs = [&](const std::string& faceName) -> std::shared_ptr<FixedSizeFont::fixed_size_font> {
		const auto game = FontChanger::FaceFromFont::GetGameFontFamilyAndSize(faceName);
		if (!game)
			return nullptr;
		FontChanger::Structs::FaceElement element;
		element.Renderer = FontChanger::Structs::RendererEnum::PrerenderedGameInstallation;
		element.Lookup.Name = std::string(game->first);
		element.Size = game->second;
		element.WrapModifiers.Codepoints = {{0, 0x10FFFF}};
		NoteIfEmpty(element, *element.GetBaseFont(), failures);
		return AtSize(element.GetWrappedFont(), px);
	};

	std::vector<std::pair<std::shared_ptr<FixedSizeFont::fixed_size_font>, FixedSizeFont::codepoint_merge_mode>> fonts;
	if (const auto exact = FontReplacer::GetExactFace(faces, px)) {
		// The face of the size alone, then its game font's glyphs.
		const auto& name = faces[*exact].first;
		if (auto face = MakeFace(name, px, failures, nullptr))
			fonts.emplace_back(std::move(face), FixedSizeFont::codepoint_merge_mode::AddNew);
		if (auto game = gameGlyphs(name))
			fonts.emplace_back(std::move(game), FixedSizeFont::codepoint_merge_mode::AddNew);
	} else {
		// As SizedFont::Pick: faces largest first, own glyphs only, then the largest game font; lines of the face drawing the most itself.
		std::vector<std::shared_ptr<FixedSizeFont::fixed_size_font>> made(faces.size());
		std::vector<std::set<char32_t>> own(faces.size());
		size_t best = 0;
		for (size_t i = 0; i < faces.size(); i++) {
			made[i] = MakeFace(faces[i].first, px, failures, &own[i]);
			if (own[i].size() > own[best].size())
				best = i;
		}

		auto lines = made[best] ? made[best] : gameGlyphs(faces[best].first);
		if (lines)
			fonts.emplace_back(std::make_shared<FixedSizeFont::empty_fixed_size_font>(px, FixedSizeFont::empty_fixed_size_font::create_struct{static_cast<float>(lines->ascent()), static_cast<float>(lines->line_height())}), FixedSizeFont::codepoint_merge_mode::AddNew);
		for (size_t i = 0; i < faces.size(); i++) {
			if (made[i] && !own[i].empty())
				fonts.emplace_back(std::make_shared<SubsetFont>(made[i], std::move(own[i])), FixedSizeFont::codepoint_merge_mode::AddNew);
		}
		if (auto game = gameGlyphs(faces.front().first))
			fonts.emplace_back(std::move(game), FixedSizeFont::codepoint_merge_mode::AddNew);
	}

	if (fonts.empty())
		throw std::runtime_error(XivAlexander::Config::Acquire()->Runtime.FormatStringResUtf8(IDS_FONTPREVIEW_ERROR_NOFACE, xivres::util::unicode::convert<std::wstring>(faces.front().first)));
	return std::make_shared<FixedSizeFont::merged_fixed_size_font>(std::move(fonts));
}

FontReplacement::FamilyPreview::Image FontReplacement::FamilyPreview::Draw(const DrawRequest& request) {
	Image image{.Width = std::max(1, request.Width), .Height = std::max(1, request.Height)};
	image.Pixels.resize(static_cast<size_t>(image.Width) * image.Height);
	const PreviewRenderer::Colors colors{request.Foreground, request.EdgeColor, request.Background};
	std::vector<PreviewRenderer::Glyph> glyphs;
	const auto edge = request.Edge.Clamped();
	const auto px = FontReplacer::GetDrawnPx(request.Size);
	std::vector<std::string> failures;

	try {
		// The faces stay while the sources do, so that only the text is drawn again as it is typed.
		if (request.Reload
			|| !m_facesMadeFor
			|| m_facesMadeFor->Family != request.Family
			|| m_facesMadeFor->Settings != request.Settings
			|| m_facesMadeFor->PresetFolder != request.PresetFolder) {
			m_facesMadeFor.reset();
			m_font.reset();
			m_faceFailures.clear();
			PresetController::PresetReader read(request.PresetFolder, m_faceFailures);
			m_faces = PresetController::MakeFamilyFaces(request.Family, request.Settings, read, m_faceFailures);
			m_facesMadeFor = request;
		}
		failures = m_faceFailures;

		if (!m_font || m_fontFaces != request.Faces || m_fontSize != px) {
			m_font.reset();
			m_fontFailures.clear();
			m_font = MakeFont(request.Faces, px, m_fontFailures);
			m_fontFaces = request.Faces;
			m_fontSize = px;
		}
		failures.insert(failures.end(), m_fontFailures.begin(), m_fontFailures.end());

		// Each glyph with the empty pixels around it that the replacement gives it for the edge (FinishGlyph).
		const auto margin = FontReplacer::GetEdgeMargin(edge, px);
		const auto text = xivres::util::unicode::convert<std::u32string>(request.Text);
		if (!text.empty()) {
			const auto measured = FixedSizeFont::text_measurer(*m_font)
				.max_width(std::max(1, image.Width - Padding * 2))
				.measure(text);
			for (const auto& c : measured.Characters) {
				const auto& m = c.Metrics;
				if (m.is_effectively_empty())
					continue;
				PreviewRenderer::Glyph glyph{
					.X = Padding + m.X1 - margin,
					.Y = Padding + m.Y1 - margin,
					.Width = m.width() + margin * 2,
					.Height = m.height() + margin * 2,
				};
				if (glyph.Y >= image.Height || glyph.X >= image.Width || glyph.X + glyph.Width <= 0 || glyph.Y + glyph.Height <= 0)
					continue;
				glyph.Alpha.resize(static_cast<size_t>(glyph.Width) * glyph.Height);
				m_font->draw(c.Displayed, glyph.Alpha.data(), 1, margin + c.X - m.X1, margin + c.Y - m.Y1, glyph.Width, glyph.Height, 255, 0, 255, 0);
				glyphs.emplace_back(std::move(glyph));
			}

			// Text with ink but none drawn: the face has none of its characters, rather than a blank image saying nothing.
			const auto hasInk = std::ranges::any_of(text, [](char32_t c) {
				return c > U' ' && !(c >= 0x7F && c <= 0xA0) && !(c >= 0x2000 && c <= 0x200F) && c != 0x3000;
			});
			const auto drawnInk = std::ranges::any_of(glyphs, [](const auto& g) { return std::ranges::any_of(g.Alpha, [](uint8_t a) { return a != 0; }); });
			if (hasInk && !drawnInk)
				failures.push_back(XivAlexander::Config::Acquire()->Runtime.FormatStringResUtf8(IDS_FONTPREVIEW_ERROR_NOGLYPHS, xivres::util::unicode::convert<std::wstring>(request.Family)));
		}
	} catch (const std::exception& e) {
		failures.emplace_back(e.what());
	}
	for (const auto& failure : failures)
		image.Failures += (image.Failures.empty() ? "" : "; ") + failure;

	// The edge, if the device can be made; it is tried once.
	if (!m_renderer && !m_rendererFailure) {
		try {
			m_renderer = std::make_unique<PreviewRenderer>();
		} catch (const std::exception& e) {
			m_rendererFailure = e.what();
		}
	}
	if (m_renderer) {
		try {
			// The radius the edge shader gets in the game: the atlas's size over the texture width the font claims.
			const auto atlasSize = FontReplacer::GetAtlasSize(request.Family);
			const auto radius = static_cast<float>(atlasSize) / FontReplacer::GetClaimedTextureWidth(edge, atlasSize, px);
			m_renderer->Draw(glyphs, radius, edge.Softness, colors, image.Width, image.Height, image.Pixels);
			return image;
		} catch (const std::exception& e) {
			image.EdgeFailure = e.what();
		}
	} else {
		image.EdgeFailure = *m_rendererFailure;
	}
	PreviewRenderer::DrawWithoutEdge(glyphs, colors, image.Width, image.Height, image.Pixels);
	return image;
}
