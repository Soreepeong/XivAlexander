#pragma once

#include <span>
#include <functional>

#include <windef.h>

#include "Utils_Win32.h"
#include "Utils__Misc.h"
#include "Utils__String.h"

namespace Utils::Win32::Closeable {
	template <typename T, BOOL(WINAPI* CloserFunction)(T)>
	class Base {
	public:
		static constexpr T Null = reinterpret_cast<T>(nullptr);

	protected:
		T m_object = Null;
		bool m_bOwnership = true;

	public:
		Base() {}

		Base(T Base, bool ownership = false)
			: m_object(Base)
			, m_bOwnership(ownership) {
		}

		Base(T Base, T invalidValue)
			: m_object(Base == invalidValue ? nullptr : Base) {
		}

		Base(T Base, T invalidValue, const std::string& errorMessage)
			: m_object(Base) {
			if (Base == invalidValue)
				throw Error(errorMessage);
		}

		template <typename ... Args>
		Base(T Base, T invalidValue, const char* errorMessageFormat, Args ... args)
			: Base(Base, invalidValue, std::format(errorMessageFormat, std::forward<Args>(args)...)) {
		}

		template <typename ... Args>
		Base(T Base, T invalidValue, const wchar_t* errorMessageFormat, Args ... args)
			: Base(Base, invalidValue, Utils::ToUtf8(std::format(errorMessageFormat, std::forward<Args>(args)...))) {
		}

		Base(Base&& r) noexcept {
			Attach(r.m_object, r.m_bOwnership);
			r.Detach();
		}

		virtual Base& operator=(Base&& r) noexcept {
			Attach(r.m_object, r.m_bOwnership);
			r.Detach();
			return *this;
		}

		virtual Base& operator=(nullptr_t) {
			Clear();
			return *this;
		}

		Base(const Base&) = delete;
		virtual Base& operator=(const Base&) = delete;

		virtual ~Base() {
			ClearInternal();
		}

		void Attach(T r, bool ownership) {
			Clear();
			m_object = r;
			m_bOwnership = ownership;
		}

		virtual void Detach() {
			m_object = nullptr;
			m_bOwnership = true;
		}

		virtual void Clear() {
			ClearInternal();
		}

		operator T() const {
			return m_object;
		}

	private:
		void ClearInternal() {
			if (m_object && m_bOwnership)
				CloserFunction(m_object);
			m_object = nullptr;
			m_bOwnership = true;
		}
	};
	
	using Icon = Base<HICON, DestroyIcon>;
	using GlobalResource = Base<HGLOBAL, FreeResource>;
	using CreatedDC = Base<HDC, DeleteDC>;
	using FindFile = Base<HANDLE, FindClose>;

	class Handle : public Base<HANDLE, CloseHandle> {

		static HANDLE DuplicateHandleNullable(HANDLE src);

	public:
		using Base<HANDLE, CloseHandle>::Base;
		Handle(Handle&& r) noexcept : Base(std::move(r)){}
		Handle(const Handle& r);
		Handle& operator=(Handle&& r) noexcept;
		Handle& operator =(const Handle& r);
		
		Handle& DuplicateFrom(HANDLE hProcess, HANDLE hSourceHandle, bool bInheritable = false);
		Handle& DuplicateFrom(HANDLE hSourceHandle, bool bInheritable = false);
	};
	
	class LoadedModule : public Base<HMODULE, FreeLibrary> {
	public:
		using Base<HMODULE, FreeLibrary>::Base;
		explicit LoadedModule(const wchar_t* pwszFileName, DWORD dwFlags = 0, bool bRequire = true);
		static LoadedModule From(HINSTANCE hInstance);
		
		void FreeAfterRunInNewThread(std::function<DWORD()>);
		template<typename T>
		T* GetProcAddress(const char* szName) const {
			return reinterpret_cast<T*>(::GetProcAddress(m_object, szName));
		}

	};
}

