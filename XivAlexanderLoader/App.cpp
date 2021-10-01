#include "pch.h"

namespace XivAlexDll {
	extern "C" __declspec(dllimport) int XA_LoaderApp();
}


int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
	return XivAlexDll::XA_LoaderApp();
}
