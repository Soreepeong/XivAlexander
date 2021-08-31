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
		T* GetProcAddress(const char* szName, bool throwIfNotFound = false) const {
			const auto result = reinterpret_cast<T*>(::GetProcAddress(m_object, szName));
			if (throwIfNotFound && !result)
				throw std::runtime_error(std::format("Function \"{}\" not found", szName));
			return result;
		}

		[[nodiscard]] std::filesystem::path PathOf() const;

		void Pin() const;
	};
}
