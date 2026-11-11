#include "pch.h"
#include "MainApp/FontReplacement/Fonts/FontLoader.h"

#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFont.h"
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

void FontReplacement::FontLoader::Request(FontSettings value) {
	// Freed after the lock.
	std::unique_ptr<FontSet> superseded;
	{
		const auto lock = std::scoped_lock(m_mutex);
		m_request = std::move(value);
		++m_serial;
		superseded = std::move(m_ready);
		m_error.reset();
	}
	m_wake.notify_all();
	if (!m_thread.joinable())
		m_thread = std::thread([this] { ThreadBody(); });
}

FontReplacement::FontLoader::Taken FontReplacement::FontLoader::TryTake() {
	const auto lock = std::scoped_lock(m_mutex);
	return {std::move(m_ready), std::exchange(m_error, std::nullopt)};
}

void FontReplacement::FontLoader::ThreadBody() {
	while (true) {
		FontSettings request;
		uint64_t serial;
		{
			auto lock = std::unique_lock(m_mutex);
			m_wake.wait(lock, [this] { return m_stopped || m_request; });
			if (m_stopped)
				return;
			request = std::move(*m_request);
			m_request.reset();
			serial = m_serial;
		}

		std::unique_ptr<FontSet> made;
		std::optional<std::string> error;
		try {
			made = Build(m_services, m_fallbackFonts, request, [this, serial] { return m_stopped || m_serial != serial; });
		} catch (const std::exception& e) {
			Host::Error("Setting up the fonts failed; the earlier ones stay: {}", e.what());
			error = e.what();
		}

		// Swapped, so that stale fonts are freed after the lock.
		const auto lock = std::scoped_lock(m_mutex);
		if (!m_stopped && m_serial == serial) {
			std::swap(m_ready, made);
			m_error = std::move(error);
		}
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
