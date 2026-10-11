#include "pch.h"
#include "MainApp/Windows/MainWindow.h"

#include "resource.h"
#include "XivAlexander.h"
#include "MainApp/App.h"
#include "MainApp/Features/SocketHook.h"

void XivAlexander::Apps::MainApp::Window::MainWindow::OnCommand_Menu_Network(int menuId) {
	auto& config = m_config->Runtime;

	switch (menuId) {
		case ID_NETWORK_HIGHLATENCYMITIGATION_ENABLE:
			config.NetworkTiming.Enabled.Toggle();
			return;

		case ID_NETWORK_HIGHLATENCYMITIGATION_MODE_1:
			config.NetworkTiming.HighLatencyMitigationMode = HighLatencyMitigationMode::SubtractLatency;
			return;

		case ID_NETWORK_HIGHLATENCYMITIGATION_MODE_2:
			config.NetworkTiming.HighLatencyMitigationMode = HighLatencyMitigationMode::SimulateRtt;
			return;

		case ID_NETWORK_HIGHLATENCYMITIGATION_MODE_3:
			config.NetworkTiming.HighLatencyMitigationMode = HighLatencyMitigationMode::SimulateNormalizedRttAndLatency;
			return;

		case ID_NETWORK_HIGHLATENCYMITIGATION_USELOGGING:
			config.NetworkTiming.UseHighLatencyMitigationLogging.Toggle();
			return;

		case ID_NETWORK_HIGHLATENCYMITIGATION_PREVIEWMODE:
			config.NetworkTiming.UseHighLatencyMitigationPreviewMode.Toggle();
			return;

		case ID_NETWORK_USEALLIPCMESSAGELOGGER:
			config.Opcodes.UseAllIpcMessageLogger.Toggle();
			return;

		case ID_NETWORK_USEIPCTYPEFINDER:
			config.Opcodes.UseOpcodeFinder.Toggle();
			return;

		case ID_NETWORK_REDUCEPACKETDELAY:
			config.Socket.ReducePacketDelay.Toggle();
			return;

		case ID_NETWORK_RESETALLCONNECTIONS:
			if (Dll::MessageBoxF(m_hWnd, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2, m_config->Runtime.GetStringRes(IDS_CONFIRM_RESET_ALL_CONNECTIONS)) == IDYES)
				m_app.RunOnGameLoop([this] { m_app.GetSocketHook().ResetAllConnections(); });
			return;

		case ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERLOOPBACKADDRESSES:
			config.Socket.TakeOverLoopbackAddresses.Toggle();
			return;

		case ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERPRIVATEADDRESSES:
			config.Socket.TakeOverPrivateAddresses.Toggle();
			return;

		case ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERALLADDRESSES:
			config.Socket.TakeOverAllAddresses.Toggle();
			return;

		case ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERALLPORTS:
			config.Socket.TakeOverAllPorts.Toggle();
			return;
	}
}
