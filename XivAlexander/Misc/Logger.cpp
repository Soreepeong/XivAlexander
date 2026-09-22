#include "pch.h"
#include "Misc/Logger.h"

#include "Game/CommandLine.h"
#include "Utils/Win32/Handle.h"
#include "Utils/Win32/Process.h"
#include "Utils/Win32/Resource.h"

#include "Config.h"
#include "resource.h"
#include "XivAlexander.h"

const std::map<XivAlexander::LogCategory, const char*> XivAlexander::Misc::Logger::LogCategoryNames{
	{LogCategory::General, "General"},
	{LogCategory::SocketHook, "SocketHook"},
	{LogCategory::AllIpcMessageLogger, "AllIpcMessageLogger"},
	{LogCategory::NetworkTimingHandler, "NetworkTimingHandler"},
	{LogCategory::IpcTypeFinder, "IpcTypeFinder"},
	{LogCategory::GameResourceOverrider, "GameResourceOverrider"},
	{LogCategory::VirtualSqPacks, "VirtualSqPacks"},
	{LogCategory::MusicImporter, "MusicImporter"},
	{LogCategory::PatchCode, "PatchCode"},
	{LogCategory::OpcodeGuesser, "OpcodeGuesser"},
	{LogCategory::AltCodecMusic, "AltCodecMusic"},
	{LogCategory::AudioResampler, "AudioResampler"},
	{LogCategory::Signatures, "Signatures"},
};

std::weak_ptr<XivAlexander::Misc::Logger> XivAlexander::Misc::Logger::s_instance;

struct XivAlexander::Misc::Logger::Implementation final {
	static constexpr int MaxLogCount = 128 * 1024;
	Logger& logger;
	std::condition_variable m_threadTrigger;

	bool m_bQuitting = false;
	std::mutex m_pendingItemLock;
	std::mutex m_itemLock;
	std::deque<LogItem> m_items, m_pendingItems;
	uint64_t m_logIdCounter = 1;

	Utils::Win32::Thread m_hDispatcherThread;

	Implementation(Logger& logger)
		: logger(logger) {
	}

	~Implementation() {
		m_bQuitting = true;
		m_threadTrigger.notify_all();
		if (m_hDispatcherThread)
			void(m_hDispatcherThread.Wait(INFINITE));
	}

	void AddLogItem(LogItem item) {
		std::lock_guard lock(m_pendingItemLock);
		item.Id = m_logIdCounter++;
		if (UseStderr())
			WriteStderr(item.Format());
		if (m_hDispatcherThread) {
			m_pendingItems.push_back(std::move(item));
			while (m_pendingItems.size() > MaxLogCount)
				m_pendingItems.pop_front();
			m_threadTrigger.notify_all();
		} else {
			m_items.push_back(item);
		}
	}

	void StartDispatcher() {
		if (m_hDispatcherThread)
			return;

		std::lock_guard lock(m_pendingItemLock);
		if (m_hDispatcherThread)
			return;

		m_hDispatcherThread = Utils::Win32::Thread(std::format(L"XivAlexander::App::Misc::Logger({:x})::Implementation({:x}::DispatcherThreadBody",
			reinterpret_cast<size_t>(&logger), reinterpret_cast<size_t>(this)
		), [this] {
			while (true) {
				std::deque<LogItem> pendingItems;
				{
					std::unique_lock pendingLock(m_pendingItemLock);
					if (m_pendingItems.empty()) {
						m_threadTrigger.wait(pendingLock);
						if (m_bQuitting)
							return;
					}
					pendingItems = std::move(m_pendingItems);
				}
				{
					std::lock_guard itemLock(m_itemLock);
					for (auto& item : pendingItems) {
						m_items.push_back(item);
						if (m_items.size() > MaxLogCount)
							m_items.pop_front();
					}
				}
				logger.OnNewLogItem(pendingItems);
			}
		});
	}
};

class XivAlexander::Misc::Logger::LoggerCreator : public Logger {
public:
	LoggerCreator() = default;
	~LoggerCreator() override = default;
};

SYSTEMTIME XivAlexander::Misc::Logger::LogItem::TimestampAsLocalSystemTime() const {
	return Utils::EpochToLocalSystemTime(std::chrono::duration_cast<std::chrono::milliseconds>(Timestamp.time_since_epoch()).count());
}

std::string XivAlexander::Misc::Logger::LogItem::Format() const {
	const auto st = TimestampAsLocalSystemTime();
	return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}\t{}\t{}",
		st.wYear, st.wMonth, st.wDay,
		st.wHour, st.wMinute, st.wSecond,
		st.wMilliseconds,
		LogCategoryNames.at(Category),
		Log);
}

XivAlexander::Misc::Logger::Logger()
	: m_pImpl(std::make_unique<Implementation>(*this))
	, OnNewLogItem([this](const auto& cb) {
		std::lock_guard lock(m_pImpl->m_itemLock);
		cb(m_pImpl->m_items);
		m_pImpl->StartDispatcher();
	}) {
	Utils::Win32::DebugPrint(L"Logger: New");
}

std::shared_ptr<XivAlexander::Misc::Logger> XivAlexander::Misc::Logger::Acquire() {
	auto r = s_instance.lock();
	if (!r) {
		static std::mutex mtx;
		std::lock_guard lock(mtx);

		r = s_instance.lock();
		if (!r)
			s_instance = r = std::make_shared<LoggerCreator>();
	}
	return r;
}

bool XivAlexander::Misc::Logger::UseStderr() {
	static const bool s_enabled = [] {
		wchar_t buf[8];
		return 0 != GetEnvironmentVariableW(L"XIVALEXANDER_STDERR", buf, std::size(buf));
	}();
	return s_enabled;
}

void XivAlexander::Misc::Logger::WriteStderr(std::string_view line) {
	const auto h = GetStdHandle(STD_ERROR_HANDLE);
	if (!h || h == INVALID_HANDLE_VALUE)
		return;

	DWORD written;
	WriteFile(h, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
	WriteFile(h, "\r\n", 2, &written, nullptr);
}

XivAlexander::Misc::Logger::~Logger() {
	Utils::Win32::DebugPrint(L"Logger: Destroy");
}

void XivAlexander::Misc::Logger::Log(LogCategory category, const char* s, LogLevel level) {
	Log(category, std::string(s), level);
}

void XivAlexander::Misc::Logger::Log(LogCategory category, const char8_t* s, LogLevel level) {
	Log(category, reinterpret_cast<const char*>(s), level);
}

void XivAlexander::Misc::Logger::Log(LogCategory category, const wchar_t* s, LogLevel level) {
	Log(category, xivres::util::unicode::convert<std::string>(s), level);
}

void XivAlexander::Misc::Logger::Log(LogCategory category, const std::string& s, LogLevel level) {
	OutputDebugStringW(std::format(L"{}\n", s).c_str());
	m_pImpl->AddLogItem(LogItem{
		.Id = 0,
		.Category = category,
		.Timestamp = std::chrono::system_clock::now(),
		.Level = level,
		.Log = s,
	});
}

void XivAlexander::Misc::Logger::Log(LogCategory category, const std::wstring& s, LogLevel level) {
	Log(category, xivres::util::unicode::convert<std::string>(s), level);
}

void XivAlexander::Misc::Logger::Log(LogCategory category, WORD wLanguage, UINT uStringResId, LogLevel level) {
	Log(category, FindStringResourceEx(Dll::Module(), uStringResId, wLanguage) + 1, level);
}

void XivAlexander::Misc::Logger::Clear() {
	std::lock_guard lock(m_pImpl->m_itemLock);
	std::lock_guard lock2(m_pImpl->m_pendingItemLock);
	m_pImpl->m_items.clear();
	m_pImpl->m_pendingItems.clear();
}

void XivAlexander::Misc::Logger::AskAndExportLogs(HWND hwndDialogParent, std::string_view heading, std::string_view preformatted) {
	static const COMDLG_FILTERSPEC saveFileTypes[] = {
		{.pszName = FindStringResourceEx(Dll::Module(), IDS_FILTERSPEC_LOGFILES) + 1, .pszSpec = L"*.log"},
		{.pszName = FindStringResourceEx(Dll::Module(), IDS_FILTERSPEC_ALLFILES) + 1, .pszSpec = L"*.*"},
	};

	try {
		IFileSaveDialogPtr pDialog;
		DWORD dwFlags;
		Utils::Win32::Error::ThrowIfFailed(pDialog.CreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER));
		Utils::Win32::Error::ThrowIfFailed(pDialog->SetFileTypes(ARRAYSIZE(saveFileTypes), saveFileTypes));
		Utils::Win32::Error::ThrowIfFailed(pDialog->SetFileTypeIndex(0));
		Utils::Win32::Error::ThrowIfFailed(pDialog->SetDefaultExtension(L"log"));
		SYSTEMTIME lt{};
		GetLocalTime(&lt);
		Utils::Win32::Error::ThrowIfFailed(pDialog->SetFileName(std::format(L"XivAlexander_{:04}{:02}{:02}_{:02}{:02}{:02}_{:03}.log", lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond, lt.wMilliseconds).c_str()));
		Utils::Win32::Error::ThrowIfFailed(pDialog->GetOptions(&dwFlags));
		Utils::Win32::Error::ThrowIfFailed(pDialog->SetOptions(dwFlags | FOS_FORCEFILESYSTEM));
		Utils::Win32::Error::ThrowIfFailed(pDialog->Show(hwndDialogParent), true);

		std::filesystem::path newFileName;
		{
			IShellItemPtr pResult;
			PWSTR pszNewFileName;
			Utils::Win32::Error::ThrowIfFailed(pDialog->GetResult(&pResult));
			Utils::Win32::Error::ThrowIfFailed(pResult->GetDisplayName(SIGDN_FILESYSPATH, &pszNewFileName));
			if (!pszNewFileName)
				throw std::runtime_error("DEBUG: The selected file does not have a filesystem path.");
			newFileName = pszNewFileName;
		}

		{
			const auto& process = Utils::Win32::Process::Current();
			std::ofstream of(newFileName);
			if (!heading.empty())
				of << heading << "\n";
			else
				of << "Log\n";

			of << "\nCommand Line:\n";
			try {
				of << std::format("{}\n", Utils::Win32::Process::Current().PathOf().wstring());
				for (auto& [k, v] : Game::CommandLine::FromString(Dll::GetOriginalCommandLine())) {
					if (k == "DEV.TestSID") {
						for (auto& c : v)
							c = '*';
						of << std::format("{}={}({})\n", k, v, v.size());
					} else
						of << std::format("{}={}\n", k, v);
				}
			} catch (const std::exception& e) {
				of << std::format("Failed to read command line parameters: {}\n", e.what());
			}

			of << "\nEnvironment Variables:\n";
			{
				auto ptr = GetEnvironmentStringsW();
				const auto ptrFree = xivres::util::on_dtor([ptr] { FreeEnvironmentStringsW(ptr); });
				while (*ptr) {
					const auto part = std::wstring(ptr);
					of << std::format("{}\n", part);
					ptr += part.size() + 1;
				}
			}

			of << "\nModules:\n";
			for (const auto& hModule : process.EnumModules()) {
				std::wstring name;
				std::string fileVersion = "-";
				std::string productVersion = "-";
				try {
					const auto hDllVersion = FindResourceW(hModule, MAKEINTRESOURCE(VS_VERSION_INFO), RT_VERSION);
					if (!hDllVersion)
						throw std::runtime_error("Failed to find version resource.");
					const auto hVersionResource = Utils::Win32::GlobalResource(LoadResource(hModule, hDllVersion),
						nullptr,
						"FormatModuleVersionString: Failed to load version resource.");
					const auto lpVersionInfo = LockResource(hVersionResource);  // no need to "UnlockResource"
					std::tie(fileVersion, productVersion) = Utils::Win32::FormatModuleVersionString(lpVersionInfo);

					struct LANGANDCODEPAGE {
						WORD wLanguage;
						WORD wCodePage;
					} * lpTranslate;
					UINT cbTranslate;
					if (!VerQueryValueW(lpVersionInfo,
						TEXT("\\VarFileInfo\\Translation"),
						reinterpret_cast<LPVOID*>(&lpTranslate),
						&cbTranslate))
						continue;

					for (size_t i = 0; i < cbTranslate / sizeof(LANGANDCODEPAGE); i++) {
						wchar_t* buf = nullptr;
						UINT size = 0;
						if (!VerQueryValueW(lpVersionInfo,
							std::format(L"\\StringFileInfo\\{:04x}{:04x}\\FileDescription",
								lpTranslate[i].wLanguage,
								lpTranslate[i].wCodePage).c_str(),
							reinterpret_cast<LPVOID*>(&buf),
							&size))
							continue;
						auto currName = std::wstring_view(buf, size);
						while (!currName.empty() && currName.back() == L'\0')
							currName = currName.substr(0, currName.size() - 1);
						if (currName.empty())
							continue;
						if (!name.empty())
							name += L", ";
						name += currName;
					}
				} catch (...) {

				}

				of << std::format("0x{:016x}: {} ({}, {}, {})\n",
					reinterpret_cast<size_t>(hModule),
					process.PathOf(hModule).wstring(),
					name, fileVersion, productVersion
				);
			}

			{
				const auto config = Config::Acquire();
				if (const auto path = config->Init.GetConfigPath(); exists(path)) {
					of << "\nInit Config:\n";
					try {
						const auto h = Utils::Win32::Handle::FromCreateFile(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING);
						const auto buf = h.Read<char>(0, std::min<uint64_t>(h.GetFileSize(), 1048576));
						of << std::string_view(buf.begin(), buf.end()) << "\n";
					} catch (const std::exception& e) {
						of << std::format("ERROR: Failed to read config file at {}: {}\n", path.wstring(), e.what());
					}
				}
				if (const auto path = config->Runtime.GetConfigPath(); exists(path)) {
					of << "\nRuntime Config:\n";
					try {
						const auto h = Utils::Win32::Handle::FromCreateFile(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING);
						const auto buf = h.Read<char>(0, std::min<uint64_t>(h.GetFileSize(), 1048576));
						of << std::string_view(buf.begin(), buf.end()) << "\n";
					} catch (const std::exception& e) {
						of << std::format("ERROR: Failed to read config file at {}: {}\n", path.wstring(), e.what());
					}
				}
				if (const auto path = config->Game.GetConfigPath(); exists(path)) {
					of << "\nOpcode Config:\n";
					try {
						const auto h = Utils::Win32::Handle::FromCreateFile(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING);
						const auto buf = h.Read<char>(0, std::min<uint64_t>(h.GetFileSize(), 1048576));
						of << std::string_view(buf.begin(), buf.end()) << "\n";
					} catch (const std::exception& e) {
						of << std::format("ERROR: Failed to read config file at {}: {}\n", path.wstring(), e.what());
					}
				}
			}

			of << "\nLogs:\n";
			if (preformatted.empty())
				WithLogs([&](const auto& items) {
					for (const auto& item : items) {
						of << item.Format() << "\n";
					}
				});
			else
				of << preformatted;
		}
		if (Dll::MessageBoxF(hwndDialogParent, MB_YESNO | MB_ICONINFORMATION, IDS_LOG_SAVED, newFileName.wstring()) == IDYES) {
			SHELLEXECUTEINFOW shex{
				.cbSize = sizeof shex,
				.hwnd = hwndDialogParent,
				.lpFile = newFileName.c_str(),
				.nShow = SW_SHOW,
			};
			if (!ShellExecuteExW(&shex))
				throw Utils::Win32::Error("ShellExecuteExW");
		}

	} catch (const Utils::Win32::CancelledError&) {  // NOLINT(bugprone-empty-catch)
		// pass

	} catch (const std::exception& e) {
		Dll::MessageBoxF(hwndDialogParent, MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
	}
}

void XivAlexander::Misc::Logger::WithLogs(const std::function<void(const std::deque<LogItem>& items)>& cb) const {
	std::lock_guard lock(m_pImpl->m_itemLock);
	cb(m_pImpl->m_items);
}

const wchar_t* XivAlexander::Misc::Logger::GetStringResource(UINT uStringResFormatId, WORD wLanguage) {
	return FindStringResourceEx(Dll::Module(), uStringResFormatId, wLanguage) + 1;
}
