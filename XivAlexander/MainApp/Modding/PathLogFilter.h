#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace XivAlexander {
	class Config;
}

namespace XivAlexander::Apps::MainApp::Features::Modding {
	/// Decides which paths the game asks for are logged: what the configured filters let through, and not again while
	/// it is among the last few logged from the same place.
	class PathLogFilter {
		const std::shared_ptr<Config> m_config;
		std::mutex m_mtx;
		std::map<size_t, std::vector<std::string>> m_recent;

	public:
		PathLogFilter();
		~PathLogFilter();

		/// \param path What the filters are matched against.
		/// \param replaced Whether the path was rewritten.
		/// \param site Where the path was asked for, to keep a separate recent list per place.
		/// \param key What is compared against the recent ones.
		[[nodiscard]] bool ShouldLog(const std::string& path, bool replaced, size_t site, const std::string& key);

		/// \returns Whether the filters let \p path through.
		[[nodiscard]] bool Wants(const std::string& path, bool replaced) const;

		/// \returns Whether \p key is not among the last few logged from \p site, remembering it either way.
		[[nodiscard]] bool IsFresh(size_t site, const std::string& key);
	};
}
