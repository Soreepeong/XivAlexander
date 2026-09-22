#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <xivres/sqpack.generator.h>

#include "Utils/Win32/Handle.h"

namespace XivAlexander::Misc {
	class Logger;
}

namespace XivAlexander::Apps::MainApp::Window {
	class ProgressPopupWindow;
}

namespace XivAlexander::Apps::MainApp::Features::Modding {
	struct Pack {
		enum class BuildState {
			Queued,
			Building,
			Built,
			Failed,
		};

		std::filesystem::path IndexPath;
		std::string Key;
		std::unique_ptr<xivres::sqpack::generator> Creator;
		std::optional<xivres::sqpack::generator::sqpack_views> Views;
		std::atomic<BuildState> State = BuildState::Queued;
	};

	class PackQueue {
	public:
		using BuildFn = std::function<void(Pack& pack)>;

		class ApplyLock {
			std::unique_lock<std::recursive_mutex> m_lock;

		public:
			explicit ApplyLock(const PackQueue& queue);
			ApplyLock(const ApplyLock&) = delete;
			ApplyLock& operator=(const ApplyLock&) = delete;
			~ApplyLock();
		};

	private:
		const std::shared_ptr<Misc::Logger> m_logger;
		const std::filesystem::path m_sqpackPath;

		mutable std::map<std::filesystem::path, Pack> m_packs;
		mutable std::mutex m_buildMtx;
		mutable std::condition_variable m_buildCv;
		mutable std::deque<Pack*> m_buildQueue;
		bool m_stopBuilding = false;
		std::vector<Utils::Win32::Thread> m_builders;
		BuildFn m_build;

		mutable std::recursive_mutex m_applyMtx;

	public:
		explicit PackQueue(std::filesystem::path sqpackPath);
		~PackQueue();

		void Discover(Window::ProgressPopupWindow& progressWindow);

		void Start(size_t builderCount, BuildFn build);

		void Stop();

		[[nodiscard]] std::map<std::filesystem::path, Pack>& All() const { return m_packs; }
		[[nodiscard]] Pack* Find(const std::filesystem::path& indexPath) const;
		[[nodiscard]] Pack* Find(const xivres::path_spec& pathSpec) const;

		const xivres::sqpack::generator::sqpack_views* EnsureBuilt(Pack& pack) const;
		void EnsureAllBuilt() const;

		[[nodiscard]] std::set<const Pack*> Built() const;
		[[nodiscard]] std::unique_lock<std::recursive_mutex> Lock() const;

	private:
		void RunBuild(Pack& pack) const;
	};
}
