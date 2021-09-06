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

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#include <algorithm>
#include <chrono>
#include <codecvt>
#include <format>
#include <functional>
#include <map>
#include <numeric>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// Windows API, part 1
#define NOMINMAX
#define _WINSOCKAPI_   // Prevent <winsock.h> from being included
#include <Windows.h>

// Windows API, part 2
#include <PathCch.h>
#include <Psapi.h>
#include <shellapi.h>
#include <Shlwapi.h>
#include <TlHelp32.h>
#include <wincrypt.h>
#include <windowsx.h>
#include <WinTrust.h>
#include <WS2tcpip.h>

#pragma warning(push)
#pragma warning(disable: 26439)  // This kind of function may not throw. Declare it 'noexcept' (f.6).
#pragma warning(disable: 26495)  // Variable is uninitialized. Always initialize a member variable (type.6).
#pragma warning(disable: 26819)  // Unannotated fallthrough between switch labels (es.78).
#define ZLIB_CONST
#include <zlib.h>
#include <cryptopp/base64.h>
#include <cryptopp/blowfish.h>
#include <cryptopp/modes.h>
#include <cryptopp/sha.h>
#include <curlpp/cURLpp.hpp>
#include <curlpp/Easy.hpp>
#include <curlpp/Options.hpp>
#include <freetype/freetype.h>
#include <nlohmann/json.hpp>
#pragma warning(pop)

#include "XaFormat.h"

#endif //PCH_H
