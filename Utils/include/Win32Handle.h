#pragma once

#include "Utils.h"

namespace Utils {

	static constexpr auto NullHandle = static_cast<HANDLE>(nullptr);
	
	template <typename T = HANDLE, BOOL(WINAPI * CloserFunction)(T) = CloseHandle>
	class Win32Handle {
		T m_hHandle;

	public:
		Win32Handle() : m_hHandle(nullptr) {}

		Win32Handle(T handle, T invalidValue) :
			m_hHandle(handle == invalidValue ? nullptr : handle) {
		}

		Win32Handle(T handle, T invalidValue, const std::string& errorMessage) :
			m_hHandle(handle) {
			if (handle == invalidValue)
				throw WindowsError(errorMessage);
		}

		template <typename ... Args>
		Win32Handle(T handle, T invalidValue, const _Printf_format_string_ char* errorMessageFormat, Args ... args) :
			Win32Handle(handle, invalidValue, FormatString(errorMessageFormat, std::forward<Args>(args)...)) {
		}

		template <typename ... Args>
		Win32Handle(T handle, T invalidValue, const _Printf_format_string_ wchar_t* errorMessageFormat, Args ... args) :
			Win32Handle(handle, invalidValue, Utils::ToUtf8(FormatString(errorMessageFormat, std::forward<Args>(args)...))) {
		}
		
		Win32Handle(Win32Handle&& r) noexcept {
			m_hHandle = r.m_hHandle;
			r.m_hHandle = nullptr;
		}

		Win32Handle& operator = (Win32Handle&& r) noexcept {
			m_hHandle = r.m_hHandle;
			r.m_hHandle = nullptr;
			return *this;
		}

		Win32Handle& operator = (nullptr_t) {
			if (m_hHandle)
				CloserFunction(m_hHandle);
			m_hHandle = nullptr;
			return *this;
		}

		Win32Handle(const Win32Handle&) = delete;
		Win32Handle& operator = (const Win32Handle&) = delete;

		void Detach() {
			m_hHandle = nullptr;
		}

		~Win32Handle() {
			if (m_hHandle)
				CloserFunction(m_hHandle);
		}

		operator T() const {
			return m_hHandle;
		}
	};
}
