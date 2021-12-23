#include "pch.h"
#include "App_Misc_CrashMessageBoxHandler.h"

#include <XivAlexander/XivAlexander.h>

#include "App_ConfigRepository.h"
#include "App_Misc_Hooks.h"
#include "App_Misc_Logger.h"
#include "DllMain.h"
#include "resource.h"

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

#pragma optimize("", off)

struct App::Misc::CrashMessageBoxHandler::Implementation {
	static Implementation* s_pInstance;
	Hooks::ImportedFunction<int, HWND, LPCWSTR, LPCWSTR, UINT> MessageBoxW{ "user32!MessageBoxW", "user32.dll", "MessageBoxW" };
	Hooks::ImportedFunction<int, HWND, LPCSTR, LPCSTR, UINT> MessageBoxA{ "user32!MessageBoxA", "user32.dll", "MessageBoxA" };
	Hooks::PointerFunction<LPTOP_LEVEL_EXCEPTION_FILTER, LPTOP_LEVEL_EXCEPTION_FILTER> SetUnhandledExceptionFilter{ "kernel32!SetUnhandledExceptionFilter", ::SetUnhandledExceptionFilter };
	Utils::CallOnDestruction::Multiple m_cleanup;

	const std::wregex Whitespace{ LR"(\s+)" };

	_crt_signal_t m_prevSignalHandler{};
	LPTOP_LEVEL_EXCEPTION_FILTER m_prevTopLevelExceptionHandler{};

	//
	// https://stackoverflow.com/a/28276227
	//

	static inline std::wstring DumpStackTrace(const CONTEXT& context, HANDLE hThread = GetCurrentThread()) {
		union Symbol {
			char Buffer[sizeof SYMBOL_INFOW + sizeof(wchar_t) * 1024];
			SYMBOL_INFOW Data;

		public:
			Symbol(DWORD64 address) {
				Data.SizeOfStruct = static_cast<ULONG>(sizeof Data);
				Data.MaxNameLen = static_cast<ULONG>(sizeof Buffer - sizeof Data);
				DWORD64 displacement;
				SymFromAddrW(GetCurrentProcess(), address, &displacement, &Data);
			}

			std::wstring UndecoratedName() const {
				if (Data.Name[0] == L'\0')
					return L"<unknown>";

				std::wstring res(Data.MaxNameLen, L'\0');
				res.resize(UnDecorateSymbolNameW(Data.Name, &res[0], static_cast<DWORD>(res.size()), UNDNAME_COMPLETE));
				return res;
			}
		};

		try {
			if (!SymInitialize(GetCurrentProcess(), nullptr, false))
				throw(std::logic_error("Unable to initialize symbol handler"));
			SymSetOptions(SymGetOptions() | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);

			std::vector<Utils::Win32::LoadedModule> modules;
			for (const auto& mod : Utils::Win32::Process::Current().EnumModules()) {
				modules.emplace_back(Utils::Win32::LoadedModule(mod, false));
				const auto modInfo = modules.back().ModuleInfo();
				SymLoadModuleExW(GetCurrentProcess(), nullptr,
					modules.back().PathOf().c_str(),
					modules.back().BaseName().c_str(),
					reinterpret_cast<DWORD64>(mod), modInfo.SizeOfImage, nullptr, 0);
			}

#ifdef _M_X64
			STACKFRAME64 frame;
			frame.AddrPC.Offset = context.Rip;
			frame.AddrPC.Mode = AddrModeFlat;
			frame.AddrStack.Offset = context.Rsp;
			frame.AddrStack.Mode = AddrModeFlat;
			frame.AddrFrame.Offset = context.Rbp;
			frame.AddrFrame.Mode = AddrModeFlat;
#else
			STACKFRAME frame;
			frame.AddrPC.Offset = context.Eip;
			frame.AddrPC.Mode = AddrModeFlat;
			frame.AddrStack.Offset = context.Esp;
			frame.AddrStack.Mode = AddrModeFlat;
			frame.AddrFrame.Offset = context.Ebp;
			frame.AddrFrame.Mode = AddrModeFlat;
#endif

			const auto imageType = ImageNtHeader(modules[0])->FileHeader.Machine;
			std::sort(modules.begin(), modules.end());
			int n = 0;

			int frame_number = 0;
			IMAGEHLP_LINEW64 line = { .SizeOfStruct = sizeof line };
			std::wostringstream builder;
			do {
				if (frame.AddrPC.Offset) {
					auto mod = std::lower_bound(modules.begin(), modules.end(), frame.AddrPC.Offset, [](const Utils::Win32::LoadedModule& l, size_t offset) { return l.Value<size_t>() < offset; });
					if (mod != modules.end() && mod != modules.begin() && mod->Value<size_t>() > frame.AddrPC.Offset)
						--mod;
					if (mod != modules.end()) {
						const auto path = mod->PathOf();
						const auto modName = mod->BaseName();
						if (lstrcmpiW(path.filename().c_str(), modName.c_str()) == 0)
							builder << std::format(L"* {}+0x{:x}: ", path, frame.AddrPC.Offset - mod->Value<size_t>());
						else
							builder << std::format(L"* {}({})+0x{:x}: ", path, modName, frame.AddrPC.Offset - mod->Value<size_t>());
					} else
						builder << std::format(L"* ??? 0x{:x}: ", frame.AddrPC.Offset);
					builder << Symbol(frame.AddrPC.Offset).UndecoratedName();
					if (DWORD offsetFromSymbol{};
						SymGetLineFromAddrW64(GetCurrentProcess(), frame.AddrPC.Offset, &offsetFromSymbol, &line))
						builder << std::format(L" {}:{}(+0x{:x})\n", line.FileName, line.LineNumber, offsetFromSymbol);
					else
						builder << "\n";
				} else
					builder << "* (No Symbols: PC == 0)\n";
				if (!StackWalk64(imageType, GetCurrentProcess(), hThread, &frame, const_cast<PVOID>(reinterpret_cast<const void*>(&context)),
					nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
					break;
			} while (frame.AddrReturn.Offset != 0 && n++ < 128);

			SymCleanup(GetCurrentProcess());

			return builder.str();
		} catch (const std::exception& e) {
			return std::format(L"Error occurred while trying to capture stack trace: {}", e.what());
		}
	}

	Implementation() {
		if (MessageBoxW) {
			m_cleanup += MessageBoxW.SetHook([this](HWND hWndParent, LPCWSTR body, LPCWSTR title, UINT flags) {
				return ProcessMessageBox(hWndParent, body, title, flags);
				});
		}
		if (MessageBoxA) {
			m_cleanup += MessageBoxA.SetHook([this](HWND hWndParent, LPCSTR body, LPCSTR title, UINT flags) {
				return ProcessMessageBox(hWndParent, Utils::FromUtf8(body, CP_OEMCP), Utils::FromUtf8(title, CP_OEMCP), flags);
				});
		}

		s_pInstance = this;

		m_prevSignalHandler = signal(SIGABRT, [](int) {
			CONTEXT ctx{};
			RtlCaptureContext(&ctx);
			s_pInstance->ShowMessage(std::format(L"Unexpected error occurred.\n\nStack Trace:\n{}", DumpStackTrace(ctx)));
			TerminateProcess(GetCurrentProcess(), 0);
			});

		m_cleanup += SetUnhandledExceptionFilter.SetHook([](LPTOP_LEVEL_EXCEPTION_FILTER) -> LPTOP_LEVEL_EXCEPTION_FILTER { return nullptr; });

		m_prevTopLevelExceptionHandler = SetUnhandledExceptionFilter.bridge([](PEXCEPTION_POINTERS excInfo) -> LONG {

			std::wostringstream errStr;
			errStr << L"Unexpected error occurred.\n\n";
			try {
				std::vector<Utils::Win32::LoadedModule> modules;
				for (const auto& mod : Utils::Win32::Process::Current().EnumModules()) {
					modules.emplace_back(Utils::Win32::LoadedModule(mod, false));
					const auto modInfo = modules.back().ModuleInfo();
					SymLoadModuleExW(GetCurrentProcess(), nullptr,
						modules.back().PathOf().c_str(),
						modules.back().BaseName().c_str(),
						reinterpret_cast<DWORD64>(mod), modInfo.SizeOfImage, nullptr, 0);
				}
				std::sort(modules.begin(), modules.end());
				for (auto excRec = excInfo->ExceptionRecord; excRec; excRec = excRec->ExceptionRecord) {
					if (excRec != excInfo->ExceptionRecord)
						errStr << L"\n";
					errStr << std::format(L"Code: 0x{:x}\nFlags: 0x{:x}\n", excRec->ExceptionCode, excRec->ExceptionFlags);

					const auto address = reinterpret_cast<size_t>(excRec->ExceptionAddress);
					auto mod = std::lower_bound(modules.begin(), modules.end(), address, [](const Utils::Win32::LoadedModule& l, size_t offset) { return l.Value<size_t>() < offset; });
					if (mod != modules.end() && mod != modules.begin() && mod->Value<size_t>() > address)
						--mod;
					if (mod != modules.end())
						errStr << std::format(L"Address: {}+0x{:x}\n", mod->BaseName(), address - mod->Value<size_t>());
					else
						errStr << std::format(L"Address: 0x{:x}\n", address);
					for (size_t i = 0; i < excRec->NumberParameters; i++) {
						errStr << std::format(L"Param #{}: {}\n", i, excRec->ExceptionInformation[i]);
					}
				}
				errStr << L"Stack trace:\n" << DumpStackTrace(*excInfo->ContextRecord);
			} catch (const std::exception& e) {
				errStr << L"An error has occurred while trying to display information about the error.\n" << e.what();
			}

			s_pInstance->ShowMessage(errStr.str());
			TerminateProcess(GetCurrentProcess(), 0);
			return EXCEPTION_CONTINUE_SEARCH;
			});
	}

	~Implementation() {
		SetUnhandledExceptionFilter.bridge(m_prevTopLevelExceptionHandler);
		signal(SIGABRT, m_prevSignalHandler);
		s_pInstance = nullptr;
	}

	void ShowMessage(const std::wstring& message, const std::wstring& title = {}) {
		// Prevent modal dialogues from processing messages for other windows

		const auto newBody = std::format(L"Thread: {}\n\n{}", Utils::Win32::TryGetThreadDescription(GetCurrentThread()), message);
		const auto okstr = Utils::Win32::MB_GetString(IDOK - 1);

		Utils::Win32::Thread(L"ShowErrorMessageThread", [&]() {
			const auto config = Config::Acquire();

			const TASKDIALOG_BUTTON tdb[] = {
				{1001, config->Runtime.GetStringRes(IDS_UNRECOVERABLEERROR_EXPORTLOG)},
				{IDOK, okstr.c_str()},
			};

			const TASKDIALOG_BUTTON tdr[] = {
				{2001, config->Runtime.GetStringRes(IDS_UNRECOVERABLEERROR_RESTARTWITHXIVALEXANDER)},
				{2002, config->Runtime.GetStringRes(IDS_UNRECOVERABLEERROR_RESTARTWITHOUTXIVALEXANDER)},
				{2003, config->Runtime.GetStringRes(IDS_UNRECOVERABLEERROR_EXIT)},
			};

			const auto tdtitle = title.empty() ? Dll::GetGenericMessageBoxTitle() : std::format(L"{} (+{})", title, Dll::GetGenericMessageBoxTitle());
			const TASKDIALOGCONFIG tdc{
				.cbSize = sizeof tdc,
				.hInstance = Dll::Module(),
				.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_CAN_BE_MINIMIZED | TDF_EXPANDED_BY_DEFAULT,
				.pszWindowTitle = tdtitle.c_str(),
				.pszMainIcon = TD_ERROR_ICON,
				.pszMainInstruction = config->Runtime.GetStringRes(IDS_TITLE_UNRECOVERABLEERROR),
				.pszContent = config->Runtime.GetStringRes(IDS_TITLE_UNRECOVERABLEERROR_CONTENT),
				.cButtons = _countof(tdb),
				.pButtons = tdb,
				.nDefaultButton = IDOK,
				.cRadioButtons = _countof(tdr),
				.pRadioButtons = tdr,
				.nDefaultRadioButton = 2001,
				.pszExpandedInformation = newBody.c_str(),
				.pszFooter = config->Runtime.GetStringRes(IDS_TITLE_UNRECOVERABLEERROR_FOOTER),
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
								.lpFile = Config::Acquire()->Runtime.GetStringRes(IDS_URL_ISSUES),
								.nShow = SW_SHOW,
							};
							if (!ShellExecuteExW(&shex))
								Dll::MessageBoxF(hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, Utils::Win32::FormatWindowsErrorMessage(GetLastError()));
						}
					} else if (msg == TDN_CREATED) {
						SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
					}
					return S_OK;
				},
				.lpCallbackData = reinterpret_cast<LONG_PTR>(&newBody),
			};

			int nButton = IDCANCEL, nRadio = 2001;
			if (FAILED(TaskDialogIndirect(&tdc, &nButton, &nRadio, nullptr))) {
				MessageBoxW.bridge(nullptr, message.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
				return;
			}
			if (nButton == IDOK && (nRadio == 2001 || nRadio == 2002)) {
				XivAlexDll::EnableInjectOnCreateProcess(0);

				const auto useXivAlexander = nRadio == 2001;
				auto builder = Utils::Win32::ProcessBuilder();
				if (!Dll::IsLoadedAsDependency() && useXivAlexander)
					builder
					.WithPath(Dll::Module().PathOf().parent_path() / XivAlexDll::XivAlexLoaderNameW)
					.WithArgument(true, std::format(L"-a launcher -l select \"{}\" {}", Utils::Win32::Process::Current().PathOf().wstring(), Dll::GetOriginalCommandLine()));
				else
					builder
					.WithPath(Utils::Win32::Process::Current().PathOf())
					.WithArgument(true, Dll::GetOriginalCommandLine());

				if (useXivAlexander)
					builder.WithoutEnviron(L"XIVALEXANDER_DISABLE");
				else
					builder.WithEnviron(L"XIVALEXANDER_DISABLE", L"1");

				try {
					builder.Run();
				} catch (const std::exception& e) {
					Dll::MessageBoxF(nullptr, MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
				}
			}
			}).Wait();
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

				CONTEXT ctx{};
				RtlCaptureContext(&ctx);
				ShowMessage(std::format(L"{}\n\n{}", body, DumpStackTrace(ctx)), title);
				return IDOK;  // since the game originally requested MB_OK
			}
		}

		return MessageBoxW.bridge(hWndParent, body.c_str(), title.c_str(), flags);
	}
};

App::Misc::CrashMessageBoxHandler::Implementation* App::Misc::CrashMessageBoxHandler::Implementation::s_pInstance = nullptr;

App::Misc::CrashMessageBoxHandler::CrashMessageBoxHandler()
	: m_pImpl(std::make_unique<Implementation>()) {
}

App::Misc::CrashMessageBoxHandler::~CrashMessageBoxHandler() = default;
