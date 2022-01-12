#include "pch.h"

namespace Dll {
	extern "C" __declspec(dllimport) int XA_LoaderApp();
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
	return Dll::XA_LoaderApp();
}
