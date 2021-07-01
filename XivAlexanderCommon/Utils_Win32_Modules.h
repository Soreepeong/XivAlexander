#pragma once

#include <windef.h>
#include <handleapi.h>
#include <filesystem>

namespace Utils::Win32::Modules {
	std::vector<DWORD> GetProcessList();
	int CallRemoteFunction(HANDLE hProcess, void* rpfn, void* rpParam, const char* pcszDescription);
	HMODULE InjectDll(HANDLE hProcess, const std::filesystem::path& path);
	enum class ModuleNameCompareMode {
		FullPath = 0,
		FileNameWithExtension = 1,
		FileNameWithoutExtension = 2,
	};
	HMODULE FindModuleAddress(HANDLE hProcess, std::filesystem::path path, ModuleNameCompareMode compareMode = ModuleNameCompareMode::FullPath);
	void* FindRemoteFunction(HANDLE hProcess, HMODULE hModule, const char* pszFunctionName, USHORT ordinal = 0);
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
