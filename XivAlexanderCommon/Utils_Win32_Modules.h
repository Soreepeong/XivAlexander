#pragma once

#include <windef.h>
#include <handleapi.h>
#include <filesystem>

namespace Utils::Win32::Modules {
	std::vector<DWORD> GetProcessList();
	static void* GetModulePointer(HANDLE hProcess, const std::filesystem::path& path);
	int CallRemoteFunction(HANDLE hProcess, void* rpfn, void* rpParam, const char* pcszDescription);
	void* InjectDll(HANDLE hProcess, const std::filesystem::path& path);
	void* FindModuleAddress(HANDLE hProcess, const std::filesystem::path& szDllPath);
	std::filesystem::path PathFromModule(HMODULE hModule = nullptr, HANDLE hProcess = INVALID_HANDLE_VALUE);
};
