#pragma once

#ifndef PCH_H
#define PCH_H

// The shared XivAlexander sources include "pch.h"; this one carries what the loader app uses.

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <regex>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

// NOLINTNEXTLINE(clang-diagnostic-reserved-macro-identifier)
#define _WINSOCKAPI_   // Prevent <winsock.h> from being included

#define NOMINMAX
#include <Windows.h>
#include <winternl.h>

#include <CommCtrl.h>
#include <dwmapi.h>
#include <PathCch.h>
#include <Psapi.h>
#include <shellapi.h>
#include <ShellScalingApi.h>
#include <ShlObj.h>
#include <Shlwapi.h>
#include <ShObjIdl.h>
#include <TlHelp32.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <wincred.h>
#include <windowsx.h>
#include <winhttp.h>
#include <WinSock2.h>
#include <WS2tcpip.h>

#pragma warning(push)
#pragma warning(disable: 26439)  // This kind of function may not throw. Declare it 'noexcept' (f.6).
#pragma warning(disable: 26495)  // Variable is uninitialized. Always initialize a member variable (type.6).
#pragma warning(disable: 26819)  // Unannotated fallthrough between switch labels (es.78).
#define ZLIB_CONST
#include <zlib.h>
#include <argparse/argparse.hpp>
#include <libzippp/libzippp.h>
#include <nlohmann/json.hpp>
#include <srell.hpp>
#include <Zydis/Zydis.h>
#pragma warning(pop)

#include <comdef.h>
_COM_SMARTPTR_TYPEDEF(IFileOpenDialog, __uuidof(IFileOpenDialog));
_COM_SMARTPTR_TYPEDEF(IFileSaveDialog, __uuidof(IFileSaveDialog));
_COM_SMARTPTR_TYPEDEF(IShellItem, __uuidof(IShellItem));

#include <xivres/util.h>
#include <xivres/util.span_cast.h>
#include <xivres/util.on_dtor.h>
#include "Utils/StringUtils.h"
#include "Utils/Utils.h"
#include "Utils/Win32/Handle.h"

#endif //PCH_H
