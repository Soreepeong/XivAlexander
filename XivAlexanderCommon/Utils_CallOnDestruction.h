#pragma once

#include <functional>

namespace Utils {
	/// \brief Calls a function on destruction.
	/// Used in places where finally is required (when using C-style functions)
	class CallOnDestruction {
		std::function<void()> m_fn;

	public:
		CallOnDestruction() noexcept;
		explicit CallOnDestruction(std::function<void()> fn);
		CallOnDestruction(const CallOnDestruction&) = delete;
		CallOnDestruction& operator =(const CallOnDestruction&) = delete;
		CallOnDestruction(CallOnDestruction&& r) noexcept;
		CallOnDestruction& operator =(CallOnDestruction&& r) noexcept;
		CallOnDestruction(std::nullptr_t) noexcept;
		CallOnDestruction& operator =(std::nullptr_t) noexcept;

		~CallOnDestruction();

		operator bool() const;
	};
}
