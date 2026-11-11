#include "pch.h"
#include "MainApp/FontReplacement/Fonts/FontLoader.h"

#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFont.h"
#include "MainApp/FontReplacement/Fonts/PresetCompiler.h"
#include "MainApp/FontReplacement/Host.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::FontLoader::~FontLoader() {
	{
		const auto lock = std::scoped_lock(m_mutex);
		m_stopped = true;
	}
	m_wake.notify_all();
	if (m_thread.joinable())
		m_thread.join();
	m_ready.reset();
}

FontReplacement::FontLoader::Taken FontReplacement::FontLoader::Take(std::optional<FontRequest> request, bool keepOnFailure) {
	Taken taken;
	{
		const auto lock = std::scoped_lock(m_mutex);
		taken = {std::move(m_ready), std::exchange(m_error, std::nullopt), std::exchange(m_report, std::nullopt)};
		if (!request)
			return taken;

		keepOnFailure &= !m_mustApply;
		m_mustApply = !keepOnFailure;
		m_request = Pending{std::move(*request), keepOnFailure, ++m_serial};
	}
	m_wake.notify_all();
	if (!m_thread.joinable())
		m_thread = std::thread([this] { ThreadBody(); });
	return taken;
}

void FontReplacement::FontLoader::ThreadBody() {
	while (true) {
		Pending pending;
		{
			auto lock = std::unique_lock(m_mutex);
			m_wake.wait(lock, [this] { return m_stopped || m_request; });
			if (m_stopped)
				return;
			pending = std::move(*m_request);
			m_request.reset();
			m_mustApply = false;
		}

		Load(pending.Request, pending.KeepOnFailure, pending.Serial);
	}
}

void FontReplacement::FontLoader::Load(const FontRequest& value, bool keepOnFailure, uint64_t serial) {
	std::optional<FontSettings> settings;
	LoadReport report;
	try {
		std::tie(settings, report) = PresetCompiler::Compile(value, keepOnFailure);
	} catch (const std::exception& e) {
		Host::Error("Loading the presets failed: {}", e.what());
		report = {std::format("Loading failed ({})", e.what()), std::nullopt};
	}

	// Before the fonts are made, which takes longer.
	{
		const auto lock = std::scoped_lock(m_mutex);
		if (settings) {
			m_unbuilt = Unbuilt{*settings, report.Watch};
		} else if (m_unbuilt) {
			settings = m_unbuilt->Settings;
			report.Watch = m_unbuilt->Watch;
		}

		if (!m_stopped && m_serial == serial)
			m_report = report;
	}

	// Declared before the lock, so that stale fonts are freed after it.
	std::unique_ptr<FontSet> made;
	std::optional<std::string> error;
	if (settings) {
		try {
			made = Build(m_services, m_fallbackFonts, *settings, [this, serial] { return m_stopped || m_serial != serial; });
		} catch (const std::exception& e) {
			Host::Error("Setting up the fonts failed; the earlier ones stay: {}", e.what());
			error = e.what();
		}
	}

	const auto lock = std::scoped_lock(m_mutex);
	const auto current = !m_stopped && m_serial == serial;

	// Settings whose fonts can't be made aren't tried again for a later request.
	if ((current && made) || error)
		m_unbuilt.reset();
	if (current) {
		std::swap(m_ready, made);
		m_error = std::move(error);
	}
}

std::unique_ptr<FontReplacement::FontSet> FontReplacement::FontLoader::Build(
	FontServices& services, SystemFallbackFonts& fallbackFonts, const FontSettings& value, const std::function<bool()>& stale) {
	auto set = FontSet::CreateGame(services, value.SystemFallback, fallbackFonts);
	set->Game().Prepare();
	for (const auto& [name, def] : value.Preset) {
		if (stale())
			return nullptr;
		try {
			auto font = std::make_unique<MaterializedFont>(services, def, value.SystemFallback, fallbackFonts, set->Sources());
			font->Prepare();
			set->Preset().emplace(name, std::move(font));
		} catch (const std::exception& e) {
			Host::Warning("The font {} can't be set up; its text falls back: {}", name, e.what());
		}
	}
	return stale() ? nullptr : std::move(set);
}
