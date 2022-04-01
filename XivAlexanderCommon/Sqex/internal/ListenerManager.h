#ifndef _XIVRES_INTERNAL_LISTENERMANAGER_H_
#define _XIVRES_INTERNAL_LISTENERMANAGER_H_

#include <functional>
#include <map>
#include <memory>
#include <mutex>

#include "CallOnDestruction.h"

namespace XivRes::Internal {

	template <typename, typename, typename, typename...>
	class ListenerManagerImpl_;

	template <typename R, typename ... T>
	class ListenerManagerImplBase_ {
		template <typename, typename, typename, typename...>
		friend class ListenerManagerImpl_;

		typedef std::function<R(T ...)> CallbackType;

		size_t m_callbackId = 0;
		std::map<size_t, std::function<R(T ...)>> m_callbacks;
		std::shared_ptr<std::mutex> m_lock = std::make_shared<std::mutex>();
		std::shared_ptr<bool> m_destructed = std::make_shared<bool>(false);

		const std::function<void(const CallbackType&)> m_onNewCallback;

	public:
		ListenerManagerImplBase_(std::function<void(const CallbackType&)> onNewCallback = nullptr)
			: m_onNewCallback(onNewCallback) {
		}

		virtual ~ListenerManagerImplBase_() {
			std::lock_guard lock(*m_lock);
			m_callbacks.clear();
			*m_destructed = true;
		}

		bool Empty() const {
			return m_callbacks.empty();
		}

		/// \brief Adds a callback function to call when an event has been fired.
		/// \returns An object that will remove the callback when destructed.
		[[nodiscard]] virtual CallOnDestruction operator() (std::function<R(T ...)> fn, std::function<void()> onUnbind = {}) {
			std::lock_guard lock(*m_lock);
			const auto callbackId = m_callbackId++;
			if (m_onNewCallback)
				m_onNewCallback(fn);
			m_callbacks.emplace(callbackId, std::move(fn));

			return CallOnDestruction([destructed = m_destructed, onUnbind = std::move(onUnbind), mutex = m_lock, callbackId, this]() {
				std::lock_guard lock(*mutex);

				if (!*destructed)
					m_callbacks.erase(callbackId);

				if (onUnbind)
					onUnbind();
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
	class ListenerManagerImpl_<F, R, std::enable_if_t<!std::is_same_v<R, void>>, T...> :
		public ListenerManagerImplBase_<R, T...> {

		friend F;

	public:
		using ListenerManagerImplBase_<R, T...>::ListenerManagerImplBase_;

		/// \brief Adds a callback function to call when an event has been fired.
		/// \returns An object that will remove the callback when destructed.
		[[nodiscard]] CallOnDestruction operator() (std::function<R(T ...)> fn, std::function<void()> onUnbind = {}) override {
			return ListenerManagerImplBase_<R, T...>::operator()(std::move(fn), std::move(onUnbind));
		}

	protected:

		/// \brief Fires an event.
		/// \returns Number of callbacks called.
		size_t operator() (T ... t) override {
			return ListenerManagerImplBase_<R, T...>::operator()(std::forward<T>(t)...);
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
					break;
				notified++;
			}
			return notified;
		}
	};

	template <typename F, typename R, typename...T>
	class ListenerManagerImpl_<F, R, std::enable_if_t<std::is_same_v<R, void>>, T...> :
		public ListenerManagerImplBase_<R, T...> {

		friend F;

	public:
		using ListenerManagerImplBase_<R, T...>::ListenerManagerImplBase_;

		/// \brief Adds a callback function to call when an event has been fired.
		/// \returns An object that will remove the callback when destructed.
		[[nodiscard]] CallOnDestruction operator() (std::function<R(T ...)> fn, std::function<void()> onUnbind = {}) override {
			return ListenerManagerImplBase_<R, T...>::operator()(std::move(fn), std::move(onUnbind));
		}

	protected:
		/// \brief Fires an event.
		/// \returns Number of callbacks called.
		size_t operator() (T ... t) override {
			return ListenerManagerImplBase_<R, T...>::operator()(std::forward<T>(t)...);
		}
	};

	/// Event callback management class.
	template <typename F, typename R, typename...T>
	class ListenerManager : public ListenerManagerImpl_<F, R, void, T...> {
	public:
		using ListenerManagerImpl_<F, R, void, T...>::ListenerManagerImpl_;
	};

}

#endif
