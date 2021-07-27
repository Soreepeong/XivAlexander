#pragma once
#include "Utils_Win32_Closeable.h"

namespace Utils::Win32 {
	class GlobalResource : public Closeable<HGLOBAL, FreeResource> {
	public:
		using Closeable<HGLOBAL, FreeResource>::Closeable;
		GlobalResource(HINSTANCE hInstance, LPCWSTR lpType, LPCWSTR lpName, WORD wLanguage = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), bool fallbackToDefault = true);

		[[nodiscard]] void* GetData() const;
	};
	
	class Menu : public Closeable<HMENU, DestroyMenu> {
	public:
		using Closeable<HMENU, DestroyMenu>::Closeable;
		Menu(HINSTANCE hInstance, LPCWSTR lpType, LPCWSTR lpName, WORD wLanguage = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), bool fallbackToDefault = true);

		void AttachAndSwap(HWND hWnd);
	};

	class Accelerator : public Closeable<HACCEL, DestroyAcceleratorTable> {
	public:
		using Closeable<HACCEL, DestroyAcceleratorTable>::Closeable;
		Accelerator(HINSTANCE hInstance, LPCWSTR lpType, LPCWSTR lpName, WORD wLanguage = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), bool fallbackToDefault = true);
	};

	LPCWSTR FindStringResourceEx(HINSTANCE hInstance, UINT uId, WORD wLanguage = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), bool fallbackToDefault = true);

	std::wstring MB_GetString(int i);
}
