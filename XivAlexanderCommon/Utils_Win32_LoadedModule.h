#pragma once

#include <span>
#include <windef.h>

#include "Utils_Win32_Closeable.h"

namespace Utils::Win32 {
	class LoadedModule : public Closeable<HMODULE, FreeLibrary> {
	public:
		using Closeable<HMODULE, FreeLibrary>::Closeable;
		explicit LoadedModule(const wchar_t* pwszFileName, DWORD dwFlags = 0, bool bRequire = true);
		LoadedModule(LoadedModule&& r) noexcept;
		LoadedModule(const LoadedModule& r);
		LoadedModule& operator=(LoadedModule&& r) noexcept;
		LoadedModule& operator=(const LoadedModule& r);
		LoadedModule& operator=(std::nullptr_t) override;
		~LoadedModule() override;

		static LoadedModule LoadMore(const LoadedModule& module);

		template<typename T>
		T* GetProcAddress(const char* szName) const {
			return reinterpret_cast<T*>(::GetProcAddress(m_object, szName));
		}

		[[nodiscard]] std::filesystem::path PathOf() const;
	};
}
