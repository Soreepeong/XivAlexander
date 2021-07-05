#pragma once

#include <span>
#include <functional>

#include <windef.h>

#include "Utils_CallOnDestruction.h"
#include "Utils_Win32.h"
#include "Utils__Misc.h"
#include "Utils__String.h"

namespace Utils::Win32::Closeable {
	template <typename T, auto CloserFunction>
	class Base {
	public:
		static constexpr T Null = reinterpret_cast<T>(nullptr);

	protected:
		T m_object = Null;
		bool m_bOwnership = true;

	public:
		Base() = default;

		Base(std::nullptr_t) {}

		explicit Base(T Base, bool ownership = false)
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
			m_object = r.m_object;
			m_bOwnership = r.m_bOwnership;
			r.Detach();
		}

		Base& operator=(Base&& r) noexcept {
			m_object = r.m_object;
			m_bOwnership = r.m_bOwnership;
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

		virtual Base& Attach(T r, T invalidValue, bool ownership, const std::string& errorMessage) {
			if (r == invalidValue)
				throw Error(errorMessage);
			
			Clear();
			m_object = r;
			m_bOwnership = ownership;
			return *this;
		}

		virtual T Detach() {
			const auto prev = m_object;
			m_object = nullptr;
			m_bOwnership = true;
			return prev;
		}

		virtual void Clear() {
			ClearInternal();
		}

		operator T() const {
			return m_object;
		}

		bool HasOwnership() const {
			return m_object && m_bOwnership;
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
		Handle() = default;
		Handle(Handle&& r) noexcept : Base(std::move(r)) {}
		Handle(const Handle& r);
		Handle& operator=(Handle&& r) noexcept;
		Handle& operator=(const Handle& r);
		
		Handle& DuplicateFrom(HANDLE hProcess, HANDLE hSourceHandle, bool bInheritable = false);
		Handle& DuplicateFrom(HANDLE hSourceHandle, bool bInheritable = false);
	};

	class ActivationContext : public Base<HANDLE, ReleaseActCtx> {
	public:
		using Base<HANDLE, ReleaseActCtx>::Base;
		explicit ActivationContext(const ACTCTXW& actctx);

		CallOnDestruction With() const;
	};
	
	class LoadedModule : public Base<HMODULE, FreeLibrary> {
	public:
		using Base<HMODULE, FreeLibrary>::Base;
		explicit LoadedModule(const wchar_t* pwszFileName, DWORD dwFlags = 0, bool bRequire = true);
		LoadedModule(LoadedModule&& r) noexcept;
		LoadedModule(const LoadedModule& r);
		LoadedModule& operator=(LoadedModule&& r) noexcept;
		LoadedModule& operator=(const LoadedModule& r);
		LoadedModule& operator=(std::nullptr_t) override;
		static LoadedModule From(HINSTANCE hInstance);
		
		template<typename T>
		T* GetProcAddress(const char* szName) const {
			return reinterpret_cast<T*>(::GetProcAddress(m_object, szName));
		}
	};

	class Thread : public Handle {
	public:
		Thread(std::wstring name, std::function<DWORD()> body, LoadedModule hLibraryToFreeAfterExecution = nullptr);
		Thread(std::wstring name, std::function<void()> body, LoadedModule hLibraryToFreeAfterExecution = nullptr);
		~Thread() override;
		
		static Thread WithReference(std::wstring name, std::function<DWORD()> body, HINSTANCE hModule);
		static Thread WithReference(std::wstring name, std::function<void()> body, HINSTANCE hModule);

		[[nodiscard]] DWORD GetId() const;

		void Wait() const;
		[[nodiscard]] DWORD Wait(DWORD duration) const;
	};
}
