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
	// Between the text and the edges of the image.
	constexpr int Padding = 8;

	// A font of the game's glyphs drawn at another size: the game has its fonts at a few sizes only, which the replacement
	// draws at any size by scaling a glyph's pixels (GameTextureSource::Scale, RasterGlyph::Scaled); so does this, about the
	// pen at the top of the line. Metrics scale alike, rounded to whole pixels.
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

		// Draws a glyph's coverage scaled, with the pen at (drawX, drawY), through blend(x, y, coverage) for each pixel in the
		// destination.
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

	// Gets a font at a size: itself if it is of the size, else it scaled.
	std::shared_ptr<FixedSizeFont::fixed_size_font> AtSize(std::shared_ptr<FixedSizeFont::fixed_size_font> font, float size) {
		if (font->font_size() <= 0 || font->font_size() == size)
			return font;
		return std::make_shared<ScaledFont>(std::move(font), size);
	}
}

FontReplacement::FamilyPreview::FamilyPreview(Notify notify)
	: m_notify(std::move(notify)) {
	// FontChanger draws elements of the game's fonts from an installation, which is this process's: the game's folder has
	// its sqpack. Nothing else here makes FontChanger's element fonts, so setting it for the process changes nothing else.
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
		// A request that would make the faces anew still does when a later one replaces it before it is drawn.
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
				// A later request moves the time on; this wakes for it, or for the time.
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

std::shared_ptr<FixedSizeFont::fixed_size_font> FontReplacement::FamilyPreview::MakeFont(const std::string& faceName, float px, std::vector<std::string>& failures) const {
	std::vector<std::pair<std::shared_ptr<FixedSizeFont::fixed_size_font>, FixedSizeFont::codepoint_merge_mode>> fonts;

	// FontChanger makes a font that can't be (one not installed, or the game's that can't be read) as one without glyphs,
	// and the replacement draws its element so too, silently; here the failure line says so instead.
	const auto noteIfEmpty = [&failures](const FontChanger::Structs::FaceElement& element, const FixedSizeFont::fixed_size_font& base) {
		if (element.Renderer == FontChanger::Structs::RendererEnum::Empty || !base.all_codepoints().empty())
			return;
		const auto& name = element.Lookup.Name.empty() ? element.RendererSpecific.GlyphImages.Path : element.Lookup.Name;
		auto failure = XivAlexander::Config::Acquire()->Runtime.FormatStringResUtf8(IDS_FONTPREVIEW_ERROR_EMPTYELEMENT, xivres::util::unicode::convert<std::wstring>(name));
		if (std::ranges::find(failures, failure) == failures.end())
			failures.emplace_back(std::move(failure));
	};

	// The face, scaled as ReplacementFace scales it: its sizes and pixel values relative to its first element's size.
	if (const auto it = m_faces.find(faceName); it != m_faces.end() && !it->second->Elements.empty()) {
		const auto& face = *it->second;
		const auto reference = face.Elements[0]->Size > 0 ? face.Elements[0]->Size : 1.f;
		const auto factor = px / reference;
		std::vector<std::pair<std::shared_ptr<FixedSizeFont::fixed_size_font>, FixedSizeFont::codepoint_merge_mode>> elements;
		for (const auto& element : face.Elements) {
			FontChanger::Structs::FaceElement scaled = *element;
			scaled.OnFontCreateParametersChange();
			scaled.Scale(factor);
			if (element->Renderer == FontChanger::Structs::RendererEnum::PrerenderedGameInstallation) {
				// The game's glyphs, of the game's font nearest the element's own size, scaled.
				noteIfEmpty(*element, *element->GetBaseFont());
				elements.emplace_back(std::make_shared<FixedSizeFont::wrapping_fixed_size_font>(AtSize(element->GetBaseFont(), scaled.Size), scaled.WrapModifiers), element->MergeMode);
			} else {
				noteIfEmpty(*element, *scaled.GetBaseFont());
				elements.emplace_back(scaled.GetWrappedFont(), element->MergeMode);
			}
		}
		fonts.emplace_back(std::make_shared<FixedSizeFont::merged_fixed_size_font>(std::move(elements), face.VerticalAlignment), FixedSizeFont::codepoint_merge_mode::AddNew);
	}

	// After it, the game's glyphs, as the replacer falls back to them. A wrapped font has only the codepoints in its
	// ranges: the element takes every one.
	if (const auto game = FontChanger::FaceFromFont::GetGameFontFamilyAndSize(faceName)) {
		FontChanger::Structs::FaceElement element;
		element.Renderer = FontChanger::Structs::RendererEnum::PrerenderedGameInstallation;
		element.Lookup.Name = std::string(game->first);
		element.Size = game->second;
		element.WrapModifiers.Codepoints = {{0, 0x10FFFF}};
		noteIfEmpty(element, *element.GetBaseFont());
		fonts.emplace_back(AtSize(element.GetWrappedFont(), px), FixedSizeFont::codepoint_merge_mode::AddNew);
	}
	if (fonts.empty())
		throw std::runtime_error(XivAlexander::Config::Acquire()->Runtime.FormatStringResUtf8(IDS_FONTPREVIEW_ERROR_NOFACE, xivres::util::unicode::convert<std::wstring>(faceName)));
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

		if (!m_font || m_fontFace != request.FaceName || m_fontSize != px) {
			m_font.reset();
			m_fontFailures.clear();
			m_font = MakeFont(request.FaceName, px, m_fontFailures);
			m_fontFace = request.FaceName;
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
				// Lines below the image aren't drawn.
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
				failures.push_back(XivAlexander::Config::Acquire()->Runtime.FormatStringResUtf8(IDS_FONTPREVIEW_ERROR_NOGLYPHS, xivres::util::unicode::convert<std::wstring>(request.FaceName)));
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
			m_renderer->Draw(glyphs, radius, colors, image.Width, image.Height, image.Pixels);
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
