#include "pch.h"
#include "App_Misc_Logger.h"

static App::Misc::Logger* s_pLogger;
class App::Misc::Logger::Internals {
public:
	Logger& logger;
	const Utils::Win32Handle<> m_hLogDispatcherThread;
	std::condition_variable m_threadTrigger;

	bool m_bQuitting = false;
	std::mutex m_pendingItemLock;
	std::mutex m_itemLock;
	std::deque<LogItem> m_items, m_pendingItems;

	Internals(Logger& logger)
		: logger(logger)
		, m_hLogDispatcherThread(CreateThread(nullptr, 0, [](PVOID p) -> DWORD { return reinterpret_cast<Internals*>(p)->ThreadWorker(); }, this, CREATE_SUSPENDED, nullptr))
	{
		ResumeThread(m_hLogDispatcherThread);
	}

	~Internals() {
		m_bQuitting = true;
		m_threadTrigger.notify_all();
		WaitForSingleObject(m_hLogDispatcherThread, INFINITE);
	}

	DWORD ThreadWorker() {
		Utils::SetThreadDescription(GetCurrentThread(), L"XivAlexander::Misc::Logger::Internals::ThreadWorker");
		while (true) {
			std::deque<LogItem> pendingItems;
			{
				std::unique_lock<std::mutex> lock(m_pendingItemLock);
				if (m_pendingItems.empty()) {
					m_threadTrigger.wait(lock);
					if (m_bQuitting)
						return 0;
				}
				pendingItems = std::move(m_pendingItems);
			}
			for (auto& item : pendingItems) {
				{
					std::lock_guard<std::mutex> lock(m_itemLock);
					m_items.push_back(item);
					if (m_items.size() > 1024)
						m_items.pop_front();
				}
				logger.OnNewLogItem(m_items.back());
			}
		}
	}

	void AddLogItem(LogItem item) {
		std::lock_guard<std::mutex> lock(m_pendingItemLock);
		m_pendingItems.push_back(std::move(item));
		m_threadTrigger.notify_all();
	}
};

App::Misc::Logger::Logger()
	: m_pImpl(std::make_unique<Internals>(*this)) {
	s_pLogger = this;
}

App::Misc::Logger::~Logger() {
	s_pLogger = nullptr;
}

void App::Misc::Logger::Log(LogCategory category, const char* s, LogLevel level) {
	Log(category, std::string(s));
}

void App::Misc::Logger::Log(LogCategory category, const char8_t* s, LogLevel level) {
	Log(category, reinterpret_cast<const char*>(s));
}

void App::Misc::Logger::Log(LogCategory category, const std::string& s, LogLevel level) {
	FILETIME ft;
	GetSystemTimePreciseAsFileTime(&ft);
	m_pImpl->AddLogItem(LogItem{
		category,
		ft,
		level,
		s,
		});
}

void App::Misc::Logger::Clear() {
	std::lock_guard<std::mutex> lock(m_pImpl->m_itemLock);
	std::lock_guard<std::mutex> lock2(m_pImpl->m_pendingItemLock);
	m_pImpl->m_items.clear();
	m_pImpl->m_pendingItems.clear();
}

std::deque<const App::Misc::Logger::LogItem*> App::Misc::Logger::GetLogs() const {
	std::deque<const App::Misc::Logger::LogItem*> res;
	std::for_each(m_pImpl->m_items.begin(), m_pImpl->m_items.end(), [&res](LogItem& item) {res.push_back(&item); });
	return res;
}

App::Misc::Logger& App::Misc::Logger::GetLogger() {
	return *s_pLogger;
}
