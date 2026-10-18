#include "pch.h"
#include "MainApp/FontReplacement/PluginInterface/PresetController.h"

#include "Config.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::PresetController::PresetController(std::shared_ptr<Config> config, Apply apply)
	: m_config(std::move(config))
	, m_apply(std::move(apply))
	, m_presetFolder(Presets::Folder(*m_config))
	, m_thread([this] { ThreadBody(); }) {
	Reload();
}

FontReplacement::PresetController::~PresetController() {
	{
		const auto lock = std::scoped_lock(m_mutex);
		m_stop = true;
	}
	m_wake.notify_all();
	m_thread.join();
}

std::string FontReplacement::PresetController::Status() {
	const auto lock = std::scoped_lock(m_mutex);
	return m_status;
}

void FontReplacement::PresetController::Reload() {
	Schedule(false, std::chrono::milliseconds(0));
}
