#pragma once

namespace Dll {
	const Utils::Win32::LoadedModule& Module();
	const Utils::Win32::ActivationContext& ActivationContext();
	size_t DisableUnloading(const char* pszReason);
	const char* GetUnloadDisabledReason();
	bool IsLoadedAsDependency();
}
