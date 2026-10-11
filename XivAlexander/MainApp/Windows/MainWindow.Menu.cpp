#include "pch.h"
#include "MainApp/Windows/MainWindow.h"
#include "MainApp/Windows/MainWindow.Internal.h"

#include "Utils/Win32/Resource.h"

#include "resource.h"
#include "XivAlexander.h"

namespace {
	// The first command of the items added to the window menu; below the window menu's own (SC_*, from 0xF000).
	constexpr UINT SystemMenuCommandBase = 0x1000;

	bool DeleteSubMenuHolding(HMENU hMenu, UINT commandId) {
		for (int i = GetMenuItemCount(hMenu) - 1; i >= 0; --i) {
			const auto hSub = GetSubMenu(hMenu, i);
			if (!hSub)
				continue;
			for (int j = 0, count = GetMenuItemCount(hSub); j < count; ++j) {
				if (GetMenuItemID(hSub, j) == commandId) {
					DeleteMenu(hMenu, i, MF_BYPOSITION);
					return true;
				}
			}
			if (DeleteSubMenuHolding(hSub, commandId))
				return true;
		}
		return false;
	}

	void DeleteEmptySubMenus(HMENU hMenu) {
		for (int i = GetMenuItemCount(hMenu) - 1; i >= 0; --i) {
			const auto hSub = GetSubMenu(hMenu, i);
			if (!hSub)
				continue;
			DeleteEmptySubMenus(hSub);
			auto empty = true;
			for (int j = 0, count = GetMenuItemCount(hSub); empty && j < count; ++j) {
				MENUITEMINFOW mii{.cbSize = sizeof mii, .fMask = MIIM_FTYPE};
				empty = GetMenuItemInfoW(hSub, j, TRUE, &mii) && (mii.fType & MFT_SEPARATOR);
			}
			if (empty)
				DeleteMenu(hMenu, i, MF_BYPOSITION);
		}
	}

	void TidySeparators(HMENU hMenu) {
		const auto isSeparator = [hMenu](int i) {
			MENUITEMINFOW mii{.cbSize = sizeof mii, .fMask = MIIM_FTYPE};
			return GetMenuItemInfoW(hMenu, i, TRUE, &mii) && (mii.fType & MFT_SEPARATOR);
		};
		for (int i = GetMenuItemCount(hMenu) - 1; i >= 0; --i) {
			if (const auto hSub = GetSubMenu(hMenu, i))
				TidySeparators(hSub);
			else if (isSeparator(i) && (i == 0 || i == GetMenuItemCount(hMenu) - 1 || isSeparator(i - 1)))
				DeleteMenu(hMenu, i, MF_BYPOSITION);
		}
		if (const auto count = GetMenuItemCount(hMenu); count > 0 && isSeparator(0))
			DeleteMenu(hMenu, 0, MF_BYPOSITION);
	}

	/// Removes what the settings pages in the window cover; their commands and shortcuts still work.
	void TrimMenu(HMENU hMenu) {
		for (const UINT commandId : {
			ID_CONFIGURE_EDITOPCODECONFIGURATION,
			ID_CONFIGURE_CHECKFORUPDATEDOPCODES,
			ID_CONFIGURE_CHECKFORUPDATEDOPCODESONSTARTUP,
			ID_NETWORK_HIGHLATENCYMITIGATION_USELOGGING,
			ID_NETWORK_HIGHLATENCYMITIGATION_PREVIEWMODE,
			ID_NETWORK_USEIPCTYPEFINDER,
			ID_NETWORK_USEALLIPCMESSAGELOGGER,
			ID_MODDING_TTMP_OPENDIRECTORY,
			ID_MODDING_USEALTCODECMUSICSUPPORT,
			ID_MODDING_LOGALLFILEACCESS,
			ID_MODDING_ENABLE,
			ID_NETWORK_REDUCEPACKETDELAY,
			ID_NETWORK_RESETALLCONNECTIONS,
			ID_VIEW_ALWAYSONTOP,
			ID_VIEW_ALWAYSONTOPGAME,
			ID_VIEW_HIDEONMINIMIZE,
		}) {
			while (DeleteMenu(hMenu, commandId, MF_BYCOMMAND)) {}
		}

		// Whole submenus, each found by an item in it.
		for (const UINT commandId : {
			ID_RESTART_RESTART,
			ID_MODDING_TTMP_REFRESH,
			ID_CONFIGURE_RELOAD,
			ID_HELP_OPENHOMEPAGE,
			ID_NETWORK_HIGHLATENCYMITIGATION_ENABLE,
			ID_CONFIGURE_USEMORECPUTIME,
			ID_MODDING_MUTEVOICE_BATTLE,
			ID_CONFIGURE_GAMEFIX_OPENDIRECTORY,
			ID_CONFIGURE_LANGUAGE_SYSTEMDEFAULT,
			ID_CONFIGURE_THEME_SYSTEM,
			ID_CONFIGURE_WINDOWTITLE_PID_NONE,
			ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERALLPORTS,
		})
			DeleteSubMenuHolding(hMenu, commandId);

		DeleteEmptySubMenus(hMenu);
		TidySeparators(hMenu);
	}
}

void XivAlexander::Apps::MainApp::Window::MainWindow::RepopulateMenu() {
	if (GetWindowThreadProcessId(m_hWnd, nullptr) != GetCurrentThreadId()) {
		PostMessageW(m_hWnd, WmRepopulateMenu, 0, 0);
		return;
	}

	auto menu = Utils::Win32::Menu(Dll::Module(), RT_MENU, MAKEINTRESOURCE(IDR_TRAY_MENU), m_config->Runtime.GetLangId());

	const auto title = std::format(L"{}: {}, {}, {}",
		m_config->Runtime.GetStringRes(IDS_APP_NAME), GetCurrentProcessId(), m_gameReleaseInfo.CountryCode, m_gameReleaseInfo.GameVersion);
	ModifyMenuW(menu, ID_FILE_CURRENTINFO, MF_BYCOMMAND, ID_FILE_CURRENTINFO, title.c_str());

	TrimMenu(menu);
	m_menu = std::move(menu);

	// No menu bar: what is left of the menu is in the window menu (Alt+Space).
	if (const auto hAttached = GetMenu(m_hWnd)) {
		SetMenu(m_hWnd, nullptr);
		DestroyMenu(hAttached);
	}
	RepopulateSystemMenu();
}

void XivAlexander::Apps::MainApp::Window::MainWindow::RepopulateSystemMenu() {
	// Reset to the stock window menu; it reserves the low four bits of its commands, so items are numbered by sixteens.
	GetSystemMenu(m_hWnd, TRUE);
	const auto hSystemMenu = GetSystemMenu(m_hWnd, FALSE);
	const auto hFileMenu = GetSubMenu(m_menu, 0);
	m_systemMenuCommands.clear();
	if (!hSystemMenu || !hFileMenu)
		return;

	AppendMenuW(hSystemMenu, MF_SEPARATOR, 0, nullptr);
	for (int i = 0, count = GetMenuItemCount(hFileMenu); i < count; ++i) {
		const auto commandId = GetMenuItemID(hFileMenu, i);
		if (commandId == ID_FILE_CURRENTINFO)
			continue;
		if (commandId == 0 || commandId == static_cast<UINT>(-1)) {
			if (!m_systemMenuCommands.empty())
				AppendMenuW(hSystemMenu, MF_SEPARATOR, 0, nullptr);
			continue;
		}
		m_systemMenuCommands.push_back(commandId);
		AppendMenuW(hSystemMenu, MF_STRING, SystemMenuCommandBase + (m_systemMenuCommands.size() - 1) * 16, RepopulateMenu_GetMenuTextById(hFileMenu, commandId).c_str());
	}
}

LRESULT XivAlexander::Apps::MainApp::Window::MainWindow::OnSysCommand(WPARAM commandId, short xPos, short yPos) {
	const auto id = commandId & 0xFFF0;
	if (id >= SystemMenuCommandBase && (id - SystemMenuCommandBase) / 16 < m_systemMenuCommands.size()) {
		SendMessageW(m_hWnd, WM_COMMAND, MAKEWPARAM(m_systemMenuCommands[(id - SystemMenuCommandBase) / 16], 0), 0);
		return 0;
	}
	return BaseWindow::OnSysCommand(commandId, xPos, yPos);
}

std::wstring XivAlexander::Apps::MainApp::Window::MainWindow::RepopulateMenu_GetMenuTextById(HMENU hParentMenu, UINT commandId) {
	std::wstring res(static_cast<size_t>(1) + GetMenuStringW(hParentMenu, commandId, nullptr, 0, MF_BYCOMMAND), '\0');
	GetMenuStringW(hParentMenu, commandId, res.data(), static_cast<int>(res.size()), MF_BYCOMMAND);
	return res;
}

void XivAlexander::Apps::MainApp::Window::MainWindow::SetMenuStates() const {
	const auto hMenu = static_cast<HMENU>(m_menu);

	const auto& config = m_config->Runtime;
	using namespace Utils::Win32;

	// File, and its items in the window menu.
	{
		SetMenuState(hMenu, ID_FILE_SHOWCONTROLWINDOW, config.Ui.MainWindow.Show, true);
		SetMenuState(hMenu, ID_FILE_SHOWLOGGINGWINDOW, config.Ui.LogWindow.Show, true);
		if (const auto hSystemMenu = GetSystemMenu(m_hWnd, FALSE)) {
			for (size_t i = 0; i < m_systemMenuCommands.size(); ++i) {
				const auto state = GetMenuState(hMenu, m_systemMenuCommands[i], MF_BYCOMMAND);
				if (state == static_cast<UINT>(-1))
					continue;
				const auto id = static_cast<UINT>(SystemMenuCommandBase + i * 16);
				CheckMenuItem(hSystemMenu, id, MF_BYCOMMAND | (state & MF_CHECKED ? MF_CHECKED : MF_UNCHECKED));
				EnableMenuItem(hSystemMenu, id, MF_BYCOMMAND | (state & (MF_DISABLED | MF_GRAYED) ? MF_GRAYED : MF_ENABLED));
			}
		}
	}

	{
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_ENABLE, config.NetworkTiming.Enabled, true);
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_MODE_1, config.NetworkTiming.HighLatencyMitigationMode == HighLatencyMitigationMode::SubtractLatency, true);
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_MODE_2, config.NetworkTiming.HighLatencyMitigationMode == HighLatencyMitigationMode::SimulateRtt, true, config.FormatStringRes(IDS_MENU_NETWORKLATENCYHANDLEMODE_2, config.NetworkTiming.ExpectedAnimationLockDurationUs.Value()));
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_MODE_3, config.NetworkTiming.HighLatencyMitigationMode == HighLatencyMitigationMode::SimulateNormalizedRttAndLatency, true);
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_USELOGGING, config.NetworkTiming.UseHighLatencyMitigationLogging, true);
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_PREVIEWMODE, config.NetworkTiming.UseHighLatencyMitigationPreviewMode, true);
		SetMenuState(hMenu, ID_NETWORK_USEIPCTYPEFINDER, config.Opcodes.UseOpcodeFinder, true);
		SetMenuState(hMenu, ID_NETWORK_USEALLIPCMESSAGELOGGER, config.Opcodes.UseAllIpcMessageLogger, true);
		SetMenuState(hMenu, ID_NETWORK_REDUCEPACKETDELAY, config.Socket.ReducePacketDelay, true);
		SetMenuState(hMenu, ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERLOOPBACKADDRESSES, config.Socket.TakeOverLoopbackAddresses, true);
		SetMenuState(hMenu, ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERPRIVATEADDRESSES, config.Socket.TakeOverPrivateAddresses, true);
		SetMenuState(hMenu, ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERALLADDRESSES, config.Socket.TakeOverAllAddresses, true);
		SetMenuState(hMenu, ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERALLPORTS, config.Socket.TakeOverAllPorts, true);
	}

	{
		SetMenuState(hMenu, ID_MODDING_ENABLE, config.Modding.Enabled, true);
		SetMenuState(hMenu, ID_MODDING_USEALTCODECMUSICSUPPORT, config.Modding.UseAltCodecMusicSupport, true);
		SetMenuState(hMenu, ID_MODDING_LOGALLFILEACCESS, config.Modding.Logging.AllDataFileRead, true);

		SetMenuState(hMenu, ID_MODDING_MUTEVOICE_BATTLE, config.Modding.MuteVoice.Battle, true);
		SetMenuState(hMenu, ID_MODDING_MUTEVOICE_CM, config.Modding.MuteVoice.Cm, true);
		SetMenuState(hMenu, ID_MODDING_MUTEVOICE_EMOTE, config.Modding.MuteVoice.Emote, true);
		SetMenuState(hMenu, ID_MODDING_MUTEVOICE_LINE, config.Modding.MuteVoice.Line, true);

	}

	{
		SetMenuState(hMenu, ID_CONFIGURE_CHECKFORUPDATEDOPCODESONSTARTUP, config.Opcodes.CheckForUpdatesOnStartup, true);
		SetMenuState(hMenu, ID_CONFIGURE_USEMORECPUTIME, config.FramerateControl.UseMoreCpuTime, true);
		SetMenuState(hMenu, ID_CONFIGURE_BACKGROUND_FRAMERATE_LIMIT, config.FramerateControl.UseBackgroundLimit, true, config.FormatStringRes(IDS_MENU_BACKGROUND_FRAMERATE_TARGET, config.FramerateControl.BackgroundLimit.Value()));
		if (config.FramerateControl.Lock.Automatic)
			SetMenuState(hMenu, ID_CONFIGURE_LOCKFRAMERATE, true, true, m_config->Runtime.GetStringRes(IDS_MENU_LOCKFRAMERATE_AUTOMATIC));
		else if (config.FramerateControl.Lock.Interval)
			SetMenuState(hMenu, ID_CONFIGURE_LOCKFRAMERATE, true, true, m_config->Runtime.FormatStringRes(IDS_MENU_LOCKFRAMERATE, 1000000. / config.FramerateControl.Lock.Interval));
		else
			SetMenuState(hMenu, ID_CONFIGURE_LOCKFRAMERATE, false, true, m_config->Runtime.GetStringRes(IDS_MENU_LOCKFRAMERATE_DISABLED));
		SetMenuState(hMenu, ID_CONFIGURE_SYNCHRONIZEPROCESSING, config.FramerateControl.SynchronizeProcessing, true);
		SetMenuState(hMenu, ID_CONFIGURE_WINDOWTITLE_PID_NONE, config.GameWindow.TitleMode == GameWindowTitleMode::None, true);
		SetMenuState(hMenu, ID_CONFIGURE_WINDOWTITLE_PID_PREFIX, config.GameWindow.TitleMode == GameWindowTitleMode::Prefix, true);
		SetMenuState(hMenu, ID_CONFIGURE_WINDOWTITLE_PID_SUFFIX, config.GameWindow.TitleMode == GameWindowTitleMode::Suffix, true);
		SetMenuState(hMenu, ID_CONFIGURE_LANGUAGE_SYSTEMDEFAULT, config.Ui.Language == Language::SystemDefault, true);
		SetMenuState(hMenu, ID_CONFIGURE_LANGUAGE_ENGLISH, config.Ui.Language == Language::English, true);
		SetMenuState(hMenu, ID_CONFIGURE_LANGUAGE_KOREAN, config.Ui.Language == Language::Korean, true);
		SetMenuState(hMenu, ID_CONFIGURE_LANGUAGE_JAPANESE, config.Ui.Language == Language::Japanese, true);
		SetMenuState(hMenu, ID_CONFIGURE_THEME_SYSTEM, config.Ui.ThemeMode == ThemeMode::System, true);
		SetMenuState(hMenu, ID_CONFIGURE_THEME_LIGHT, config.Ui.ThemeMode == ThemeMode::Light, true);
		SetMenuState(hMenu, ID_CONFIGURE_THEME_DARK, config.Ui.ThemeMode == ThemeMode::Dark, true);
	}

	{
		SetMenuState(hMenu, ID_VIEW_ALWAYSONTOP, config.Ui.MainWindow.AlwaysOnTop, true);
		SetMenuState(hMenu, ID_VIEW_ALWAYSONTOPGAME, config.GameWindow.AlwaysOnTop, true);
		SetMenuState(hMenu, ID_VIEW_HIDEONMINIMIZE, config.Ui.MainWindow.HideOnMinimize, true);
	}
}

void XivAlexander::Apps::MainApp::Window::MainWindow::ShowContextMenu(const BaseWindow* parent) const {
	if (!parent)
		parent = this;

	SetMenuStates();
	POINT curPoint;
	GetCursorPos(&curPoint);

	BOOL result;

	{
		const auto temporaryFocus = parent->WithTemporaryFocus();
		result = TrackPopupMenu(
			GetSubMenu(m_menu, 0),
			TPM_RETURNCMD | TPM_NONOTIFY,
			curPoint.x,
			curPoint.y,
			0,
			parent->Handle(),
			nullptr
		);
	}

	if (result)
		SendMessageW(m_hWnd, WM_COMMAND, MAKEWPARAM(result, 0), 0);
}
