#include "pch.h"
#include "MainApp/Windows/MainWindow.h"

#include "resource.h"
#include "XivAlexander.h"
#include "MainApp/Windows/ConfigWindow.h"
#include "MainApp/Windows/Dialog/FramerateLockingDialog.h"
#include "Misc/GameInstallationDetector.h"
#include "Misc/Logger.h"
#include "Utils/WinHttp.h"

namespace {
	enum class RemoteConfigUpdateResult {
		NotFound,
		NotChanged,
		Changed,
	};

	RemoteConfigUpdateResult UpdateConfigFromRemote(XivAlexander::BaseConfigRepository& repository, const std::string& url) {
		std::string prev;
		try {
			const auto prevFile = Utils::Win32::Handle::FromCreateFile(repository.GetConfigPath(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING);
			prev.resize(prevFile.GetFileSize());
			prevFile.Read(0, prev.data(), prev.size());
			prev = nlohmann::json::parse(prev).dump();
		} catch (...) {
			prev.clear();
		}

		std::string updated;
		{
			const auto response = Utils::Win32::WinHttp::Get(url);

			switch (static_cast<int>(response.StatusCode)) {
				case 200:
					updated = nlohmann::json::parse(response.Body).dump();
					break;

				case 404:
					return RemoteConfigUpdateResult::NotFound;

				default:
					throw std::runtime_error(XivAlexander::Config::Acquire()->Runtime.FormatStringResUtf8(IDS_ERROR_HTTP, response.StatusCode));
			}
		}

		if (updated == prev)
			return RemoteConfigUpdateResult::NotChanged;

		std::ofstream(repository.GetConfigPath()) << updated;
		repository.Reload();
		return RemoteConfigUpdateResult::Changed;
	}

	size_t UpdatePatchCodesFromRemote(const std::filesystem::path& directory) {
		const auto listing = Utils::Win32::WinHttp::Get("https://api.github.com/repos/Soreepeong/XivAlexander/contents/StaticData/PatchCode?ref=main");
		if (listing.StatusCode != 200)
			throw std::runtime_error(XivAlexander::Config::Acquire()->Runtime.FormatStringResUtf8(IDS_ERROR_HTTP, listing.StatusCode));

		size_t changed = 0;
		for (const auto& item : nlohmann::json::parse(listing.Body)) {
			const auto name = item.value("name", "");
			if (item.value("type", "") != "file" || !name.ends_with(".json") || name.find_first_of("/\\:") != std::string::npos)
				continue;

			const auto response = Utils::Win32::WinHttp::Get(item.at("download_url").get<std::string>());
			if (response.StatusCode != 200)
				throw std::runtime_error(std::format("{}: {}", name, XivAlexander::Config::Acquire()->Runtime.FormatStringResUtf8(IDS_ERROR_HTTP, response.StatusCode)));
			const auto updated = nlohmann::json::parse(response.Body);

			const auto path = directory / xivres::util::unicode::convert<std::wstring>(name);
			try {
				if (Utils::ParseJsonFromFile(path) == updated)
					continue;
			} catch (...) {
				// missing or broken; overwrite
			}
			Utils::SaveJsonToFile(path, updated);
			changed++;
		}
		return changed;
	}
}

void XivAlexander::Apps::MainApp::Window::MainWindow::OnCommand_Menu_Configure(int menuId) {
	auto& config = m_config->Runtime;

	switch (menuId) {
		case ID_CONFIGURE_EDITRUNTIMECONFIGURATION:
			if (m_runtimeConfigEditor && !m_runtimeConfigEditor->IsDestroyed())
				SetForegroundWindow(m_runtimeConfigEditor->Handle());
			else {
				m_runtimeConfigEditor.reset();
				m_runtimeConfigEditor = std::make_unique<ConfigWindow>(IDS_WINDOW_RUNTIME_CONFIG_EDITOR, &m_config->Runtime);
			}
			return;

		case ID_CONFIGURE_EDITOPCODECONFIGURATION:
			if (m_gameConfigEditor && !m_gameConfigEditor->IsDestroyed())
				SetForegroundWindow(m_gameConfigEditor->Handle());
			else {
				m_gameConfigEditor.reset();
				m_gameConfigEditor = std::make_unique<ConfigWindow>(IDS_WINDOW_OPCODE_CONFIG_EDITOR, &m_config->Game);
			}
			return;

		case ID_CONFIGURE_GAMEFIX_OPENDIRECTORY:
			EnsureAndOpenDirectory(m_config->PatchCode.GetDirectory());
			return;

		case ID_CONFIGURE_CHECKFORUPDATEDOPCODES:
			CheckUpdatedOpcodes(true);
			return;

		case ID_CONFIGURE_CHECKFORUPDATEDOPCODESONSTARTUP:
			config.Opcodes.CheckForUpdatesOnStartup.Toggle();
			return;

		case ID_CONFIGURE_USEMORECPUTIME:
			config.FramerateControl.UseMoreCpuTime.Toggle();
			return;

		case ID_CONFIGURE_BACKGROUND_FRAMERATE_LIMIT:
			config.FramerateControl.UseBackgroundLimit.Toggle();
			return;

		case ID_CONFIGURE_LOCKFRAMERATE:
			m_cleanupFramerateLockDialog = Dialog::FramerateLockingDialog::Show(m_app, m_hWnd);
			return;

		case ID_CONFIGURE_SYNCHRONIZEPROCESSING:
			config.FramerateControl.SynchronizeProcessing.Toggle();
			return;

		case ID_CONFIGURE_WINDOWTITLE_PID_NONE:
			config.GameWindow.TitleMode = GameWindowTitleMode::None;
			return;

		case ID_CONFIGURE_WINDOWTITLE_PID_PREFIX:
			config.GameWindow.TitleMode = GameWindowTitleMode::Prefix;
			return;

		case ID_CONFIGURE_WINDOWTITLE_PID_SUFFIX:
			config.GameWindow.TitleMode = GameWindowTitleMode::Suffix;
			return;

		case ID_CONFIGURE_LANGUAGE_SYSTEMDEFAULT:
			config.Ui.Language = Language::SystemDefault;
			return;

		case ID_CONFIGURE_LANGUAGE_ENGLISH:
			config.Ui.Language = Language::English;
			return;

		case ID_CONFIGURE_LANGUAGE_KOREAN:
			config.Ui.Language = Language::Korean;
			return;

		case ID_CONFIGURE_LANGUAGE_JAPANESE:
			config.Ui.Language = Language::Japanese;
			return;

		case ID_CONFIGURE_THEME_SYSTEM:
			config.Ui.ThemeMode = ThemeMode::System;
			return;

		case ID_CONFIGURE_THEME_LIGHT:
			config.Ui.ThemeMode = ThemeMode::Light;
			return;

		case ID_CONFIGURE_THEME_DARK:
			config.Ui.ThemeMode = ThemeMode::Dark;
			return;

		case ID_CONFIGURE_OPENCONFIGURATIONDIRECTORY:
			EnsureAndOpenDirectory(m_config->Init.ResolveConfigStorageDirectoryPath());
			return;

		case ID_CONFIGURE_RELOAD:
			m_config->Reload();
			return;
	}
}

void XivAlexander::Apps::MainApp::Window::MainWindow::CheckUpdatedOpcodes(bool showResultMessageBox) {
	void(Utils::Win32::Thread(L"CheckUpdatedOpcodes", [this, showResultMessageBox] {
		const auto releaseInfo = Misc::GameInstallationDetector::GetGameReleaseInfo();

		auto opcodes = RemoteConfigUpdateResult::NotFound;
		size_t patchCodesChanged = 0;
		std::string error;

		try {
			opcodes = UpdateConfigFromRemote(m_config->Game, std::format("https://raw.githubusercontent.com/Soreepeong/XivAlexander/main/StaticData/OpcodeDefinition/game.{}.{}.json", releaseInfo.CountryCode, releaseInfo.GameVersion));
			switch (opcodes) {
				case RemoteConfigUpdateResult::NotFound:
					m_logger->Log(LogCategory::General, "No updates to opcodes yet.");
					break;
				case RemoteConfigUpdateResult::NotChanged:
					m_logger->Log(LogCategory::General, "No updates to opcodes.");
					break;
				case RemoteConfigUpdateResult::Changed:
					m_logger->Log(LogCategory::General, "Opcodes updated.");
					break;
			}
		} catch (const std::exception& e) {
			m_logger->Format<LogLevel::Error>(LogCategory::General, "Opcode update check failed: {}", e.what());
			error = e.what();
		}

		try {
			patchCodesChanged = UpdatePatchCodesFromRemote(m_config->PatchCode.GetDirectory());
			if (patchCodesChanged) {
				m_config->PatchCode.Reload();
				m_logger->Format(LogCategory::General, "Patch codes updated ({} files).", patchCodesChanged);
			} else {
				m_logger->Log(LogCategory::General, "No updates to patch codes.");
			}
		} catch (const std::exception& e) {
			m_logger->Format<LogLevel::Error>(LogCategory::General, "Patch code update check failed: {}", e.what());
			if (error.empty())
				error = e.what();
		}

		if (!showResultMessageBox)
			return;

		if (!error.empty())
			Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, error);
		else if (opcodes == RemoteConfigUpdateResult::Changed || patchCodesChanged)
			Dll::MessageBoxF(m_hWnd, MB_OK, m_config->Runtime.FormatStringRes(IDS_OPCODEUPDATE_OK_CHANGED));
		else if (opcodes == RemoteConfigUpdateResult::NotFound)
			Dll::MessageBoxF(m_hWnd, MB_OK, m_config->Runtime.FormatStringRes(IDS_OPCODEUPDATE_ERROR_404));
		else
			Dll::MessageBoxF(m_hWnd, MB_OK, m_config->Runtime.FormatStringRes(IDS_OPCODEUPDATE_OK_NOTCHANGED));
	}));
}
