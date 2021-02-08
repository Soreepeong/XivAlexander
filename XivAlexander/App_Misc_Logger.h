#pragma once
namespace App::Misc {
	class Logger {
	public:
		enum class LogLevel {
			Debug = 40,
			Info,
			Warning,
			Error,
		};

		struct LogItem {
			FILETIME timestamp;
			LogLevel level;
			std::string log;
		};
		
	private:
		class Internals;
		const std::unique_ptr<Internals> m_pImpl;

	public:
		Logger();
		Logger(Logger&&) = delete;
		Logger(const Logger&) = delete;
		Logger operator =(Logger&&) = delete;
		Logger operator =(const Logger&) = delete;
		~Logger();

		void Log(const char* s, LogLevel level = LogLevel::Info);
		void Log(const char8_t* s, LogLevel level = LogLevel::Info);
		void Log(const std::string& s, LogLevel level = LogLevel::Info);
		void Clear();

		std::deque<const LogItem*> GetLogs() const;
		Utils::ListenerManager<Logger, void, const LogItem&> OnNewLogItem;

		template <LogLevel Level = LogLevel::Info, typename ... Args>
		void Format(const _Printf_format_string_ char* format, Args ... args) {
			Log(Utils::FormatString(format, std::forward<Args>(args)...), Level);
		}

		template <LogLevel Level = LogLevel::Info, typename ... Args>
		void Format(const _Printf_format_string_ char8_t* format, Args ... args) {
			Log(Utils::FormatString(reinterpret_cast<const char*>(format), std::forward<Args>(args)...), Level);
		}

		static Logger& GetLogger();
	};
}
