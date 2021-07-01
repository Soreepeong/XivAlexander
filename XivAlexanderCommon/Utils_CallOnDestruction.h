#pragma once

#include <functional>

namespace Utils {
	/// \brief Calls a function on destruction.
	/// Used in places where finally is required (when using C-style functions)
	class CallOnDestruction {
		std::function<void()> m_fn;

	public:
		CallOnDestruction() noexcept;
		CallOnDestruction(std::function<void()> fn);
		CallOnDestruction(const CallOnDestruction&) = delete;
		CallOnDestruction& operator =(const CallOnDestruction&) = delete;

		CallOnDestruction(CallOnDestruction&& r) noexcept;
		CallOnDestruction& operator =(CallOnDestruction&& r) noexcept;

		CallOnDestruction(std::nullptr_t) noexcept;
		CallOnDestruction& operator =(std::nullptr_t) noexcept;

		CallOnDestruction& Wrap(std::function<void(std::function<void()>)>);

		~CallOnDestruction();

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
}
