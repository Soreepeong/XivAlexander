#include "pch.h"
#include "MainApp/Modding/PackQueue.h"

#include <xivres/util.on_dtor.h>

#include "MainApp/Modding/FutureReservations.h"
#include "MainApp/Windows/ProgressPopupWindow.h"
#include "Misc/Logger.h"

namespace XivAlexander::Apps::MainApp::Features::Modding {
	namespace {
		thread_local bool t_isBuilder = false;
		thread_local const Pack* t_building = nullptr;
		thread_local size_t t_applying = 0;
	}

	PackQueue::ApplyLock::ApplyLock(const PackQueue& queue)
		: m_lock(queue.m_applyMtx) {
		++t_applying;
	}

	PackQueue::ApplyLock::~ApplyLock() {
		--t_applying;
	}

	PackQueue::PackQueue(std::filesystem::path sqpackPath)
		: m_logger(Misc::Logger::Acquire())
		, m_sqpackPath(std::move(sqpackPath)) {}

	PackQueue::~PackQueue() {
		Stop();
	}

	void PackQueue::Discover(Window::ProgressPopupWindow& progressWindow) {
		for (const auto& expac : std::filesystem::directory_iterator(m_sqpackPath)) {
			if (!expac.is_directory())
				continue;

			for (const auto& sqpack : std::filesystem::directory_iterator(expac)) {
				if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
					throw std::runtime_error("Cancelled");

				auto ext = sqpack.path().extension().wstring();
				CharLowerW(ext.data());
				if (ext != L".index2")
					continue;

				auto& pack = m_packs[sqpack.path()];
				pack.IndexPath = sqpack.path();
				pack.Creator = std::make_unique<xivres::sqpack::generator>(
					xivres::util::unicode::convert<std::string>(expac.path().filename().wstring()),
					xivres::util::unicode::convert<std::string>(sqpack.path().filename().replace_extension().replace_extension().wstring())
				);
				pack.Key = FutureReservations::PackKey(*pack.Creator);
			}
		}
	}

	void PackQueue::Start(size_t builderCount, BuildFn build) {
		m_build = std::move(build);

		std::vector<Pack*> queue;
		for (auto& pack : m_packs | std::views::values)
			queue.emplace_back(&pack);

		std::ranges::stable_partition(queue, [](const Pack* pack) { return pack->Key.starts_with("ffxiv/"); });

		{
			const auto lock = std::lock_guard(m_buildMtx);
			m_buildQueue.assign(queue.begin(), queue.end());
		}

		m_builders.reserve(builderCount);
		for (size_t i = 0; i < builderCount; i++) {
			m_builders.emplace_back(std::format(L"VirtualSqPacks Builder {}", i), [this] {
				t_isBuilder = true;
				while (true) {
					Pack* pack;
					{
						std::unique_lock lock(m_buildMtx);
						m_buildCv.wait(lock, [this] { return m_stopBuilding || !m_buildQueue.empty(); });
						if (m_stopBuilding)
							return;
						pack = m_buildQueue.front();
						m_buildQueue.pop_front();
						pack->State = Pack::BuildState::Building;
					}
					RunBuild(*pack);
				}
			});
		}
	}

	void PackQueue::Stop() {
		{
			const auto lock = std::lock_guard(m_buildMtx);
			m_stopBuilding = true;
			m_buildQueue.clear();
		}

		m_buildCv.notify_all();
		for (const auto& builder : m_builders)
			builder.Wait();
		m_builders.clear();
	}

	Pack* PackQueue::Find(const std::filesystem::path& indexPath) const {
		const auto it = m_packs.find(indexPath);
		return it == m_packs.end() ? nullptr : &it->second;
	}

	Pack* PackQueue::Find(const xivres::path_spec& pathSpec) const {
		try {
			return Find(m_sqpackPath / std::format(L"{}/{}.win32.index2", xivres::util::unicode::convert<std::wstring>(pathSpec.exname()), xivres::util::unicode::convert<std::wstring>(pathSpec.packname())));
		} catch (...) {
			return nullptr;
		}
	}

	const xivres::sqpack::generator::sqpack_views* PackQueue::EnsureBuilt(Pack& pack) const {
		if (pack.State == Pack::BuildState::Built)
			return &*pack.Views;
		if (t_building == &pack)
			return pack.Views ? &*pack.Views : nullptr;
		if (t_applying)
			return nullptr;

		std::unique_lock lock(m_buildMtx);
		if (pack.State == Pack::BuildState::Queued) {
			if (const auto it = std::ranges::find(m_buildQueue, &pack); it != m_buildQueue.end())
				m_buildQueue.erase(it);

			if (t_isBuilder) {
				pack.State = Pack::BuildState::Building;
				lock.unlock();
				RunBuild(pack);
				return pack.State == Pack::BuildState::Built ? &*pack.Views : nullptr;
			}

			m_logger->Format<LogLevel::Debug>(LogCategory::VirtualSqPacks, "[{}] Wanted before being built; moved to the front", pack.Key);
			m_buildQueue.push_front(&pack);
			m_buildCv.notify_all();
		}

		m_buildCv.wait(lock, [&] { return pack.State == Pack::BuildState::Built || pack.State == Pack::BuildState::Failed || m_stopBuilding; });
		return pack.State == Pack::BuildState::Built ? &*pack.Views : nullptr;
	}

	void PackQueue::EnsureAllBuilt() const {
		for (auto& pack : m_packs | std::views::values)
			EnsureBuilt(pack);
	}

	std::set<const Pack*> PackQueue::Built() const {
		std::set<const Pack*> built;
		for (const auto& pack : m_packs | std::views::values) {
			if (pack.State == Pack::BuildState::Built)
				built.emplace(&pack);
		}
		return built;
	}

	std::unique_lock<std::recursive_mutex> PackQueue::Lock() const {
		return std::unique_lock(m_applyMtx);
	}

	void PackQueue::RunBuild(Pack& pack) const {
		const auto previous = t_building;
		t_building = &pack;
		const auto restore = xivres::util::on_dtor([previous] { t_building = previous; });

		const auto start = std::chrono::steady_clock::now();
		try {
			m_build(pack);
		} catch (const std::exception& e) {
			m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "[{}] Error: {}", pack.Key, e.what());
		}

		// stall mod application
		{
			const auto applyLock = std::lock_guard(m_applyMtx);
			{
				const auto lock = std::lock_guard(m_buildMtx);
				pack.State = pack.Views ? Pack::BuildState::Built : Pack::BuildState::Failed;
			}
		}
		
		m_buildCv.notify_all();
		m_logger->Format<LogLevel::Debug>(LogCategory::VirtualSqPacks,
			"[{}] {} in {}ms",
			pack.Key, pack.Views ? "Built" : "Failed to build",
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
	}
}
