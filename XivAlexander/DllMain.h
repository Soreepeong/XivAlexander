#pragma once

namespace Utils {
	namespace Win32 {
		class ActivationContext;
		class LoadedModule;
	}
}

namespace Dll {
	HWND FindGameMainWindow(bool throwOnError = true);

	const Utils::Win32::LoadedModule& Module();
	const Utils::Win32::ActivationContext& ActivationContext();
	size_t DisableUnloading(const char* pszReason);
	const char* GetUnloadDisabledReason();
	bool IsLoadedAsDependency();
}
