// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

#include <vector>
#include <set>
#include <algorithm>
#include <fstream>
#include <format>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>
#include <TlHelp32.h>
#include <PathCch.h>
#include <shellapi.h>
#include <Shlwapi.h>
#include <ShObjIdl_core.h>
#include <comdef.h>

#include <XivAlexanderCommon.h>
#include <XivAlexander.h>

#include <argparse/argparse.hpp>
#include <nlohmann/json.hpp>
#include <curlpp/cURLpp.hpp>
#include <curlpp/Easy.hpp>
#include <curlpp/Options.hpp>
#include <libzippp/libzippp.h>

_COM_SMARTPTR_TYPEDEF(IFileOpenDialog, __uuidof(IFileOpenDialog));
_COM_SMARTPTR_TYPEDEF(IShellItem, __uuidof(IShellItem));

#endif //PCH_H
