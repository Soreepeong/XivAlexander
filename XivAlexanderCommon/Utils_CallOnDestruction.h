#pragma once

#include <functional>

namespace Utils {
	/// \brief Calls a function on destruction.
	/// Used in places where finally is required (when using C-style functions)
	class CallOnDestruction {
	protected:
		std::function<void()> m_fn;

	public:
		CallOnDestruction() noexcept;
		CallOnDestruction(std::function<void()> fn);
		CallOnDestruction(const CallOnDestruction&) = delete;
		CallOnDestruction& operator=(const CallOnDestruction&) = delete;

		CallOnDestruction(CallOnDestruction&& r) noexcept;
		CallOnDestruction& operator=(CallOnDestruction&& r) noexcept;

		CallOnDestruction(std::nullptr_t) noexcept;
		CallOnDestruction& operator=(std::nullptr_t) noexcept;
		
		CallOnDestruction& operator=(std::function<void()>&& fn) noexcept;
		CallOnDestruction& operator=(const std::function<void()>& fn);

		CallOnDestruction& Wrap(std::function<void(std::function<void()>)>);

		virtual ~CallOnDestruction();

		CallOnDestruction& Clear();

		operator bool() const;

		class Multiple {
			std::vector<CallOnDestruction> m_list;
			
		public:
			Multiple();

			~Multiple();

			Multiple& operator+=(CallOnDestruction o);

			Multiple& operator+=(std::function<void()> f);

			Multiple& operator+=(Multiple r);

			void Clear();
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
		CallOnDestructionWithValue(std::nullptr_t) noexcept : CallOnDestruction() {}
		
		template<typename = std::enable_if_t<std::is_default_constructible_v<T>>>
		CallOnDestructionWithValue& operator=(std::nullptr_t) noexcept {
			CallOnDestruction::operator=(nullptr);
			return *this;
		}
		
		~CallOnDestructionWithValue() override = default;

		operator T&() {
			return m_value;
		}

		operator const T&() const {
			return m_value;
		}
	};
}
