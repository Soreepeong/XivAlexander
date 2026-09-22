#include "pch.h"
#include "MainApp/Modding/PathLogFilter.h"

#include "Config.h"

namespace XivAlexander::Apps::MainApp::Features::Modding {
	PathLogFilter::PathLogFilter()
		: m_config(Config::Acquire()) {}

	PathLogFilter::~PathLogFilter() = default;

	bool PathLogFilter::ShouldLog(const std::string& path, bool replaced, size_t site, const std::string& key) {
		return Wants(path, replaced) && IsFresh(site, key);
	}

	bool PathLogFilter::Wants(const std::string& path, bool replaced) const {
		for (const auto& filter : m_config->Runtime.LogPathFilters.Value()) {
			if (filter.Pattern.empty())
				continue;
			if (regex_search(path, filter.Regex()))
				return filter.Include;
		}

		return m_config->Runtime.LogAllPaths
			|| m_config->Runtime.UseHashTrackerKeyLogging
			|| (replaced && m_config->Runtime.LogReplacedPaths);
	}

	bool PathLogFilter::IsFresh(size_t site, const std::string& key) {
		const auto lock = std::lock_guard(m_mtx);
		auto& recent = m_recent[site];
		const auto it = std::ranges::find(recent, key);
		const auto fresh = it == recent.end();
		if (!fresh)
			recent.erase(it);
		recent.push_back(key);
		while (recent.size() > 16)
			recent.erase(recent.begin());
		return fresh;
	}
}
