#pragma once

namespace App {
	enum class LogLevel {
		Debug = 10,
		Info = 20,
		Warning = 30,
		Error = 40,
	};
	enum class LogCategory {
		General,
		SocketHook,
		AllIpcMessageLogger,
		AnimationLockLatencyHandler,
		EffectApplicationDelayLogger,
		IpcTypeFinder,
	};
}
namespace App::Misc {
	class Logger {
	public:

		struct LogItem {
			LogCategory category;
			std::chrono::system_clock::time_point timestamp;
			LogLevel level;
			std::string log;

			SYSTEMTIME TimestampAsLocalSystemTime() const;
		};
		
	protected:
		class Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

		class LoggerCreator;
		friend class LoggerCreator;
		Logger();
		static std::weak_ptr<Logger> s_instance;

	public:
		static std::shared_ptr<Logger> Acquire();
		
		Logger(Logger&&) = delete;
		Logger(const Logger&) = delete;
		Logger operator =(Logger&&) = delete;
		Logger operator =(const Logger&) = delete;
		virtual ~Logger();

		void Log(LogCategory category, const char* s, LogLevel level = LogLevel::Info);
		void Log(LogCategory category, const char8_t* s, LogLevel level = LogLevel::Info);
		void Log(LogCategory category, const std::string& s, LogLevel level = LogLevel::Info);
		void Clear();

		[[nodiscard]] std::deque<const LogItem*> GetLogs() const;
		Utils::ListenerManager<Logger, void, const LogItem&> OnNewLogItem;

		template <LogLevel Level = LogLevel::Info, typename ... Args>
		void Format(LogCategory category, const char* format, Args ... args) {
			Log(category, std::format(format, std::forward<Args>(args)...), Level);
		}

		template <LogLevel Level = LogLevel::Info, typename ... Args>
		void Format(LogCategory category, const char8_t* format, Args ... args) {
			Log(category, std::format(reinterpret_cast<const char*>(format), std::forward<Args>(args)...), Level);
		}
	};
}
