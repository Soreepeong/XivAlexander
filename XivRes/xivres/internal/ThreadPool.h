#ifndef _XIVRES_INTERNAL_THREADPOOL_H_
#define _XIVRES_INTERNAL_THREADPOOL_H_

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>

namespace XivRes::Internal {
	template<typename TIdentifier = void*, typename TResult = void*, typename = std::enable_if_t<std::is_move_assignable_v<TIdentifier>&& std::is_move_assignable_v<TResult>>>
	class ThreadPool {
		struct Task {
			std::optional<TIdentifier> Identifier;
			std::function<TResult()> Function;
		};

		bool m_bQuitting = false;
		bool m_bNoMoreTasks = false;
		size_t m_nThreads;
		std::vector<std::thread> m_threads;

		std::mutex m_queuedTaskLock;
		std::condition_variable m_queuedTaskAvailable;
		std::deque<Task> m_queuedTasks;

		std::mutex m_finishedTaskLock;
		std::condition_variable m_finishedTaskAvailable;
		std::deque<std::pair<TIdentifier, TResult>> m_finishedTasks;

	public:
		ThreadPool(size_t nThreads)
			: m_nThreads(nThreads) {
			m_threads.reserve(m_nThreads);
		}

		~ThreadPool() {
			SubmitDoneAndWait();
		}

		bool IsAnyWorkerThreadRunning() const {
			for (auto& th : m_threads)
				if (th.joinable())
					return true;

			return false;
		}

		void Submit(std::function<TResult()> fn) {
			return Submit(Task{ std::nullopt, std::move(fn) });
		}

		void Submit(std::function<void()> fn) {
			return Submit(Task{ std::nullopt, [fn = std::move(fn), this](){ fn(); return TResult(); } });
		}

		void Submit(TIdentifier identifier, std::function<TResult()> fn) {
			return Submit(Task{ std::move(identifier), std::move(fn) });
		}

		void SubmitDone() {
			if (m_bNoMoreTasks)
				return;

			{
				const auto lock = std::lock_guard(m_queuedTaskLock);
				m_bNoMoreTasks = true;
				for (const auto& _ : m_threads)
					m_queuedTasks.emplace_back();
			}

			m_queuedTaskAvailable.notify_all();
		}

		void SubmitDoneAndWait() {
			SubmitDone();

			for (auto& th : m_threads)
				if (th.joinable())
					th.join();
		}

		template<class Rep, class Period>
		std::optional<std::pair<TIdentifier, TResult>> GetResult() {
			auto lock = std::unique_lock(m_finishedTaskLock);
			m_finishedTaskAvailable.wait(lock, [this] { return !IsAnyWorkerThreadRunning() || !m_finishedTasks.empty(); });
			if (m_finishedTasks.empty())
				return std::nullopt;

			auto res = std::move(m_finishedTasks.front());
			m_finishedTasks.pop_front();
			return res;
		}

		template<class Rep, class Period>
		std::optional<std::pair<TIdentifier, TResult>> GetResult(const std::chrono::duration<Rep, Period>& rel_time) {
			auto lock = std::unique_lock(m_finishedTaskLock);
			if (!m_finishedTaskAvailable.wait_for(lock, rel_time, [this] { return !IsAnyWorkerThreadRunning() || !m_finishedTasks.empty(); }) || m_finishedTasks.empty())
				return std::nullopt;

			auto res = std::move(m_finishedTasks.front());
			m_finishedTasks.pop_front();
			return res;
		}

		template<class Clock, class Duration>
		std::optional<std::pair<TIdentifier, TResult>> GetResult(const std::chrono::time_point<Clock, Duration>& timeout_time) {
			auto lock = std::unique_lock(m_finishedTaskLock);
			if (!m_finishedTaskAvailable.wait_until(lock, timeout_time, [this] { return !IsAnyWorkerThreadRunning() || !m_finishedTasks.empty(); }) || m_finishedTasks.empty())
				return std::nullopt;

			auto res = std::move(m_finishedTasks.front());
			m_finishedTasks.pop_front();
			return res;
		}

	private:
		void Submit(Task task) {
			if (!task.Function)
				throw std::invalid_argument("Function must be specified");
			if (m_bNoMoreTasks)
				throw std::runtime_error("Marked as no more tasks will be forthcoming.");

			{
				const auto lock = std::lock_guard(m_queuedTaskLock);
				m_queuedTasks.emplace_back(std::move(task));
			}

			m_queuedTaskAvailable.notify_one();

			if (m_threads.size() >= m_nThreads)
				return;

			m_threads.emplace_back([this]() {
				while (!m_bQuitting) {
					Task task;

					{
						auto lock = std::unique_lock(m_queuedTaskLock);
						m_queuedTaskAvailable.wait(lock, [this] { return !m_queuedTasks.empty() || m_bNoMoreTasks; });
						if (m_queuedTasks.empty())
							return;

						task = std::move(m_queuedTasks.front());
						m_queuedTasks.pop_front();
					}

					if (!task.Function)
						return;

					auto result = task.Function();
					if (!task.Identifier.has_value())
						continue;

					{
						const auto lock = std::lock_guard(m_finishedTaskLock);
						m_finishedTasks.emplace_back(std::move(*task.Identifier), std::move(result));
					}

					m_finishedTaskAvailable.notify_one();
				}
			});
		}
	};
}

#endif
