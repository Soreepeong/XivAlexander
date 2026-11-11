#include "pch.h"
#include "MainApp/FontReplacement/PluginInterface/FrameCoordinator.h"

#include "MainApp/FontReplacement/Fonts/FontLoader.h"
#include "MainApp/FontReplacement/Fonts/FontReplacer.h"
#include "MainApp/FontReplacement/Game/LineBreaker.h"
#include "MainApp/FontReplacement/Game/NamePlateText.h"
#include "MainApp/FontReplacement/Host.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	template<typename T>
	std::unique_ptr<T> Optional(const char* part, FontReplacement::FontReplacer& replacer) {
		try {
			return std::make_unique<T>(replacer);
		} catch (const std::exception& e) {
			FontReplacement::Host::Warning("{} is off: {}", part, e.what());
			return nullptr;
		}
	}

	template<typename T>
	void Step(const char* name, T&& step) {
		try {
			step();
		} catch (const std::exception& e) {
			FontReplacement::Host::Error("Undoing the {} failed: {}", name, e.what());
		}
	}
}

FontReplacement::FrameCoordinator::FrameCoordinator(const Settings& settings, HWND window)
	: m_replacer(std::make_unique<FontReplacer>(m_state)) {
	try {
		m_namePlates = Optional<NamePlateText>("Nameplate text", *m_replacer);
		m_lineBreaker = Optional<LineBreaker>("Line breaking", *m_replacer);
		m_replacer->SetNamePlateTest([this](uintptr_t node) { return m_namePlates && m_namePlates->IsPlateText(node); });
		m_loader = std::make_unique<FontLoader>(m_replacer->Services(), m_replacer->FallbackFonts());
		OnUpdate(settings, window);
	} catch (...) {
		Unhook();
		throw;
	}
}

FontReplacement::FrameCoordinator::~FrameCoordinator() {
	Unhook();
	Step("font replacer", [this] { m_replacer.reset(); });
}

void FontReplacement::FrameCoordinator::OnUpdate(const Settings& settings, HWND window) {
	const auto log = m_state.Drain();
	auto [fonts, error] = m_loader->TryTake();

	// Every cause is known before anything is made again, so that it is made once.
	auto scope = m_replacer->SetEnabled(!m_state.Failed(Part::Replacer));
	m_edge.Apply(settings.Edge);
	scope |= m_replacer->SetEdge(settings.Edge);

	const auto subpixel = m_state.Failed(Part::Subpixel) ? SubpixelShaders::SettingsRecord{} : m_subpixelSource.Get(settings.Subpixel, settings.ClearType, window);
	const auto active = m_subpixel.TryApply(subpixel);
	scope |= m_replacer->SetSubpixel(active ? subpixel.Order : SubpixelOrder::None, active);
	scope |= m_replacer->SetVerticalSubpixel(settings.VerticalSubpixel);
	scope |= m_replacer->SetAtlas(settings.Atlas);
	if (log.GameFontsChanged)
		scope |= ResetScope::Text;
	const auto fontsChanged = fonts != nullptr;
	m_replacer->Reset(scope, std::move(fonts));

	if (Includes(scope, ResetScope::Text) || fontsChanged)
		RedoText();
	else if (log.PlaneReused && m_namePlates)
		m_namePlates->Rebake();

	if (m_namePlates)
		m_namePlates->SetMode(settings.NamePlates);
}

void FontReplacement::FrameCoordinator::OnPresent() {
	m_replacer->Present();

	// This frame's UI draw commands are processed by now.
	m_subpixel.ForgetDrawState();
	m_state.EndFrame();
}

void FontReplacement::FrameCoordinator::RedoText() {
	if (m_namePlates)
		m_namePlates->Rebake();
}

void FontReplacement::FrameCoordinator::Unhook() {
	// Laid out with the game's fonts while the hooks that know the plates' bake scales are still there.
	Step("text", [this] {
		if (m_replacer)
			m_replacer->SetEnabled(false);
		RedoText();
	});
	Step("line breaking", [this] { m_lineBreaker.reset(); });
	Step("nameplate text", [this] { m_namePlates.reset(); });
	Step("edge shader", [this] { m_edge.Apply({}); });
	Step("subpixel shaders", [this] { m_subpixel.TryApply({}); });
	Step("hooks", [this] {
		if (m_replacer)
			m_replacer->Unhook();
	});
	Step("font loader", [this] { m_loader.reset(); });
}
