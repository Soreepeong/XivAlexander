#pragma once

#include <XivAlexanderCommon/Utils/ListenerManager.h>
#include <XivAlexanderCommon/Utils/Win32/Resource.h>

namespace XivAlexander {
	enum class LogLevel {
		Unset = 0,
		Debug = 10,
		Info = 20,
		Warning = 30,
		Error = 40,
	};
	enum class LogCategory {
		General,
		SocketHook,
		AllIpcMessageLogger,
		NetworkTimingHandler,
		EffectApplicationDelayLogger,
		IpcTypeFinder,
		GameResourceOverrider,
		VirtualSqPacks,
		MusicImporter,
	};
}

namespace XivAlexander::Misc {
	class Logger {
	public:
		static const std::map<LogCategory, const char*> LogCategoryNames;

		struct LogItem {
			uint64_t id;
			LogCategory category;
			std::chrono::system_clock::time_point timestamp;
			LogLevel level;
			std::string log;

			[[nodiscard]] SYSTEMTIME TimestampAsLocalSystemTime() const;
			[[nodiscard]] std::string Format() const;
		};

	protected:
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

		class LoggerCreator;
		friend class LoggerCreator;
		Logger();
		static std::weak_ptr<Logger> s_instance;

	public:
		static std::shared_ptr<Logger> Acquire();

		Logger(Logger&&) = delete;
		Logger(const Logger&) = delete;
		Logger operator=(Logger&&) = delete;
		Logger operator=(const Logger&) = delete;
		virtual ~Logger();

		void Log(LogCategory category, const char* s, LogLevel level = LogLevel::Info);
		void Log(LogCategory category, const char8_t* s, LogLevel level = LogLevel::Info);
		void Log(LogCategory category, const wchar_t* s, LogLevel level = LogLevel::Info);
		void Log(LogCategory category, const std::string& s, LogLevel level = LogLevel::Info);
		void Log(LogCategory category, const std::wstring& s, LogLevel level = LogLevel::Info);
		void Log(LogCategory category, WORD wLanguage, UINT uStringResId, LogLevel level = LogLevel::Info);
		void Clear();

		void AskAndExportLogs(HWND hwndDialogParent, std::string_view heading = std::string_view(), std::string_view preformatted = std::string_view());

		void WithLogs(const std::function<void(const std::deque<LogItem>& items)>& cb) const;
		Utils::ListenerManager<Logger, void, const std::deque<LogItem>&> OnNewLogItem;

		template <LogLevel Level = LogLevel::Info, typename ... Args>
		void Format(LogCategory category, const char* format, Args ... args) {
			Log(category, std::format(format, std::forward<Args>(args)...), Level);
		}

		template <LogLevel Level = LogLevel::Info, typename ... Args>
		void Format(LogCategory category, const wchar_t* format, Args ... args) {
			Log(category, std::format(format, std::forward<Args>(args)...), Level);
		}

		template <LogLevel Level = LogLevel::Info, typename ... Args>
		void Format(LogCategory category, const char8_t* format, Args ... args) {
			Log(category, std::format(reinterpret_cast<const char*>(format), std::forward<Args>(args)...), Level);
		}

	private:
		static const wchar_t* GetStringResource(UINT uStringResFormatId, WORD wLanguage = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));

	public:
		template <LogLevel Level = LogLevel::Info, typename ... Args>
		void Format(LogCategory category, WORD wLanguage, UINT uStringResFormatId, Args ... args) {
			Log(category, std::format(GetStringResource(uStringResFormatId, wLanguage), std::forward<Args>(args)...), Level);
		}

		template <LogLevel Level = LogLevel::Info, typename ... Args>
		void FormatDefaultLanguage(LogCategory category, UINT uStringResFormatId, Args ... args) {
			Log(category, std::format(GetStringResource(uStringResFormatId), std::forward<Args>(args)...), Level);
		}
	};
}
