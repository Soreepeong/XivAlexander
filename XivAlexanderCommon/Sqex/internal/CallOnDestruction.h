#ifndef _XIVRES_INTERNAL_CALLONDESTRUCTION_H_
#define _XIVRES_INTERNAL_CALLONDESTRUCTION_H_

#include <functional>

namespace XivRes::Internal {
	/// \brief Calls a function on destruction.
	/// Used in places where finally is required (when using C-style functions)
	class CallOnDestruction {
	protected:
		std::function<void()> m_fn;

	public:
		CallOnDestruction() noexcept = default;
		CallOnDestruction(const CallOnDestruction&) = delete;
		CallOnDestruction& operator=(const CallOnDestruction&) = delete;

		CallOnDestruction(std::function<void()> fn)
			: m_fn(std::move(fn)) {
		}

		CallOnDestruction(CallOnDestruction&& r) noexcept {
			m_fn = std::move(r.m_fn);
			r.m_fn = nullptr;
		}

		CallOnDestruction& operator=(CallOnDestruction&& r) noexcept {
			if (m_fn)
				m_fn();
			m_fn = std::move(r.m_fn);
			r.m_fn = nullptr;
			return *this;
		}

		CallOnDestruction(std::nullptr_t) noexcept {
		}

		CallOnDestruction& operator=(std::nullptr_t) noexcept {
			Clear();
			return *this;
		}

		CallOnDestruction& operator=(std::function<void()>&& fn) noexcept {
			if (m_fn)
				m_fn();
			m_fn = std::move(fn);
			return *this;
		}

		CallOnDestruction& operator=(const std::function<void()>& fn) {
			if (m_fn)
				m_fn();
			m_fn = fn;
			return *this;
		}

		CallOnDestruction& Wrap(std::function<void(std::function<void()>)> wrapper) {
			m_fn = [fn = std::move(m_fn), wrapper = std::move(wrapper)]() {
				wrapper(fn);
			};
			return *this;
		}

		virtual ~CallOnDestruction() {
			if (m_fn)
				m_fn();
		}

		CallOnDestruction& Clear() {
			if (m_fn) {
				m_fn();
				m_fn = nullptr;
			}
			return *this;
		}

		void Cancel() {
			m_fn = nullptr;
		}

		operator bool() const {
			return !!m_fn;
		}

		class Multiple {
			std::vector<CallOnDestruction> m_list;

		public:
			Multiple() = default;
			Multiple(const Multiple&) = delete;
			Multiple(Multiple&&) = delete;
			Multiple& operator=(const Multiple&) = delete;
			Multiple& operator=(Multiple&&) = delete;

			~Multiple() {
				Clear();
			}

			Multiple& operator+=(CallOnDestruction o) {
				if (o)
					m_list.emplace_back(std::move(o));
				return *this;
			}

			Multiple& operator+=(std::function<void()> f) {
				m_list.emplace_back(f);
				return *this;
			}

			Multiple& operator+=(Multiple r) {
				m_list.insert(m_list.end(), std::make_move_iterator(r.m_list.begin()), std::make_move_iterator(r.m_list.end()));
				r.m_list.clear();
				return *this;
			}

			void Clear() {
				while (!m_list.empty()) {
					m_list.back().Clear();
					m_list.pop_back();
				}
			}
		};
	};

	template<typename T>
	class CallOnDestructionWithValue final : public CallOnDestruction {
		T m_value;

	public:
		CallOnDestructionWithValue(T value, std::function<void()> fn) noexcept
			: CallOnDestruction(fn)
			, m_value(std::move(value)) {
		}

		template<typename = std::enable_if_t<std::is_default_constructible_v<T>>>
		CallOnDestructionWithValue() noexcept
			: CallOnDestruction(nullptr)
			, m_value{} {
		}

		template<typename = std::enable_if_t<std::is_default_constructible_v<T>>>
		CallOnDestructionWithValue(CallOnDestructionWithValue&& r) noexcept
			: CallOnDestruction(std::move(r))
			, m_value(std::move(r.m_value)) {
			if constexpr (std::is_trivial_v<T>)
				r.m_value = {};
		}

		template<typename = std::enable_if_t<std::is_default_constructible_v<T>>>
		CallOnDestructionWithValue& operator=(CallOnDestructionWithValue&& r) noexcept {
			m_value = std::move(r.m_value);
			CallOnDestruction::operator=(std::move(r));
			if constexpr (std::is_trivial_v<T>)
				r.m_value = {};
			return *this;
		}

		template<typename = std::enable_if_t<std::is_default_constructible_v<T>>>
		CallOnDestructionWithValue(std::nullptr_t) noexcept
			: CallOnDestruction() {
		}

		template<typename = std::enable_if_t<std::is_default_constructible_v<T>>>
		CallOnDestructionWithValue& operator=(std::nullptr_t) noexcept {
			CallOnDestruction::operator=(nullptr);
			return *this;
		}

		~CallOnDestructionWithValue() override = default;

		operator T& () {
			return m_value;
		}

		operator const T& () const {
			return m_value;
		}
	};
}

#endif
