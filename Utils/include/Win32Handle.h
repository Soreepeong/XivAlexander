#pragma once

#include "Utils.h"

namespace Utils {
	template <typename T = HANDLE, BOOL(WINAPI * CloserFunction)(T) = CloseHandle>
	class Win32Handle {
		T m_hHandle;

	public:
		Win32Handle() : m_hHandle(nullptr) {}

		explicit Win32Handle(T handle) :
			m_hHandle(handle) {
			if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
				throw std::exception(FormatString("Handle error: %s", FormatWindowsErrorMessage().c_str()).c_str());
		}

		Win32Handle(Win32Handle<T, CloserFunction>&& r) noexcept {
			m_hHandle = r.m_hHandle;
			r.m_hHandle = nullptr;
		}

		Win32Handle<T, CloserFunction>& operator = (Win32Handle<T, CloserFunction>&& r) noexcept {
			m_hHandle = r.m_hHandle;
			r.m_hHandle = nullptr;
			return *this;
		}

		Win32Handle<T, CloserFunction>& operator = (HANDLE h) {
			if (m_hHandle)
				CloserFunction(m_hHandle);
			m_hHandle = h;
			return *this;
		}

		Win32Handle<T, CloserFunction>& operator = (nullptr_t) {
			if (m_hHandle)
				CloserFunction(m_hHandle);
			m_hHandle = nullptr;
			return *this;
		}

		Win32Handle<T, CloserFunction>(const Win32Handle<T, CloserFunction>&) = delete;
		Win32Handle<T, CloserFunction>& operator = (const Win32Handle<T, CloserFunction>&) = delete;

		~Win32Handle() {
			if (m_hHandle)
				CloserFunction(m_hHandle);
		}

		operator T() const {
			return m_hHandle;
		}
	};
}
