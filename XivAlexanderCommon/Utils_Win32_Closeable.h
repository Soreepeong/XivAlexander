#pragma once

#include <windef.h>
#include "Utils__String.h"

namespace Utils::Win32::Closeable {
	template <typename T, BOOL(WINAPI* CloserFunction)(T)>
	class Base {
	public:
		static constexpr T Null = reinterpret_cast<T>(nullptr);

	private:
		T m_hBase;

	public:
		Base() : m_hBase(nullptr) {}

		Base(T Base, T invalidValue) :
			m_hBase(Base == invalidValue ? nullptr : Base) {
		}

		Base(T Base, T invalidValue, const std::string& errorMessage) :
			m_hBase(Base) {
			if (Base == invalidValue)
				throw Error(errorMessage);
		}

		template <typename ... Args>
		Base(T Base, T invalidValue, const _Printf_format_string_ char* errorMessageFormat, Args ... args) :
			Base(Base, invalidValue, FormatString(errorMessageFormat, std::forward<Args>(args)...)) {
		}

		template <typename ... Args>
		Base(T Base, T invalidValue, const _Printf_format_string_ wchar_t* errorMessageFormat, Args ... args) :
			Base(Base, invalidValue, Utils::ToUtf8(FormatString(errorMessageFormat, std::forward<Args>(args)...))) {
		}

		Base(Base&& r) noexcept {
			m_hBase = r.m_hBase;
			r.m_hBase = nullptr;
		}

		Base& operator = (Base&& r) noexcept {
			m_hBase = r.m_hBase;
			r.m_hBase = nullptr;
			return *this;
		}

		Base& operator = (nullptr_t) {
			if (m_hBase)
				CloserFunction(m_hBase);
			m_hBase = nullptr;
			return *this;
		}

		Base(const Base&) = delete;
		Base& operator = (const Base&) = delete;

		void Detach() {
			m_hBase = nullptr;
		}

		~Base() {
			if (m_hBase)
				CloserFunction(m_hBase);
		}

		operator T() const {
			return m_hBase;
		}
	};

	using Handle = Base<HANDLE, CloseHandle>;
	using Icon = Base<HICON, DestroyIcon>;
	using GlobalResource = Base<HGLOBAL, FreeResource>;
	using CreatedDC = Base<HDC, DeleteDC>;
	using LoadedModule = Base<HMODULE, FreeLibrary>;
	using FindFile = Base<HANDLE, FindClose>;
}
