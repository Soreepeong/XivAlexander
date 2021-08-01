#include "pch.h"
#include "App_Misc_Logger.h"

std::weak_ptr<App::Misc::Logger> App::Misc::Logger::s_instance;

class App::Misc::Logger::Implementation final {
	static const int MaxLogCount = 8192;

public:
	Logger& logger;
	std::condition_variable m_threadTrigger;

	bool m_bQuitting = false;
	std::mutex m_pendingItemLock;
	std::mutex m_itemLock;
	std::deque<LogItem> m_items, m_pendingItems;

	// needs to be last, as "this" needs to be done initializing
	const Utils::Win32::Thread m_hDispatcherThread;

	Implementation(Logger& logger)
		: logger(logger)
		, m_hDispatcherThread(std::format(L"XivAlexander::App::Misc::Logger({:x})::Implementation({:x}::DispatcherThreadBody",
			reinterpret_cast<size_t>(&logger), reinterpret_cast<size_t>(this)
			), [this]() { return DispatcherThreadBody(); }) {
		ResumeThread(m_hDispatcherThread);
	}

	~Implementation() {
		m_bQuitting = true;
		m_threadTrigger.notify_all();
		WaitForSingleObject(m_hDispatcherThread, INFINITE);
	}

	void DispatcherThreadBody() {
		while (true) {
			std::deque<LogItem> pendingItems;
			{
				std::unique_lock lock(m_pendingItemLock);
				if (m_pendingItems.empty()) {
					m_threadTrigger.wait(lock);
					if (m_bQuitting)
						return;
				}
				pendingItems = std::move(m_pendingItems);
			}
			{
				std::lock_guard lock(m_itemLock);
				for (auto& item : pendingItems) {
					m_items.push_back(item);
					if (m_items.size() > MaxLogCount)
						m_items.pop_front();
				}
			}
			logger.OnNewLogItem(pendingItems);
		}
	}

	void AddLogItem(LogItem item) {
		std::lock_guard lock(m_pendingItemLock);
		m_pendingItems.push_back(std::move(item));
		while (m_pendingItems.size() > MaxLogCount)
			m_pendingItems.pop_front();
		m_threadTrigger.notify_all();
	}
};

SYSTEMTIME App::Misc::Logger::LogItem::TimestampAsLocalSystemTime() const {
	return Utils::EpochToLocalSystemTime(std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count());
}

class App::Misc::Logger::LoggerCreator : public Logger {
public:
	LoggerCreator() = default;
	~LoggerCreator() override = default;
};

App::Misc::Logger::Logger()
	: m_pImpl(std::make_unique<Implementation>(*this))
	, OnNewLogItem([this](const auto& cb) {
		std::lock_guard lock(m_pImpl->m_itemLock);
		cb(m_pImpl->m_items);
	}) {
	Utils::Win32::DebugPrint(L"Logger: New");
}

std::shared_ptr<App::Misc::Logger> App::Misc::Logger::Acquire() {
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

App::Misc::Logger::~Logger() {
	Utils::Win32::DebugPrint(L"Logger: Destroy");
}

void App::Misc::Logger::Log(LogCategory category, const char* s, LogLevel level) {
	Log(category, std::string(s), level);
}

void App::Misc::Logger::Log(LogCategory category, const char8_t* s, LogLevel level) {
	Log(category, reinterpret_cast<const char*>(s), level);
}

void App::Misc::Logger::Log(LogCategory category, const wchar_t* s, LogLevel level) {
	Log(category, Utils::ToUtf8(s), level);
}

void App::Misc::Logger::Log(LogCategory category, const std::string& s, LogLevel level) {
	m_pImpl->AddLogItem(LogItem{
		category,
		std::chrono::system_clock::now(),
		level,
		s,
		});
}

void App::Misc::Logger::Log(LogCategory category, const std::wstring& s, LogLevel level) {
	Log(category, Utils::ToUtf8(s), level);
}

void App::Misc::Logger::Log(LogCategory category, WORD wLanguage, UINT uStringResId, LogLevel level) {
	Log(category, FindStringResourceEx(Dll::Module(), uStringResId, wLanguage) + 1, level);
}

void App::Misc::Logger::Clear() {
	std::lock_guard lock(m_pImpl->m_itemLock);
	std::lock_guard lock2(m_pImpl->m_pendingItemLock);
	m_pImpl->m_items.clear();
	m_pImpl->m_pendingItems.clear();
}

void App::Misc::Logger::WithLogs(const std::function<void(const std::deque<LogItem>& items)>& cb) const {
	std::lock_guard lock(m_pImpl->m_itemLock);
	cb(m_pImpl->m_items);
}
