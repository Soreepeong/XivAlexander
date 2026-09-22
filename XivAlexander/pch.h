// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

// ReSharper disable CppClangTidyClangDiagnosticReservedIdMacro
// ReSharper disable CppClangTidyBugproneReservedIdentifier

#pragma once

#ifndef PCH_H
#define PCH_H

// C++ standard library
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <locale>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <numeric>
#include <queue>
#include <ranges>
#include <regex>
#include <set>
#include <signal.h>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

// NOLINTNEXTLINE(clang-diagnostic-reserved-macro-identifier)
#define _WINSOCKAPI_   // Prevent <winsock.h> from being included

// Windows API, part 1
#define NOMINMAX
#include <Windows.h>
#include <winternl.h>

// Windows API, part 2
#include <audioclient.h>
#include <bcrypt.h>
#include <DbgHelp.h>
#include <dwmapi.h>
#include <iphlpapi.h>
#include <mmdeviceapi.h>
#include <mstcpip.h>
#include <PathCch.h>
#include <propkey.h>
#include <propvarutil.h>
#include <Psapi.h>
#include <shellapi.h>
#include <ShellScalingApi.h>
#include <ShlObj.h>
#include <shlobj_core.h>
#include <Shlwapi.h>
#include <ShObjIdl.h>
#include <TlHelp32.h>
#include <uxtheme.h>
#include <wincrypt.h>
#include <windowsx.h>
#include <WinTrust.h>
#include <winhttp.h>
#include <WinSock2.h>
#include <WS2tcpip.h>

// Windows API, part 3
#include <IcmpAPI.h>

// vcpkg dependencies
#pragma warning(push)
#pragma warning(disable: 26439)  // This kind of function may not throw. Declare it 'noexcept' (f.6).
#pragma warning(disable: 26495)  // Variable is uninitialized. Always initialize a member variable (type.6).
#pragma warning(disable: 26819)  // Unannotated fallthrough between switch labels (es.78).
#define ZLIB_CONST
#include <zlib.h>
#include <MinHook.h>
#include <argparse/argparse.hpp>
#include <libzippp/libzippp.h>
#include <nlohmann/json.hpp>
#include <scintilla/Scintilla.h>

#include <scintilla/ILexer.h>
#include <lexilla/SciLexer.h>
#include <lexilla/Lexilla.h>

#include <FLAC++/decoder.h>
#include <srell.hpp>
#include <Zydis/Zydis.h>
#pragma warning(pop)

// COM smart pointer definitions
#include <comdef.h>
#include <dwrite_3.h>
_COM_SMARTPTR_TYPEDEF(IAudioClient, __uuidof(IAudioClient));
_COM_SMARTPTR_TYPEDEF(IFileSaveDialog, __uuidof(IFileSaveDialog));
_COM_SMARTPTR_TYPEDEF(IFileOpenDialog, __uuidof(IFileOpenDialog));
_COM_SMARTPTR_TYPEDEF(IMMDevice, __uuidof(IMMDevice));
_COM_SMARTPTR_TYPEDEF(IMMDeviceEnumerator, __uuidof(IMMDeviceEnumerator));
_COM_SMARTPTR_TYPEDEF(IShellItem, __uuidof(IShellItem));
_COM_SMARTPTR_TYPEDEF(IShellItemArray, __uuidof(IShellItemArray));
_COM_SMARTPTR_TYPEDEF(IPropertyStore, __uuidof(IPropertyStore));
_COM_SMARTPTR_TYPEDEF(ITaskbarList3, __uuidof(ITaskbarList3));

// Infrequently changed utility headers
#include <xivres/util.h>
#include <xivres/util.span_cast.h>
#include <xivres/util.on_dtor.h>
#include "Utils/StringUtils.h"
#include "Utils/Utils.h"
#include "Utils/Win32/Handle.h"

#endif //PCH_H
