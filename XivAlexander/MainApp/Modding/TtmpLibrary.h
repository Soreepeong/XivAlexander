#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "MainApp/Modding/NestedTtmp.h"

namespace XivAlexander {
	class Config;
}

namespace XivAlexander::Misc {
	class Logger;
}

namespace XivAlexander::Apps::MainApp::Window {
	class ProgressPopupWindow;
}

namespace XivAlexander::Apps::MainApp::Features::Modding {
	class TtmpLibrary {
		const std::shared_ptr<Config> m_config;
		const std::shared_ptr<Misc::Logger> m_logger;
		const std::filesystem::path m_sqpackPath;
		std::shared_ptr<NestedTtmp> m_root;

	public:
		using Reservations = std::map<std::string, std::vector<std::pair<xivres::path_spec, uint32_t>>>;

		explicit TtmpLibrary(std::filesystem::path sqpackPath);
		~TtmpLibrary();

		[[nodiscard]] const std::shared_ptr<NestedTtmp>& Root() const { return m_root; }

		void Scan(Window::ProgressPopupWindow& progressWindow);
		void Rescan(Window::ProgressPopupWindow& progressWindow);
		[[nodiscard]] Reservations CollectReservations(Window::ProgressPopupWindow& progressWindow) const;

		NestedTtmp* Add(const std::filesystem::path& ttmplPath, Window::ProgressPopupWindow& progressWindow);
		void Delete(const std::filesystem::path& ttmplPath);
		void ReconcileFiles();
		void SaveChoices(NestedTtmp& ttmp) const;
		void ReloadChoices();

	private:
		[[nodiscard]] std::vector<std::filesystem::path> GetPossibleTtmpDirs() const;
		[[nodiscard]] std::string ResolveChoicesFileName() const;
		[[nodiscard]] bool IsDisabled(const std::filesystem::path& dir) const;

		void RescanTree(const std::filesystem::path& path, std::shared_ptr<NestedTtmp> parent, Window::ProgressPopupWindow& progressWindow);
		std::shared_ptr<NestedTtmp> AddFromTtmpl(const std::filesystem::path& ttmplPath, const std::shared_ptr<NestedTtmp>& parent);
		std::shared_ptr<NestedTtmp> FindContainer(const std::filesystem::path& ttmpl, bool create);
		void ReconcileFiles(NestedTtmp& nestedTtmp);
	};
}
