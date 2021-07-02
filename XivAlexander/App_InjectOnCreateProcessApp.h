#pragma once

#include <memory>

namespace App {
	namespace Misc {
		class DebuggerDetectionDisabler;
	}

	class InjectOnCreateProcessApp {
		const std::shared_ptr<Misc::DebuggerDetectionDisabler> m_detectionDisabler;

		class Implementation;
		friend class Implementation;
		std::unique_ptr<Implementation> m_pImpl;

	public:
		InjectOnCreateProcessApp();
		~InjectOnCreateProcessApp();
	};
}

extern "C" __declspec(dllexport) int __stdcall PatchEntryPointForInjection(HANDLE hProcess);
extern "C" __declspec(dllexport) int __stdcall EnableInjectOnCreateProcess(size_t bEnable);

struct InjectEntryPointParameters {
	void* EntryPoint;
	void* EntryPointOriginalBytes;
	size_t EntryPointOriginalLength;
	void* TrampolineAddress;
};

extern "C" __declspec(dllexport) void __stdcall InjectEntryPoint(InjectEntryPointParameters* param);
