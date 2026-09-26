#include "pch.h"
#include "Misc/CrashMessageBoxHandler.h"

#include <XivAlexander/XivAlexander.h>

#include "Config.h"
#include "resource.h"
#include "Misc/Hooks.h"
#include "Misc/Logger.h"
#include "Utils/Win32/Resource.h"

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

namespace {
	struct ErrorCodeName {
		uint32_t Code;
		const wchar_t* Name;
	};

	// What the game shows in place of what failed, as "<message>(%x)".
	const ErrorCodeName GameErrorCodes[]{
		{0x10000000, L"Client::Graphics::Kernel::DeviceDX11::Initialize failed"},
		{0x11000001, L"IDXGISwapChain::Present: DXGI_ERROR_DEVICE_RESET (0x887A0007)"},
		{0x11000002, L"IDXGISwapChain::Present: DXGI_ERROR_DEVICE_REMOVED (0x887A0005)"},
		{0x1100000F, L"IDXGISwapChain::Present: failed with something other than DXGI_ERROR_DEVICE_RESET or DXGI_ERROR_DEVICE_REMOVED"},
	};

	const ErrorCodeName HResultNames[]{
		{0x80004001, L"E_NOTIMPL"},
		{0x80004002, L"E_NOINTERFACE"},
		{0x80004003, L"E_POINTER"},
		{0x80004004, L"E_ABORT"},
		{0x80004005, L"E_FAIL"},
		{0x8000FFFF, L"E_UNEXPECTED"},
		{0x80070005, L"E_ACCESSDENIED"},
		{0x80070006, L"E_HANDLE"},
		{0x8007000E, L"E_OUTOFMEMORY"},
		{0x80070057, L"E_INVALIDARG"},
		{0x087A0001, L"DXGI_STATUS_OCCLUDED"},
		{0x087A0002, L"DXGI_STATUS_CLIPPED"},
		{0x087A0004, L"DXGI_STATUS_NO_REDIRECTION"},
		{0x087A0005, L"DXGI_STATUS_NO_DESKTOP_ACCESS"},
		{0x087A0006, L"DXGI_STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE"},
		{0x087A0007, L"DXGI_STATUS_MODE_CHANGED"},
		{0x087A0008, L"DXGI_STATUS_MODE_CHANGE_IN_PROGRESS"},
		{0x887A0001, L"DXGI_ERROR_INVALID_CALL"},
		{0x887A0002, L"DXGI_ERROR_NOT_FOUND"},
		{0x887A0003, L"DXGI_ERROR_MORE_DATA"},
		{0x887A0004, L"DXGI_ERROR_UNSUPPORTED"},
		{0x887A0005, L"DXGI_ERROR_DEVICE_REMOVED"},
		{0x887A0006, L"DXGI_ERROR_DEVICE_HUNG"},
		{0x887A0007, L"DXGI_ERROR_DEVICE_RESET"},
		{0x887A000A, L"DXGI_ERROR_WAS_STILL_DRAWING"},
		{0x887A000B, L"DXGI_ERROR_FRAME_STATISTICS_DISJOINT"},
		{0x887A000C, L"DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE"},
		{0x887A0020, L"DXGI_ERROR_DRIVER_INTERNAL_ERROR"},
		{0x887A0021, L"DXGI_ERROR_NONEXCLUSIVE"},
		{0x887A0022, L"DXGI_ERROR_NOT_CURRENTLY_AVAILABLE"},
		{0x887A0023, L"DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED"},
		{0x887A0024, L"DXGI_ERROR_REMOTE_OUTOFMEMORY"},
		{0x887A0026, L"DXGI_ERROR_ACCESS_LOST"},
		{0x887A0027, L"DXGI_ERROR_WAIT_TIMEOUT"},
		{0x887A0028, L"DXGI_ERROR_SESSION_DISCONNECTED"},
		{0x887A0029, L"DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE"},
		{0x887A002A, L"DXGI_ERROR_CANNOT_PROTECT_CONTENT"},
		{0x887A002B, L"DXGI_ERROR_ACCESS_DENIED"},
		{0x887A002C, L"DXGI_ERROR_NAME_ALREADY_EXISTS"},
		{0x887A002D, L"DXGI_ERROR_SDK_COMPONENT_MISSING"},
		{0x887A002E, L"DXGI_ERROR_NOT_CURRENT"},
		{0x887A0030, L"DXGI_ERROR_HW_PROTECTION_OUTOFMEMORY"},
		{0x887A0031, L"DXGI_ERROR_DYNAMIC_CODE_POLICY_VIOLATION"},
		{0x887A0032, L"DXGI_ERROR_NON_COMPOSITED_UI"},
		{0x887A0033, L"DXGI_ERROR_MODE_CHANGE_IN_PROGRESS"},
		{0x887A0034, L"DXGI_ERROR_CACHE_CORRUPT"},
		{0x887A0035, L"DXGI_ERROR_CACHE_FULL"},
		{0x887A0036, L"DXGI_ERROR_CACHE_HASH_COLLISION"},
		{0x887A0037, L"DXGI_ERROR_ALREADY_EXISTS"},
		{0x887C0001, L"D3D11_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS"},
		{0x887C0002, L"D3D11_ERROR_FILE_NOT_FOUND"},
		{0x887C0003, L"D3D11_ERROR_TOO_MANY_UNIQUE_VIEW_OBJECTS"},
		{0x887C0004, L"D3D11_ERROR_DEFERRED_CONTEXT_MAP_WITHOUT_INITIAL_DISCARD"},
	};

	const ErrorCodeName FacilityNames[]{
		{0x000, L"FACILITY_NULL"},
		{0x001, L"FACILITY_RPC"},
		{0x002, L"FACILITY_DISPATCH"},
		{0x003, L"FACILITY_STORAGE"},
		{0x004, L"FACILITY_ITF"},
		{0x007, L"FACILITY_WIN32"},
		{0x008, L"FACILITY_WINDOWS"},
		{0x00A, L"FACILITY_CONTROL"},
		{0x87A, L"FACILITY_DXGI"},
		{0x87B, L"FACILITY_DXGI_DDI"},
		{0x87C, L"FACILITY_DIRECT3D11"},
		{0x87D, L"FACILITY_DIRECT3D11_DEBUG"},
		{0x87E, L"FACILITY_DIRECT3D12"},
		{0x87F, L"FACILITY_DIRECT3D12_DEBUG"},
	};

	const wchar_t* FindName(std::span<const ErrorCodeName> names, uint32_t code) {
		const auto it = std::ranges::find(names, code, &ErrorCodeName::Code);
		return it == names.end() ? nullptr : it->Name;
	}

	std::wstring DescribeErrorCode(uint32_t code) {
		if (const auto name = FindName(GameErrorCodes, code))
			return name;

		const auto facility = HRESULT_FACILITY(code);
		std::wstring res;
		if (const auto name = FindName(HResultNames, code))
			res = name;
		else if (const auto facilityName = FindName(FacilityNames, facility); facilityName && FAILED(static_cast<HRESULT>(code)))
			res = std::format(L"{}, code 0x{:X}", facilityName, HRESULT_CODE(code));
		else
			return {};

		if (const auto message = Utils::Win32::FormatWindowsErrorMessage(facility == FACILITY_WIN32 ? HRESULT_CODE(code) : code); !message.empty())
			res += std::format(L": {}", xivres::util::unicode::convert<std::wstring>(message));
		return res;
	}

	std::wstring DescribeErrorCodesIn(const std::wstring& body) {
		static const std::wregex EightHexDigits{LR"((?:^|[^0-9A-Fa-f])([0-9A-Fa-f]{8})(?![0-9A-Fa-f]))"};

		std::wstring res;
		std::set<uint32_t> seen;
		for (auto it = std::wsregex_iterator(body.begin(), body.end(), EightHexDigits); it != std::wsregex_iterator(); ++it) {
			const auto code = static_cast<uint32_t>(std::wcstoul((*it)[1].str().c_str(), nullptr, 16));
			if (!seen.insert(code).second)
				continue;
			if (const auto description = DescribeErrorCode(code); !description.empty())
				res += std::format(L"\n{:08x}: {}", code, description);
		}
		return res;
	}
}

#pragma optimize("", off)

struct XivAlexander::Misc::CrashMessageBoxHandler::Implementation {
	static Implementation* s_pInstance;
	Hooks::ImportedFunction<int, HWND, LPCWSTR, LPCWSTR, UINT> MessageBoxW{ "user32!MessageBoxW", "user32.dll", "MessageBoxW" };
	Hooks::ImportedFunction<int, HWND, LPCSTR, LPCSTR, UINT> MessageBoxA{ "user32!MessageBoxA", "user32.dll", "MessageBoxA" };
	Hooks::PointerFunction<LPTOP_LEVEL_EXCEPTION_FILTER, LPTOP_LEVEL_EXCEPTION_FILTER> SetUnhandledExceptionFilter{ "kernel32!SetUnhandledExceptionFilter", ::SetUnhandledExceptionFilter };

	xivres::util::on_dtor::multi m_cleanup;

	const std::wregex Whitespace{ LR"(\s+)" };

	_crt_signal_t m_prevSignalHandler{};
	LPTOP_LEVEL_EXCEPTION_FILTER m_prevTopLevelExceptionHandler{};

	//
	// https://stackoverflow.com/a/28276227
	//

	static inline std::wstring DumpStackTrace(const CONTEXT& context, HANDLE hThread = GetCurrentThread()) {
		union Symbol {
			char Buffer[sizeof(SYMBOL_INFOW) + sizeof(wchar_t) * 1024]{};
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
				if (res.empty())
					return Data.Name;
				return res;
			}
		};

		try {
			if (!SymInitialize(GetCurrentProcess(), nullptr, false))
				throw(std::logic_error("Unable to initialize symbol handler"));
			SymSetOptions(SymGetOptions() | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);

			std::vector<Utils::Win32::LoadedModule> modules;
			std::map<HMODULE, MODULEINFO> modInfos;
			for (const auto& mod : Utils::Win32::Process::Current().EnumModules()) {
				modules.emplace_back(Utils::Win32::LoadedModule(mod, false));
				const auto& modInfo = modInfos[mod] = modules.back().ModuleInfo();
				SymLoadModuleExW(GetCurrentProcess(), nullptr,
					modules.back().PathOf().c_str(),
					modules.back().BaseName().c_str(),
					reinterpret_cast<DWORD64>(mod), modInfo.SizeOfImage, nullptr, 0);
			}

			STACKFRAME64 frame;
#ifdef _M_X64
			frame.AddrPC.Offset = context.Rip;
			frame.AddrPC.Mode = AddrModeFlat;
			frame.AddrStack.Offset = context.Rsp;
			frame.AddrStack.Mode = AddrModeFlat;
			frame.AddrFrame.Offset = context.Rbp;
			frame.AddrFrame.Mode = AddrModeFlat;
#else
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

			IMAGEHLP_LINEW64 line = { .SizeOfStruct = sizeof line };
			std::wostringstream builder;
			do {
				if (frame.AddrPC.Offset) {
					builder << L"* " << ReadableAddress(frame.AddrPC.Offset, modules) << ": " << Symbol(frame.AddrPC.Offset).UndecoratedName();
					if (DWORD offsetFromSymbol{};
						SymGetLineFromAddrW64(GetCurrentProcess(), frame.AddrPC.Offset, &offsetFromSymbol, &line))
						builder << std::format(L" {}:{}(+0x{:X})\n", line.FileName, line.LineNumber, offsetFromSymbol);
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

	static std::wstring ReadableAddress(DWORD64 address, std::vector<Utils::Win32::LoadedModule>& modules) {
		if (modules.empty()) {
			for (const auto& mod : Utils::Win32::Process::Current().EnumModules()) {
				modules.emplace_back(Utils::Win32::LoadedModule(mod, false));
				const auto modInfo = modules.back().ModuleInfo();
				SymLoadModuleExW(GetCurrentProcess(), nullptr,
					modules.back().PathOf().c_str(),
					modules.back().BaseName().c_str(),
					reinterpret_cast<DWORD64>(mod), modInfo.SizeOfImage, nullptr, 0);
			}
			std::sort(modules.begin(), modules.end());
		}

		auto mod = std::lower_bound(modules.begin(), modules.end(), address, [](const Utils::Win32::LoadedModule& l, DWORD64 offset) { return l.Value<DWORD64>() < offset; });
		if (mod != modules.end() && mod != modules.begin() && mod->Value<DWORD64>() > address)
			--mod;
		if (mod == modules.end() || address < mod->Value<DWORD64>() || address >= mod->Value<DWORD64>() + mod->ModuleInfo().SizeOfImage)
			return std::format(L"0x{:X}", address - mod->Value<DWORD64>());
		
		const auto path = mod->PathOf();
		const auto modName = mod->BaseName();
		if (lstrcmpiW(path.filename().c_str(), modName.c_str()) != 0)
			return std::format(L"{}({})+0x{:X}", path, modName, address - mod->Value<DWORD64>());

		size_t cnt = 0;
		for (const auto& mod : modules)
			cnt += lstrcmpiW(mod.BaseName().c_str(), modName.c_str()) == 0 ? 1 : 0;
		if (cnt > 1)
			return std::format(L"{}({})+0x{:X}", path, modName, address - mod->Value<DWORD64>());

		return std::format(L"{}+0x{:X}", path, address - mod->Value<DWORD64>());
	}

	Implementation() {
		if (MessageBoxW) {
			m_cleanup += MessageBoxW.SetHook([this](HWND hWndParent, LPCWSTR body, LPCWSTR title, UINT flags) {
				return ProcessMessageBox(hWndParent, body, title, flags);
				});
		}
		if (MessageBoxA) {
			m_cleanup += MessageBoxA.SetHook([this](HWND hWndParent, LPCSTR body, LPCSTR title, UINT flags) {
				return ProcessMessageBox(hWndParent, Utils::FromAnsi(body), Utils::FromAnsi(title), flags);
				});
		}

		s_pInstance = this;

		m_prevSignalHandler = signal(SIGABRT, [](int) {
			CONTEXT ctx{};
			RtlCaptureContext(&ctx);
			s_pInstance->ShowMessage(std::format(L"Unexpected error occurred.\n\nStack Trace:\n{}", DumpStackTrace(ctx)));
			TerminateProcess(GetCurrentProcess(), 0);
			});

		m_cleanup += SetUnhandledExceptionFilter.SetHook([&](LPTOP_LEVEL_EXCEPTION_FILTER) -> LPTOP_LEVEL_EXCEPTION_FILTER { return nullptr; });

		m_prevTopLevelExceptionHandler = SetUnhandledExceptionFilter.bridge([](PEXCEPTION_POINTERS excInfo) -> LONG {
			s_pInstance->HandleExceptionPointers(excInfo);
			return EXCEPTION_CONTINUE_SEARCH;
			});
	}

	~Implementation() {
		SetUnhandledExceptionFilter.bridge(m_prevTopLevelExceptionHandler);
		signal(SIGABRT, m_prevSignalHandler);
		s_pInstance = nullptr;
	}

	void HandleExceptionPointers(PEXCEPTION_POINTERS excInfo, std::wstring addInfo = L"Unexpected error occurred.") {
		std::wostringstream errStr;
		errStr << addInfo << L"\n\n";

		auto stackTraceDisplayed = false;
		if (excInfo && !IsBadReadPtr(excInfo, sizeof * excInfo)) {
			try {
				std::vector<Utils::Win32::LoadedModule> modules;
				for (auto excRec = excInfo->ExceptionRecord; excRec && !IsBadReadPtr(excRec, sizeof *excRec); excRec = excRec->ExceptionRecord) {
					if (excRec != excInfo->ExceptionRecord)
						errStr << L"\n";
					errStr << std::format(L"Code: 0x{:X}\nFlags: 0x{:X}\n", excRec->ExceptionCode, excRec->ExceptionFlags);
					errStr << std::format(L"Address: {}\n", ReadableAddress(reinterpret_cast<DWORD64>(excRec->ExceptionAddress), modules));
					for (size_t i = 0; i < excRec->NumberParameters; i++) {
						errStr << std::format(L"Param #{}: {:x}\n", i, excRec->ExceptionInformation[i]);
					}
				}
				errStr << L"Stack trace:\n" << DumpStackTrace(*excInfo->ContextRecord);
				stackTraceDisplayed = true;
			} catch (const std::exception& e) {
				errStr << L"An error has occurred while trying to display information about the error.\n" << e.what();
			}
		}

		if (!stackTraceDisplayed) {
			try {
				CONTEXT ctx{};
				RtlCaptureContext(&ctx);
				errStr << L"Stack trace from error handler:\n" << DumpStackTrace(ctx);
			} catch (const std::exception& e) {
				errStr << L"An error has occurred while trying to display stack trace from error handler.\n" << e.what();
			}
		}

		ShowMessage(errStr.str());
		TerminateProcess(GetCurrentProcess(), 0);
	}

	void ShowMessage(const std::wstring& message, const std::wstring& title = {}) {
		// Prevent modal dialogues from processing messages for other windows

		const auto newBody = std::format(L"Thread: {}\n\n{}", Utils::Win32::TryGetThreadDescription(GetCurrentThread()), message);

		if (Logger::UseStderr()) {
			Logger::WriteStderr(xivres::util::unicode::convert<std::string>(std::format(L"[{}] {}", title.empty() ? Dll::GetGenericMessageBoxTitle() : title, newBody)));
			ExitProcess(1);
		}

		const auto okstr = Utils::Win32::MB_GetString(IDOK - 1);

		Utils::Win32::Thread(L"ShowErrorMessageThread", [&] {
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
						Logger::Acquire()->AskAndExportLogs(hWnd, xivres::util::unicode::convert<std::string>(*reinterpret_cast<std::wstring*>(lpRefData)));
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
				Dll::EnableInjectOnCreateProcess(0);

				const auto useXivAlexander = nRadio == 2001;
				auto builder = Utils::Win32::ProcessBuilder();
				if (!Dll::IsLoadedAsDependency() && useXivAlexander)
					builder
					.WithPath(Dll::Module().PathOf().parent_path() / Dll::XivAlexLoaderNameW)
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
				ShowMessage(std::format(L"{}{}\n\n{}", body, DescribeErrorCodesIn(body), DumpStackTrace(ctx)), title);
				return IDOK;  // since the game originally requested MB_OK
			}
		}

		return MessageBoxW.bridge(hWndParent, body.c_str(), title.c_str(), flags);
	}
};

XivAlexander::Misc::CrashMessageBoxHandler::Implementation* XivAlexander::Misc::CrashMessageBoxHandler::Implementation::s_pInstance = nullptr;

XivAlexander::Misc::CrashMessageBoxHandler::CrashMessageBoxHandler()
	: m_pImpl(std::make_unique<Implementation>()) {
}

XivAlexander::Misc::CrashMessageBoxHandler::~CrashMessageBoxHandler() = default;
