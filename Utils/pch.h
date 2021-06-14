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
#include <wintrust.h>
#include <Shlwapi.h>
#include <Psapi.h>

#define ZLIB_CONST
#include <zlib.h>

#endif //PCH_H
