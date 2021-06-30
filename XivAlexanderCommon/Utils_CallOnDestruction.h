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
				while (!m_list.empty())
					m_list.pop_back();
			}

			~Multiple() {
				Clear();
			}
		};
	};
}
