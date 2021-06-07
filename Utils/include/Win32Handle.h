#pragma once

#include "Utils.h"

#include <system_error>

namespace Utils {

	static constexpr HANDLE NullHandle = static_cast<HANDLE>(nullptr);
	
	template <typename T = HANDLE, BOOL(WINAPI * CloserFunction)(T) = CloseHandle>
	class Win32Handle {
		T m_hHandle;

	public:
		Win32Handle() : m_hHandle(nullptr) {}

		Win32Handle(T handle, T invalidValue) :
			m_hHandle(handle == invalidValue ? nullptr : handle) {
		}

		template <typename C>
		Win32Handle(T handle, T invalidValue, C errorMessage) :
			m_hHandle(handle) {
			static_assert(std::is_same_v<std::remove_cv_t<C>, char*>
				|| std::is_same_v<std::remove_cv_t<C>, wchar_t*>
				|| std::is_same_v<std::remove_reference_t<std::remove_cv_t<C>>, std::string>
				|| std::is_same_v<std::remove_reference_t<std::remove_cv_t<C>>, std::wstring>);
			if (handle == invalidValue)
				ThrowFromWinLastError(errorMessage);
		}

		template <typename C, typename ... Args>
		Win32Handle(T handle, T invalidValue, const _Printf_format_string_ C* errorMessageFormat, Args ... args) :
			m_hHandle(handle) {
			static_assert(std::is_same_v<C, char> || std::is_same_v<C, wchar_t>);
			if (handle == invalidValue)
				ThrowFromWinLastError(FormatString(errorMessageFormat, std::forward<Args>(args)...));
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
