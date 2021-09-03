#pragma once

#include <filesystem>
#include <windef.h>
#include "Utils_Win32_Process.h"

namespace Utils::Win32 {
	class InjectedModule {
		HMODULE m_rpModule;
		std::filesystem::path m_path;
		Process m_hProcess;

	public:
		InjectedModule();
		InjectedModule(Process hProcess, std::filesystem::path path);
		InjectedModule(const InjectedModule&);
		InjectedModule(InjectedModule&&) noexcept;
		InjectedModule& operator=(const InjectedModule&);
		InjectedModule& operator=(InjectedModule&&) noexcept;
		~InjectedModule();

		HMODULE Address() const;

		int Call(void* rpfn, void* rpParam, const char* pcszDescription) const;
		int Call(const char* name, void* rpParam, const char* pcszDescription) const;
		void Clear();
	};
};
