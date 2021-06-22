// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

#include <string>
#include <algorithm>
#include <vector>
#include <functional>
#include <numeric>
#include <map>
#include <set>
#include <stdexcept>
#include <chrono>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <WS2tcpip.h>
#include <PathCch.h>
#include <wincrypt.h>
#include <WinTrust.h>
#include <Shlwapi.h>
#include <Psapi.h>
#include <TlHelp32.h>

#define ZLIB_CONST
#include <zlib.h>

static DECLSPEC_NORETURN void mark_unreachable_code() {}

#endif //PCH_H
