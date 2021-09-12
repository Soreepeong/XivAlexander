#include "pch.h"

namespace XivAlexDll {
	extern "C" __declspec(dllimport) int __stdcall XA_LoaderApp(LPWSTR lpCmdLine);
}


int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd) {
	return XivAlexDll::XA_LoaderApp(lpCmdLine);
}
