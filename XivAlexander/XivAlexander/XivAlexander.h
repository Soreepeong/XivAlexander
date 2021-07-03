#pragma once

#ifndef XIVALEXANDER_DLLEXPORT

#include <minwindef.h>

#ifdef XIVALEXANDER_DLLEXPORT_SET
#define XIVALEXANDER_DLLEXPORT __declspec(dllexport)
#else
#define XIVALEXANDER_DLLEXPORT __declspec(dllimport)
#endif

namespace XivAlexDll {

	extern "C" XIVALEXANDER_DLLEXPORT int __stdcall PatchEntryPointForInjection(HANDLE hProcess);
	
	class InjectOnCreateProcessAppFlags {
	public:
		enum : size_t {
			Use = 1 << 0,
			InjectAll = 1 << 1,
			InjectGameOnly = 1 << 2,
		};
	};
	extern "C" XIVALEXANDER_DLLEXPORT int __stdcall EnableInjectOnCreateProcess(size_t flags);

	struct InjectEntryPointParameters {
		void* EntryPoint;
		void* EntryPointOriginalBytes;
		size_t EntryPointOriginalLength;
		void* TrampolineAddress;
	};

	extern "C" XIVALEXANDER_DLLEXPORT void __stdcall InjectEntryPoint(InjectEntryPointParameters* param);
	extern "C" XIVALEXANDER_DLLEXPORT int __stdcall EnableXivAlexander(size_t bEnable);
	extern "C" XIVALEXANDER_DLLEXPORT int __stdcall ReloadConfiguration(void* lpReserved);
	extern "C" XIVALEXANDER_DLLEXPORT int __stdcall CallFreeLibrary(void*);

}

#endif
