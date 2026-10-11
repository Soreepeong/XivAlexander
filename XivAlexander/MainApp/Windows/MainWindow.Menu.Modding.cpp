#include "pch.h"
#include "MainApp/Windows/MainWindow.h"

#include "resource.h"
#include "XivAlexander.h"
#include "MainApp/App.h"
#include "MainApp/Modding/ResourceOverrider.h"
#include "MainApp/Modding/VirtualSqPacks.h"
#include "MainApp/Windows/ProgressPopupWindow.h"

void XivAlexander::Apps::MainApp::Window::MainWindow::OnCommand_Menu_Modding(int menuId) {
	auto& config = m_config->Runtime;

	switch (menuId) {
		case ID_MODDING_ENABLE:
			config.Modding.Enabled.Toggle();
			return;

		case ID_MODDING_USEALTCODECMUSICSUPPORT:
			config.Modding.UseAltCodecMusicSupport.Toggle();
			return;

		case ID_MODDING_LOGALLFILEACCESS:
			config.Modding.Logging.AllDataFileRead.Toggle();
			return;

		case ID_MODDING_MUTEVOICE_BATTLE:
			config.Modding.MuteVoice.Battle.Toggle();
			return;

		case ID_MODDING_MUTEVOICE_CM:
			config.Modding.MuteVoice.Cm.Toggle();
			return;

		case ID_MODDING_MUTEVOICE_EMOTE:
			config.Modding.MuteVoice.Emote.Toggle();
			return;

		case ID_MODDING_MUTEVOICE_LINE:
			config.Modding.MuteVoice.Line.Toggle();
			return;

		case ID_MODDING_TTMP_IMPORT: {
			const COMDLG_FILTERSPEC fileTypes[] = {
				{.pszName = m_config->Runtime.GetStringRes(IDS_FILTERSPEC_TTMP), .pszSpec = L"*.ttmp; *.ttmp2; *.mpl"},
				{.pszName = m_config->Runtime.GetStringRes(IDS_FILTERSPEC_ALLFILES), .pszSpec = L"*"},
			};
			const auto paths = ChooseFileToOpen(std::span(fileTypes), IDS_TITLE_IMPORT_TTMP);
			switch (paths.size()) {
				case 0:
					return;

				case 1: {
					ProgressPopupWindow progress(m_hWnd);
					progress.UpdateMessage(xivres::util::unicode::convert<std::string>(m_config->Runtime.GetStringRes(IDS_TITLE_IMPORTING)));
					progress.Show();

					std::string resultMessage;
					const auto adderThread = Utils::Win32::Thread(L"TTMP Importer", [&] {
						try {
							resultMessage = InstallTTMP(paths[0], progress);
						} catch (const std::exception& e) {
							progress.Cancel();
							Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
						}
						});

					while (WAIT_TIMEOUT == progress.DoModalLoop(100, { adderThread })) {
					}
					adderThread.Wait();

					if (!resultMessage.empty())
						Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONINFORMATION, xivres::util::unicode::convert<std::wstring>(resultMessage));
					return;
				}

				default:
					InstallMultipleFiles(paths);
			}

			return;
		}

		case ID_MODDING_TTMP_REMOVEALL:
		case ID_MODDING_TTMP_ENABLEALL:
		case ID_MODDING_TTMP_DISABLEALL: {
			if (const auto& sqpacks = m_app.GetResourceOverrider().GetVirtualSqPacks())
				BatchTtmpOperation(*sqpacks->GetTtmps(), menuId);
			return;
		}

		case ID_MODDING_TTMP_OPENDIRECTORY:
			EnsureAndOpenDirectory(ResolvePrimaryDirectory(ToggledPath::EnabledOf(m_config->Runtime.Modding.Ttmp.SearchDirectories.Value()), L"TexToolsMods"));
			return;

		case ID_MODDING_TTMP_REFRESH: {
			if (m_backgroundWorkerThread) {
				m_ttmpRescanPending = true;
				return;
			}
			if (auto& sqpacks = m_app.GetResourceOverrider().GetVirtualSqPacks()) {
				m_backgroundWorkerThread = Utils::Win32::Thread(L"RescanTtmpOnOtherThread", [this, &sqpacks]{
					m_backgroundWorkerProgressWindow = std::make_shared<ProgressPopupWindow>(nullptr);
					const auto workerThread = Utils::Win32::Thread(L"RescanTtmp", [&] {
						try {
							sqpacks->RescanTtmp(*m_backgroundWorkerProgressWindow);
						} catch (const std::exception& e) {
							m_backgroundWorkerProgressWindow->Cancel();
							Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
						}
						});

					do {
						m_backgroundWorkerProgressWindow->UpdateMessage(m_config->Runtime.GetStringRes(IDS_TITLE_DISCOVERINGFILES));
						// A rescan is usually quick enough that showing this at once only flashes it.
						m_backgroundWorkerProgressWindow->Show(std::chrono::seconds(3));
					} while (WAIT_TIMEOUT == m_backgroundWorkerProgressWindow->DoModalLoop(100, { workerThread }));
					workerThread.Wait();
					m_backgroundWorkerThread = nullptr;
					m_backgroundWorkerProgressWindow = nullptr;
					if (m_ttmpRescanPending.exchange(false))
						PostMessageW(m_hWnd, WM_COMMAND, ID_MODDING_TTMP_REFRESH, 0);
					});
			}
			return;
		}
	}
}

void XivAlexander::Apps::MainApp::Window::MainWindow::BatchTtmpOperation(Features::Modding::NestedTtmp& parent, int menuId) {
	try {
		std::wstring msg;
		switch (menuId) {
			case ID_MODDING_TTMP_REMOVEALL:
				msg = m_config->Runtime.FormatStringRes(IDS_CONFIRM_REMOVEALLTTMP, parent.Path.wstring());
				break;

			case ID_MODDING_TTMP_ENABLEALL:
				msg = m_config->Runtime.FormatStringRes(IDS_CONFIRM_ENABLEALLTTMP, parent.Path.wstring());
				break;

			case ID_MODDING_TTMP_DISABLEALL:
				msg = m_config->Runtime.FormatStringRes(IDS_CONFIRM_DISABLEALLTTMP, parent.Path.wstring());
				break;
		}
		if (Dll::MessageBoxF(m_hWnd, MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2, msg) == IDYES) {
			if (auto& sqpacks = m_app.GetResourceOverrider().GetVirtualSqPacks()) {
				m_backgroundWorkerThread = Utils::Win32::Thread(L"BatchTtmpOperationOnOtherThread", [this, &parent, &sqpacks, menuId] {
					m_backgroundWorkerProgressWindow = std::make_shared<ProgressPopupWindow>(nullptr);
					const auto workerThread = Utils::Win32::Thread(L"BatchTtmpOperation", [&] {
						try {
							auto lock = sqpacks->LockTtmps();
							parent.Traverse(false, [&](Features::Modding::NestedTtmp& nestedTtmp) {
								if (!nestedTtmp.Ttmp)
									return;

								const auto& set = *nestedTtmp.Ttmp;
								switch (menuId) {
									case ID_MODDING_TTMP_REMOVEALL:
										sqpacks->DeleteTtmp(set.ListPath, false);
										break;

									case ID_MODDING_TTMP_ENABLEALL:
										nestedTtmp.Enabled = true;
										sqpacks->ApplyTtmpChanges(nestedTtmp, false);
										break;

									case ID_MODDING_TTMP_DISABLEALL:
										nestedTtmp.Enabled = false;
										sqpacks->ApplyTtmpChanges(nestedTtmp, false);
										break;
								}
								});
							lock.unlock();

							sqpacks->RescanTtmp(*m_backgroundWorkerProgressWindow);
						} catch (const std::exception& e) {
							m_backgroundWorkerProgressWindow->Cancel();
							Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
						}
						});

					do {
						m_backgroundWorkerProgressWindow->UpdateMessage(m_config->Runtime.GetStringRes(IDS_TITLE_DISCOVERINGFILES));
						m_backgroundWorkerProgressWindow->Show();
					} while (WAIT_TIMEOUT == m_backgroundWorkerProgressWindow->DoModalLoop(100, { workerThread }));
					workerThread.Wait();
					m_backgroundWorkerThread = nullptr;
					m_backgroundWorkerProgressWindow = nullptr;
					if (m_ttmpRescanPending.exchange(false))
						PostMessageW(m_hWnd, WM_COMMAND, ID_MODDING_TTMP_REFRESH, 0);
					});
			}
		}
	} catch (const std::exception& e) {
		Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
	}
}
