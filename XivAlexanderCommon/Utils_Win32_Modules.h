#pragma once

#include <windef.h>
#include <handleapi.h>
#include <filesystem>

namespace Utils::Win32::Modules {
	std::vector<DWORD> GetProcessList();
	int CallRemoteFunction(HANDLE hProcess, void* rpfn, void* rpParam, const char* pcszDescription);
	HMODULE InjectDll(HANDLE hProcess, const std::filesystem::path& path);
	HMODULE FindModuleAddress(HANDLE hProcess, const std::filesystem::path& szDllPath);
	std::filesystem::path PathFromModule(HMODULE hModule = nullptr, HANDLE hProcess = INVALID_HANDLE_VALUE);

	class InjectedModule {
		HMODULE m_rpModule;
		std::filesystem::path m_path;
		HANDLE m_hProcess;

	public:
		InjectedModule();
		InjectedModule(HANDLE hProcess, std::filesystem::path path);
		InjectedModule(const InjectedModule&);
		InjectedModule(InjectedModule&&) noexcept;
		InjectedModule& operator=(const InjectedModule&);
		InjectedModule& operator=(InjectedModule&&) noexcept;
		~InjectedModule();

		int Call(void* rpfn, void* rpParam, const char* pcszDescription) const;
		int Call(const char* name, void* rpParam, const char* pcszDescription) const;
		void Clear();
	};
};
