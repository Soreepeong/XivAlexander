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
			std::function<TResult(size_t)> Function;
		};

		bool m_bQuitting = false;
		bool m_bNoMoreTasks = false;

		bool m_bErrorOccurred = false;
		std::string m_sFirstError;

		size_t m_nThreads;
		std::vector<std::thread> m_threads;
		std::vector<std::function<void(size_t)>> m_onNewThreadCallbacks;

		std::mutex m_queuedTaskLock;
		std::condition_variable m_queuedTaskAvailable;
		std::deque<Task> m_queuedTasks;

		std::mutex m_finishedTaskLock;
		std::condition_variable m_finishedTaskAvailable;
		std::deque<std::pair<TIdentifier, TResult>> m_finishedTasks;

		class InternalCancelledError : public std::exception {
		public:
			InternalCancelledError() = default;
		};

	public:
		ThreadPool(size_t nThreads = SIZE_MAX)
			: m_nThreads(nThreads == SIZE_MAX ? std::thread::hardware_concurrency() : nThreads) {
			m_threads.reserve(m_nThreads);
		}

		~ThreadPool() {
			if (!IsAnyWorkerThreadRunning())
				return;

			if (!m_bErrorOccurred) {
				m_bErrorOccurred = true;
				m_sFirstError = "Cancelled via dtor";
			}

			{
				const auto lock = std::lock_guard(m_queuedTaskLock);
				m_bNoMoreTasks = true;
				for (const auto& _ : m_threads)
					m_queuedTasks.emplace_back();
			}

			m_queuedTaskAvailable.notify_all();
			for (auto& th : m_threads)
				if (th.joinable())
					th.join();
		}

		void AddOnNewThreadCallback(std::function<void(size_t)> cb) {
			EnsureNotQuitting();
			if (IsAnyWorkerThreadRunning())
				throw std::runtime_error("AddOnNewThreadCallback must be called before submitting a task.");

			m_onNewThreadCallbacks.emplace_back(std::move(cb));
		}

		bool ErrorOccurred() const {
			return m_bErrorOccurred;
		}

		const std::string& GetFirstError() const {
			return m_sFirstError;
		}

		void AbortIfErrorOccurred() const {
			if (m_bErrorOccurred)
				throw InternalCancelledError();
		}

		void PropagateInnerErrorIfErrorOccurred() const {
			if (m_bErrorOccurred)
				throw PropagatedError(m_sFirstError);
		}

		bool IsAnyWorkerThreadRunning() const {
			for (auto& th : m_threads)
				if (th.joinable())
					return true;

			return false;
		}

		void Submit(std::function<TResult(size_t)> fn) {
			return Submit(Task{
				std::nullopt,
				std::move(fn),
			});
		}

		void Submit(std::function<void(size_t)> fn) {
			return Submit(Task{
				std::nullopt,
				[fn = std::move(fn), this](size_t nThreadIndex){ fn(nThreadIndex); return TResult(); },
			});
		}

		void Submit(TIdentifier identifier, std::function<TResult(size_t)> fn) {
			return Submit(Task{
				std::move(identifier),
				std::move(fn),
			});
		}

		void Submit(std::function<TResult()> fn) {
			return Submit(Task{
				std::nullopt,
				[fn = std::move(fn), this](size_t){ return fn(); },
			});
		}

		void Submit(std::function<void()> fn) {
			return Submit(Task{
				std::nullopt,
				[fn = std::move(fn), this](size_t){ fn(); return TResult(); },
			});
		}

		void Submit(TIdentifier identifier, std::function<TResult()> fn) {
			return Submit(Task{
				std::move(identifier),
				[fn = std::move(fn), this](size_t){ return fn(); },
			});
		}

		void SubmitDone(std::string finishingWithError = {}) {
			PropagateInnerErrorIfErrorOccurred();

			if (m_bNoMoreTasks)
				return;

			{
				const auto lock = std::lock_guard(m_queuedTaskLock);
				if (m_bErrorOccurred)
					return;

				if (!finishingWithError.empty()) {
					m_sFirstError = finishingWithError;
					m_bErrorOccurred = true;
				}

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

			PropagateInnerErrorIfErrorOccurred();
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
		void EnsureNotQuitting() const {
			PropagateInnerErrorIfErrorOccurred();

			if (m_bNoMoreTasks)
				throw std::runtime_error("Marked as no more tasks will be forthcoming.");
		}

		void Submit(Task task) {
			if (!task.Function)
				throw std::invalid_argument("Function must be specified");

			EnsureNotQuitting();

			{
				const auto lock = std::lock_guard(m_queuedTaskLock);
				m_queuedTasks.emplace_back(std::move(task));
			}

			m_queuedTaskAvailable.notify_one();

			if (m_threads.size() >= m_nThreads)
				return;

			m_threads.emplace_back([this, nThreadIndex = m_threads.size()]() {
				std::optional<TResult> result;

				try {
					for (const auto& cb : m_onNewThreadCallbacks)
						cb(nThreadIndex);

				} catch (const InternalCancelledError&) {
					return;

				} catch (const std::exception& e) {
					SubmitDone(std::format("OnNewThreadCallbacks: {}", e.what()));
					return;
				}

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

					try {
						result = task.Function(nThreadIndex);

					} catch (const InternalCancelledError&) {
						return;

					} catch (const std::exception& e) {
						SubmitDone(std::format("Task: {}", e.what()));
						return;
					}

					if (!task.Identifier.has_value())
						continue;

					{
						const auto lock = std::lock_guard(m_finishedTaskLock);
						m_finishedTasks.emplace_back(std::move(*task.Identifier), std::move(*result));
					}

					m_finishedTaskAvailable.notify_one();
					result.reset();
				}
			});
		}

	public:
		class PropagatedError : public std::runtime_error {
		public:
			using std::runtime_error::runtime_error;
		};
	};
}

#endif
