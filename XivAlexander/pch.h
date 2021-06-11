// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

#define _CRT_SECURE_NO_WARNINGS

#include <memory>
#include <algorithm>
#include <functional>
#include <string>
#include <map>
#include <set>
#include <mutex>
#include <functional>
#include <numeric>
#include <queue>
#include <type_traits>
#include <cassert>
#include <locale>
#include <fstream>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>
#include <Shlwapi.h>
#include <windowsx.h>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <d3d11.h>
#include <bcrypt.h>
#include <shellscalingapi.h>
#include <iphlpapi.h>
#include <comdef.h>
#include <shobjidl_core.h>

#include <MinHook.h>
#include <Win32Handle.h>
#include <WinPath.h>
#include <scintilla/Scintilla.h>
#include <ListenerManager.h>

#include <nlohmann/json.hpp>

extern HINSTANCE g_hInstance;

#include "App_Signatures.h"
#include "App_Hooks.h"
#include "App_ConfigRepository.h"
#include "App_Misc_Logger.h"

static DECLSPEC_NORETURN void mark_unreachable_code() {}

#endif //PCH_H
