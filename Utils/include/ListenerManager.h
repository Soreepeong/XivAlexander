#pragma once

#include "CallOnDestruction.h"

namespace Utils {

	template <typename, typename, typename, typename...>
	class _ListenerManagerImpl;

	template <typename R, typename ... T>
	class _ListenerManagerImplBase {
		template <typename, typename, typename, typename...>
		friend class _ListenerManagerImpl;

		size_t m_callbackId = 0;
		std::map<size_t, std::function<R(T ...)>> m_callbacks;
		std::list<std::shared_ptr<bool>> m_alreadyDestructedList;
		std::shared_ptr<std::mutex> m_lock = std::make_shared<std::mutex>();

	public:
		virtual ~_ListenerManagerImplBase() {
			std::lock_guard lock(*m_lock);
			m_callbacks.clear();
			for (auto& i : m_alreadyDestructedList)
				*i = true;
			m_alreadyDestructedList.clear();
		}

		/// \brief Adds a callback function to call when an event has been fired.
		/// \returns An object that will remove the callback when destructed.
		virtual CallOnDestruction operator() (const std::function<R(T ...)>& fn) {
			auto& callbacks = m_callbacks;
			auto& alreadyDestructedList = m_alreadyDestructedList;
			std::lock_guard lock(*m_lock);
			const auto callbackId = m_callbackId++;
			callbacks.emplace(callbackId, fn);

			auto indicator = std::make_shared<bool>(false);
			alreadyDestructedList.push_back(indicator);
			return CallOnDestruction([fn, indicator = std::move(indicator), mutex = m_lock, callbackId, &callbacks, &alreadyDestructedList]() {
				std::lock_guard lock(*mutex);

				// already destructed?
				if (*indicator)
					return;

				callbacks.erase(callbackId);
				alreadyDestructedList.erase(std::find(alreadyDestructedList.begin(), alreadyDestructedList.end(), indicator));
			});
		}

	protected:

		/// \brief Fires an event.
		/// \returns Number of callbacks called.
		virtual size_t operator() (T ... t) {
			std::vector<std::function<R(T ...)>> callbacks;
			{
				std::lock_guard lock(*m_lock);
				for (const auto& cbp : m_callbacks)
					callbacks.push_back(cbp.second);
			}

			size_t notified = 0;
			for (const auto& cb : callbacks) {
				cb(std::forward<T>(t)...);
				notified++;
			}
			return notified;
		}
	};

	template <typename F, typename R, typename ... T>
	class _ListenerManagerImpl<F, R, std::enable_if_t<!std::is_same_v<R, void>>, T...> :
		public _ListenerManagerImplBase<R, T...> {

		friend F;

	public:

		/// \brief Adds a callback function to call when an event has been fired.
		/// \returns An object that will remove the callback when destructed.
		CallOnDestruction operator() (const std::function<R(T ...)>& fn) override {
			return _ListenerManagerImplBase<R, T...>::operator()(fn);
		}

	protected:

		/// \brief Fires an event.
		/// \returns Number of callbacks called.
		size_t operator() (T ... t) override {
			return _ListenerManagerImplBase<R, T...>::operator()(std::forward<T>(t)...);
		}

		/// \brief Fires an event.
		/// \returns Number of callbacks called.
		size_t operator() (T ... t, const std::function<bool(size_t, R)>& stopNotifying) {
			std::vector<std::function<R(T ...)>> callbacks;
			{
				std::lock_guard<std::mutex> lock(*this->m_lock);
				for (const auto& cbp : this->m_callbacks)
					callbacks.emplace(cbp.second);
			}

			size_t notified = 0;
			for (const auto& cb : callbacks) {
				const auto r = cb(std::forward<T>(t)...);
				if (stopNotifying && stopNotifying(notified, r))
					return;
				notified++;
			}
			return notified;
		}
	};

	template <typename F, typename R, typename...T>
	class _ListenerManagerImpl<F, R, std::enable_if_t<std::is_same_v<R, void>>, T...> :
		public _ListenerManagerImplBase<R, T...> {

		friend F;

	public:

		/// \brief Adds a callback function to call when an event has been fired.
		/// \returns An object that will remove the callback when destructed.
		CallOnDestruction operator() (const std::function<R(T ...)>& fn) override {
			return _ListenerManagerImplBase<R, T...>::operator()(fn);
		}

	protected:
		/// \brief Fires an event.
		/// \returns Number of callbacks called.
		size_t operator() (T ... t) override {
			return _ListenerManagerImplBase<R, T...>::operator()(std::forward<T>(t)...);
		}
	};

	/// Event callback management class.
	template <typename F, typename R, typename...T>
	class ListenerManager : public _ListenerManagerImpl<F, R, void, T...> {};

}
