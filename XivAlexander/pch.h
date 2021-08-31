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
#include <span>
#include <new>
#include <format>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>
#include <Shlwapi.h>
#include <windowsx.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <mstcpip.h>
#include <bcrypt.h>
#include <ShellScalingApi.h>
#include <iphlpapi.h>
#include <comdef.h>
#include <ShObjIdl_core.h>
#include <IcmpAPI.h>
#include <winternl.h>
#include <Psapi.h>
#include <PathCch.h>

#include <XivAlexanderCommon.h>

#include <MinHook.h>
#include <scintilla/Scintilla.h>
#include <nlohmann/json.hpp>
#include <Zydis/Zydis.h>

_COM_SMARTPTR_TYPEDEF(IFileSaveDialog, __uuidof(IFileSaveDialog));
_COM_SMARTPTR_TYPEDEF(IShellItem, __uuidof(IShellItem));

#include "DllMain.h"
#include "App_ConfigRepository.h"
#include "App_Misc_Logger.h"

#endif //PCH_H
