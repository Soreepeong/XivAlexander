#include "pch.h"
#include "MainApp/Windows/MainWindow.h"
#include "MainApp/Windows/MainWindow.Internal.h"

#include "Game/CommandLine.h"
#include "Utils/Win32/Resource.h"
#include "Utils/Win32/TaskDialogBuilder.h"

#include "resource.h"
#include "XivAlexander.h"
#include "MainApp/App.h"
#include "MainApp/Features/LoginSessions.h"

std::filesystem::path XivAlexander::Apps::MainApp::Window::MainWindow::GameExecutablePath() {
	return Utils::Win32::Process::Current().PathOf().parent_path() / Dll::GameExecutable64NameW;
}

bool XivAlexander::Apps::MainApp::Window::MainWindow::WarnIfLaunchParametersMissing() {
	// Launching without them starts a game that has no session to log in with.
	if (!m_launchParameters.empty())
		return true;
	Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONWARNING, m_config->Runtime.FormatStringRes(IDS_WARNING_GAME_PARAMETER_PARSE, L"-"));
	return false;
}

std::wstring XivAlexander::Apps::MainApp::Window::MainWindow::MakeLaunchArguments() const {
	auto params{ m_launchParameters };
	if (const auto loginSessions = m_app.GetLoginSessions())
		loginSessions->ApplySelectedTo(params);
	if (Dll::IsLanguageRegionModifiable()) {
		Game::CommandLine::WellKnown::SetLanguage(params, m_gameLanguage);
		Game::CommandLine::WellKnown::SetRegion(params, m_gameRegion);
	}
	return Game::CommandLine::ToString(params, m_bUseParameterObfuscation);
}

void XivAlexander::Apps::MainApp::Window::MainWindow::CopyLaunchCommandLine() {
	if (!WarnIfLaunchParametersMissing())
		return;

	const auto text = std::format(L"{} {}", Utils::Win32::ReverseCommandLineToArgv(GameExecutablePath().wstring()), MakeLaunchArguments());

	const auto allocGlobal = [](const void* data, size_t bytes) {
		const auto hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
		if (!hMem)
			throw Utils::Win32::Error("GlobalAlloc");
		if (const auto p = GlobalLock(hMem)) {
			memcpy(p, data, bytes);
			GlobalUnlock(hMem);
		} else {
			const auto error = Utils::Win32::Error("GlobalLock");
			GlobalFree(hMem);
			throw error;
		}
		return hMem;
	};

	const auto hMem = allocGlobal(text.c_str(), (text.size() + 1) * sizeof(wchar_t));
	if (!OpenClipboard(m_hWnd)) {
		const auto error = Utils::Win32::Error("OpenClipboard");
		GlobalFree(hMem);
		throw error;
	}
	EmptyClipboard();
	if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
		const auto error = Utils::Win32::Error("SetClipboardData");
		CloseClipboard();
		GlobalFree(hMem);
		throw error;
	}

	// treat as password like what KeePass 2.x does
	const auto setMarker = [&allocGlobal](const wchar_t* formatName, const void* data, size_t bytes) {
		const auto format = RegisterClipboardFormatW(formatName);
		if (!format)
			return;
		try {
			const auto hMarker = allocGlobal(data, bytes);
			if (!SetClipboardData(format, hMarker))
				GlobalFree(hMarker);
		} catch (...) {
		}
	};
	static constexpr wchar_t ViewerIgnoreValue[] = L"XivAlexander";
	static constexpr DWORD True = 1, False = 0;
	setMarker(L"Clipboard Viewer Ignore", ViewerIgnoreValue, sizeof ViewerIgnoreValue);
	setMarker(L"ExcludeClipboardContentFromMonitorProcessing", &True, sizeof True);
	setMarker(L"CanUploadToCloudClipboard", &False, sizeof False);
	setMarker(L"CanIncludeInClipboardHistory", &False, sizeof False);
	CloseClipboard();

	if (const auto clearSeconds = m_config->Runtime.Launch.ClearCopiedCommandLineSeconds.Value()) {
		m_copiedLaunchCommandLineClipboardSequence = GetClipboardSequenceNumber();
		SetTimer(m_hWnd, TimerIdClearCopiedLaunchCommandLine, clearSeconds * 1000, nullptr);
	} else {
		m_copiedLaunchCommandLineClipboardSequence = 0;
		KillTimer(m_hWnd, TimerIdClearCopiedLaunchCommandLine);
	}
}

void XivAlexander::Apps::MainApp::Window::MainWindow::ClearCopiedLaunchCommandLine() {
	if (!m_copiedLaunchCommandLineClipboardSequence)
		return;

	if (GetClipboardSequenceNumber() == m_copiedLaunchCommandLineClipboardSequence) {
		if (!OpenClipboard(m_hWnd)) {
			SetTimer(m_hWnd, TimerIdClearCopiedLaunchCommandLine, 1000, nullptr);
			return;
		}
		EmptyClipboard();
		CloseClipboard();
	}

	m_copiedLaunchCommandLineClipboardSequence = 0;
	KillTimer(m_hWnd, TimerIdClearCopiedLaunchCommandLine);
}

void XivAlexander::Apps::MainApp::Window::MainWindow::AskRestartGame(bool onlyOnModifier) {
	if (onlyOnModifier && !((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_SHIFT) & 0x8000))) {
		return;
	}
	if (!WarnIfLaunchParametersMissing())
		return;

	const auto yes = Utils::Win32::MB_GetString(IDYES - 1);
	const auto no = Utils::Win32::MB_GetString(IDNO - 1);
	auto content = m_config->Runtime.FormatStringRes(
		IDS_CONFIRM_RESTART_GAME,
		m_bUseXivAlexander ? yes : no,
		m_bUseParameterObfuscation ? yes : no,
		m_bUseElevation ? yes : no,
		m_config->Runtime.GetLanguageNameLocalized(m_gameLanguage),
		m_config->Runtime.GetRegionNameLocalized(m_gameRegion));
	if (const auto loginSessions = m_app.GetLoginSessions()) {
		const auto sessions = loginSessions->GetSessions();
		if (const auto selected = loginSessions->GetSelectedIndex(); selected < sessions.size()) {
			content += m_config->Runtime.FormatStringRes(IDS_CONFIRM_RESTART_GAME_LOGINSESSION, sessions[selected].Alias.empty()
				? std::wstring(m_config->Runtime.GetStringRes(IDS_MENU_LOGINSESSION_LAUNCHARGUMENTS))
				: xivres::util::unicode::convert<std::wstring>(sessions[selected].Alias));
		}
	}

	static constexpr int IdRestart = 1001;
	static constexpr int IdNewInstance = 1002;
	const auto choice = Utils::Win32::TaskDialog::Builder()
		.WithWindowTitle(Dll::GetGenericMessageBoxTitle())
		.WithParentWindow(m_hWnd)
		.WithInstance(Dll::Module())
		.WithAllowDialogCancellation()
		.WithMainIcon(IDI_TRAY_ICON)
		.WithMainInstruction(std::wstring(m_config->Runtime.GetStringRes(IDS_CONFIRM_RESTART_GAME_TITLE)))
		.WithContent(content)
		.WithButton({.IdSet = true, .Id = IdRestart, .Text = std::wstring(m_config->Runtime.GetStringRes(IDS_RESTART_GAME_RESTART))})
		.WithButton({.IdSet = true, .Id = IdNewInstance, .Text = std::wstring(m_config->Runtime.GetStringRes(IDS_RESTART_GAME_NEWINSTANCE))})
		.WithCommonButton(TDCBF_CANCEL_BUTTON)
		.WithButtonCommandLinks()
		.WithButtonDefault(IdRestart)
		.Build()
		.Show()
		.Button;
	if (choice == IdRestart || choice == IdNewInstance) {
		Utils::Win32::RunProgramParams runParams{
			.path = GameExecutablePath(),
			.args = MakeLaunchArguments(),
			.elevateMode = m_bUseElevation ? Utils::Win32::RunProgramParams::Force : Utils::Win32::RunProgramParams::NeverUnlessShellIsElevated,
		};

		Dll::EnableInjectOnCreateProcess(0);
		const auto revertInjectOnCreateProcess = xivres::util::on_dtor([] { Dll::EnableInjectOnCreateProcess(Dll::InjectOnCreateProcessAppFlags::Use | Dll::InjectOnCreateProcessAppFlags::InjectGameOnly); });

		// This only mattered on initialization at AutoLoadAsDependencyModule, so it's safe to modify and not revert
		if (m_bUseXivAlexander)
			SetEnvironmentVariableW(L"XIVALEXANDER_DISABLE", nullptr);
		else
			SetEnvironmentVariableW(L"XIVALEXANDER_DISABLE", L"1");

		if (m_bUseXivAlexander) {
			runParams.args = std::format(L"-a launcher -l select {} {}", Utils::Win32::ReverseCommandLineToArgv(runParams.path), runParams.args);
			runParams.path = Dll::Module().PathOf().parent_path() / Dll::XivAlexLoader64NameW;
		}

		if (Utils::Win32::RunProgram(std::move(runParams)) && choice == IdRestart) {
			RemoveTrayIcon();
			Utils::Win32::Process::Current().Terminate(0);
		}
	}
}

void XivAlexander::Apps::MainApp::Window::MainWindow::OnCommand_Menu_Restart(int menuId) {
	switch (menuId) {
		case ID_RESTART_RESTART:
			AskRestartGame();
			return;

		case ID_RESTART_COPYLAUNCHCOMMANDLINE:
			CopyLaunchCommandLine();
			return;

		default:
			if (ChooseForRestart(menuId))
				AskRestartGame(true);
			return;
	}
}

bool XivAlexander::Apps::MainApp::Window::MainWindow::ChooseForRestart(UINT commandId) {
	// Whether the launch changed, rather than what is remembered of it.
	switch (commandId) {
		case ID_RESTART_USEXIVALEXANDER:
			m_bUseXivAlexander = !m_bUseXivAlexander;
			return true;

		case ID_RESTART_USEPARAMETEROBFUSCATION:
			m_bUseParameterObfuscation = !m_bUseParameterObfuscation;
			return true;

		case ID_RESTART_USEELEVATION:
			m_bUseElevation = !m_bUseElevation;
			return true;

		case ID_RESTART_LANGUAGE_REMEMBER:
			if (m_config->Runtime.Launch.RememberedLanguage == xivres::game_language::Unspecified)
				m_config->Runtime.Launch.RememberedLanguage = m_gameLanguage;
			else
				m_config->Runtime.Launch.RememberedLanguage = xivres::game_language::Unspecified;
			return false;

		case ID_RESTART_LANGUAGE_ENGLISH:
			m_gameLanguage = xivres::game_language::English;
			if (m_config->Runtime.Launch.RememberedLanguage != xivres::game_language::Unspecified)
				m_config->Runtime.Launch.RememberedLanguage = m_gameLanguage;
			return true;

		case ID_RESTART_LANGUAGE_GERMAN:
			m_gameLanguage = xivres::game_language::German;
			if (m_config->Runtime.Launch.RememberedLanguage != xivres::game_language::Unspecified)
				m_config->Runtime.Launch.RememberedLanguage = m_gameLanguage;
			return true;

		case ID_RESTART_LANGUAGE_FRENCH:
			m_gameLanguage = xivres::game_language::French;
			if (m_config->Runtime.Launch.RememberedLanguage != xivres::game_language::Unspecified)
				m_config->Runtime.Launch.RememberedLanguage = m_gameLanguage;
			return true;

		case ID_RESTART_LANGUAGE_JAPANESE:
			m_gameLanguage = xivres::game_language::Japanese;
			if (m_config->Runtime.Launch.RememberedLanguage != xivres::game_language::Unspecified)
				m_config->Runtime.Launch.RememberedLanguage = m_gameLanguage;
			return true;

		case ID_RESTART_LANGUAGE_SIMPLIFIEDCHINESE:
			m_gameLanguage = xivres::game_language::ChineseSimplified;
			if (m_config->Runtime.Launch.RememberedLanguage != xivres::game_language::Unspecified)
				m_config->Runtime.Launch.RememberedLanguage = m_gameLanguage;
			return true;

		case ID_RESTART_LANGUAGE_KOREAN:
			m_gameLanguage = xivres::game_language::Korean;
			if (m_config->Runtime.Launch.RememberedLanguage != xivres::game_language::Unspecified)
				m_config->Runtime.Launch.RememberedLanguage = m_gameLanguage;
			return true;

		case ID_RESTART_LANGUAGE_CHINESETRADITIONAL:
			m_gameLanguage = xivres::game_language::TraditionalChinese;
			if (m_config->Runtime.Launch.RememberedLanguage != xivres::game_language::Unspecified)
				m_config->Runtime.Launch.RememberedLanguage = m_gameLanguage;
			return true;

		case ID_RESTART_REGION_REMEMBER:
			if (m_config->Runtime.Launch.RememberedRegion == xivres::game_publisher::Unspecified)
				m_config->Runtime.Launch.RememberedRegion = m_gameRegion;
			else
				m_config->Runtime.Launch.RememberedRegion = xivres::game_publisher::Unspecified;
			return false;

		case ID_RESTART_REGION_JAPAN:
			m_gameRegion = xivres::game_publisher::SquareEnixJapan;
			if (m_config->Runtime.Launch.RememberedRegion != xivres::game_publisher::Unspecified)
				m_config->Runtime.Launch.RememberedRegion = m_gameRegion;
			return true;

		case ID_RESTART_REGION_NORTH_AMERICA:
			m_gameRegion = xivres::game_publisher::SquareEnixAmerica;
			if (m_config->Runtime.Launch.RememberedRegion != xivres::game_publisher::Unspecified)
				m_config->Runtime.Launch.RememberedRegion = m_gameRegion;
			return true;

		case ID_RESTART_REGION_EUROPE:
			m_gameRegion = xivres::game_publisher::SquareEnixEurope;
			if (m_config->Runtime.Launch.RememberedRegion != xivres::game_publisher::Unspecified)
				m_config->Runtime.Launch.RememberedRegion = m_gameRegion;
			return true;
	}
	return false;
}

std::pair<bool, bool> XivAlexander::Apps::MainApp::Window::MainWindow::GetRestartChoiceState(UINT commandId) const {
	// Whether it is chosen, and whether it can be changed.
	const auto languageRegionModifiable = Dll::IsLanguageRegionModifiable();
	switch (commandId) {
		case ID_RESTART_USEXIVALEXANDER: return {m_bUseXivAlexander, !m_launchParameters.empty()};
		case ID_RESTART_USEPARAMETEROBFUSCATION: return {m_bUseParameterObfuscation, !m_launchParameters.empty()};
		case ID_RESTART_USEELEVATION: return {m_bUseElevation, !m_launchParameters.empty()};
		case ID_RESTART_LANGUAGE_REMEMBER: return {languageRegionModifiable && m_config->Runtime.Launch.RememberedLanguage != xivres::game_language::Unspecified, languageRegionModifiable};
		case ID_RESTART_LANGUAGE_ENGLISH: return {m_gameLanguage == xivres::game_language::English, languageRegionModifiable};
		case ID_RESTART_LANGUAGE_GERMAN: return {m_gameLanguage == xivres::game_language::German, languageRegionModifiable};
		case ID_RESTART_LANGUAGE_FRENCH: return {m_gameLanguage == xivres::game_language::French, languageRegionModifiable};
		case ID_RESTART_LANGUAGE_JAPANESE: return {m_gameLanguage == xivres::game_language::Japanese, languageRegionModifiable};
		case ID_RESTART_LANGUAGE_SIMPLIFIEDCHINESE: return {m_gameLanguage == xivres::game_language::ChineseSimplified, false};
		case ID_RESTART_LANGUAGE_KOREAN: return {m_gameLanguage == xivres::game_language::Korean, false};
		case ID_RESTART_LANGUAGE_CHINESETRADITIONAL: return {m_gameLanguage == xivres::game_language::TraditionalChinese, false};
		case ID_RESTART_REGION_REMEMBER: return {languageRegionModifiable && m_config->Runtime.Launch.RememberedRegion != xivres::game_publisher::Unspecified, languageRegionModifiable};
		case ID_RESTART_REGION_JAPAN: return {m_gameRegion == xivres::game_publisher::SquareEnixJapan, languageRegionModifiable};
		case ID_RESTART_REGION_NORTH_AMERICA: return {m_gameRegion == xivres::game_publisher::SquareEnixAmerica, languageRegionModifiable};
		case ID_RESTART_REGION_EUROPE: return {m_gameRegion == xivres::game_publisher::SquareEnixEurope, languageRegionModifiable};
		default: return {false, false};
	}
}
