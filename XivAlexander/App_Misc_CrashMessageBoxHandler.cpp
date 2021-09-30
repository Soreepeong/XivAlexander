#include "pch.h"
#include "App_Misc_CrashMessageBoxHandler.h"

#include "App_Misc_Hooks.h"
#include "App_Misc_Logger.h"
#include "DllMain.h"
#include "resource.h"
#include "XivAlexander/XivAlexander.h"
#include "XivAlexanderCommon/XivAlex.h"

static const std::wstring_view PossibleCrashMessageBoxTitle[]{
	L"ファイナルファンタジーXIV", // Japanese
	L"FINAL FANTASY XIV", // English, German, French, and Korean
	L"最终幻想14", // Chinese
};

static const std::wstring_view PossibleCrashMessageBody[]{
	// Japanese
	L"「ファイナルファンタジーXIV」でエラーが発生したため終了しました。",
	L"DirectXで致命的なエラーが発生しました。",
	L"データファイルが見つかりません。「ファイナルファンタジーXIV」を一度アンインストールしてから再度インストールを試して下さい。",
	L"このアプリケーションは直接実行できません。ffxivboot.exe から実行してください。",
	L"「ファイナルファンタジーXIV」を終了します。",
	L"最新のDirectX がインストールされていません。",
	L"DirectX エンド ユーザー ランタイムをダウンロードして、インストールしてください。",

	// English
	L"An unexpected error has occurred. Exiting FINAL FANTASY XIV.",
	L"A fatal DirectX error has occurred.",
	L"Could not locate data files. Please reinstall FINAL FANTASY XIV.",
	L"Exiting FINAL FANTASY XIV.",
	L"Unable to launch application. Launch using the ffxivboot.exe file.",
	L"The latest version of DirectX is required to play FINAL FANTASY XIV.",
	L"Please download and install the DirectX End-User Runtime, then restart the game.",

	// German
	L"FINAL FANTASY XIV wurde aufgrund eines Fehlers beendet.",
	L"Ein schwerwiegender DirectX-Fehler ist aufgetreten.",
	L"FINAL FANTASY XIV wird beendet.",
	L"Die Datendateien konnten nicht gefunden werden. Bitte installieren Sie FINAL FANTASY XIV erneut.",
	L"Diese Anwendung kann nicht direkt gestartet werden. Bitte führen Sie „ffxivboot.exe“ aus.",
	L"Die neueste Version von DirectX ist nicht installiert. Bitte lade die DirectX-Endbenutzer-Runtime herunter, um die neueste Version von DirectX zu installieren.",

	// French
	L"FINAL FANTASY s'est fermé parce qu'une erreur s'est produite.",
	L"Une erreur fatale DirectX s’est produite.",
	L"FINAL FANTASY XIV va prendre fin.",
	L"Impossible de trouver les fichiers de données. Veuillez désinstaller puis réinstaller FINAL FANTASY XIV.",
	L"Impossible d'exécuter cette application. Veuillez lancer le jeu à partir du fichier ffxivboot.exe.",
	L"FINAL FANTASY XIV a rencontré une erreur. Le programme va prendre fin.",
	L"La dernière version de DirectX n'est pas installée. Veuillez télécharger et installer le programme d'installation de DirectX.",

	// Chinese
	L"《最终幻想14》发生意外错误，程序即将关闭。",
	L"DirectX出现了致命错误。",
	L"无法找到文件。请卸载并重新安装《最终幻想14》。",
	L"该程序无法直接运行。请运行ffxivboot.exe。",
	L"即将退出《最终幻想14》。",
	L"《最终幻想14》发生未知错误，程序即将关闭。",
	L"没有安装最新版的DirectX。",
	L"请下载并安装最新的DirectX End-User Runtime。",

	// Korean
	L"예기치 못한 오류로 인해 ‘파이널 판타지 14’가 종료되었습니다.",
	L"DirectX에서 심각한 오류가 발생했습니다.",
	L"데이터 파일을 찾을 수 없습니다. FINAL FANTASY XIV를 다시 설치해보시기 바랍니다.",
	L"이 응용 프로그램은 직접 실행할 수 없습니다.",
	L"FINAL FANTASY XIV을 종료합니다.",
	L"FINAL FANTASY XIV에서 오류가 발생하여 종료되었습니다.",
	L"최신 버전의 DirectX가 설치되지 않았습니다.",
	L"DirectX 엔드유저 런타임을 다운로드하여 설치하십시오.",
};

struct App::Misc::CrashMessageBoxHandler::Implementation {
	Hooks::ImportedFunction<int, HWND, LPCWSTR, LPCWSTR, UINT> MessageBoxW{"user32!MessageBoxW", "user32.dll", "MessageBoxW"};
	Hooks::ImportedFunction<int, HWND, LPCSTR, LPCSTR, UINT> MessageBoxA{"user32!MessageBoxA", "user32.dll", "MessageBoxA"};
	Utils::CallOnDestruction::Multiple m_cleanup;

	const std::wregex Whitespace{LR"(\s+)"};

	Implementation() {
		m_cleanup += MessageBoxW.SetHook([this](HWND hWndParent, LPCWSTR body, LPCWSTR title, UINT flags) {
			return ProcessMessageBox(hWndParent, body, title, flags);
		});
		m_cleanup += MessageBoxA.SetHook([this](HWND hWndParent, LPCSTR body, LPCSTR title, UINT flags) {
			return ProcessMessageBox(hWndParent, Utils::FromUtf8(body, CP_OEMCP), Utils::FromUtf8(title, CP_OEMCP), flags);
		});
	}

	int ProcessMessageBox(HWND hWndParent, const std::wstring& body, const std::wstring& title, UINT flags) {
		const auto actCtx = Dll::ActivationContext().With();

		// Add additional information if this message box only got OK button and has a FFXIV error message title.
		if ((flags & (MB_YESNO | MB_YESNOCANCEL | MB_OKCANCEL | MB_RETRYCANCEL | MB_ABORTRETRYIGNORE | MB_CANCELTRYCONTINUE)) == 0
			&& std::ranges::find(PossibleCrashMessageBoxTitle, title) != &PossibleCrashMessageBoxTitle[_countof(PossibleCrashMessageBoxTitle)]) {

			const auto bodyNormalizedWhitespace = std::regex_replace(body, Whitespace, L" ");
			for (const auto& candidate : PossibleCrashMessageBody) {
				if (bodyNormalizedWhitespace.find(candidate) == std::wstring::npos)
					continue;

				const auto newBody = std::format(L"Thread: {}\n\n{}", Utils::Win32::TryGetThreadDescription(GetCurrentThread()), body);

				const TASKDIALOG_BUTTON tdb[] = {
					{1001, L"Export &log"},
					{IDOK, L"OK"}
				};

				const TASKDIALOG_BUTTON tdr[] = {
					{2001, L"Restart &with XivAlexander"},
					{2002, L"Restart with&out XivAlexander"},
					{2003, L"E&xit"},
				};

				const auto hGameWindow = Dll::FindGameMainWindow(false);

				const auto tdtitle = std::format(L"{} (+{})", title, Dll::GetGenericMessageBoxTitle());
				const TASKDIALOGCONFIG tdc{
					.cbSize = sizeof tdc,
					.hwndParent = !hWndParent && hGameWindow && GetWindowThreadProcessId(hGameWindow, nullptr) == GetCurrentThreadId() ? hGameWindow : hWndParent,
					.hInstance = Dll::Module(),
					.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_CAN_BE_MINIMIZED,
					.pszWindowTitle = tdtitle.c_str(),
					.pszMainIcon = TD_ERROR_ICON,
					.pszMainInstruction = L"Unrecoverable error",
					.pszContent = L"An unexpected error has occurred, and the game must exit.\nXivAlexander may be the cause of the error. Make an issue at GitHub to get help.",
					.cButtons = _countof(tdb),
					.pButtons = tdb,
					.nDefaultButton = IDOK,
					.cRadioButtons = _countof(tdr),
					.pRadioButtons = tdr,
					.nDefaultRadioButton = 2001,
					.pszExpandedInformation = newBody.c_str(),
					.pszFooter = LR"(Report to <a href="issues">Issues at GitHub</a>)",
					.pfCallback = [](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, LONG_PTR lpRefData) -> HRESULT {
						if (msg == TDN_BUTTON_CLICKED && wParam == 1001) {
							Logger::Acquire()->AskAndExportLogs(hWnd, Utils::ToUtf8(*reinterpret_cast<std::wstring*>(lpRefData)));
							return S_FALSE;
						} else if (msg == TDN_HYPERLINK_CLICKED) {
							const auto target = std::wstring_view(reinterpret_cast<wchar_t*>(lParam));
							if (target == L"issues") {
								SHELLEXECUTEINFOW shex{
									.cbSize = sizeof shex,
									.hwnd = hWnd,
									.lpFile = L"https://github.com/Soreepeong/XivAlexander/issues",
									.nShow = SW_SHOW,
								};
								if (!ShellExecuteExW(&shex))
									Dll::MessageBoxF(hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, Utils::Win32::FormatWindowsErrorMessage(GetLastError()));
							}
						}
						return S_OK;
					},
					.lpCallbackData = reinterpret_cast<LONG_PTR>(&newBody),
				};

				int nButton = IDCANCEL, nRadio = 2001;
				if (FAILED(TaskDialogIndirect(&tdc, &nButton, &nRadio, nullptr)))
					break;
				if (nButton == IDOK && (nRadio == 2001 || nRadio == 2002)) {
					XivAlexDll::EnableInjectOnCreateProcess(0);

					const auto useXivAlexander = nRadio == 2001;
					auto builder = Utils::Win32::ProcessBuilder();
					if (!Dll::IsLoadedAsDependency() && useXivAlexander)
						builder
							.WithPath(Dll::Module().PathOf().parent_path() / XivAlex::XivAlexLoaderNameW)
							.WithArgument(true, std::format(L"-a launcher -l select \"{}\" {}", Utils::Win32::Process::Current().PathOf().wstring(), Utils::Win32::GetCommandLineWithoutProgramName()));
					else
						builder
							.WithPath(Utils::Win32::Process::Current().PathOf())
							.WithArgument(true, Utils::Win32::GetCommandLineWithoutProgramName());

					if (useXivAlexander)
						builder.WithoutEnviron(L"XIVALEXANDER_DISABLE");
					else
						builder.WithEnviron(L"XIVALEXANDER_DISABLE", L"1");

					try {
						builder.Run();
					} catch (const std::exception& e) {
						Dll::MessageBoxF(hWndParent, MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
					}
				}

				return IDOK; // since the game originally requested MB_OK
			}
		}

		return MessageBoxW.bridge(hWndParent, body.c_str(), title.c_str(), flags);
	}
};

App::Misc::CrashMessageBoxHandler::CrashMessageBoxHandler()
	: m_pImpl(std::make_unique<Implementation>()) {
}

App::Misc::CrashMessageBoxHandler::~CrashMessageBoxHandler() = default;
