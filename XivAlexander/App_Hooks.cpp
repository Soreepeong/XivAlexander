#include "pch.h"
#include "App_Signatures.h"
#include "App_Hooks.h"

namespace App::Hooks {

	namespace Socket {
		ImportedFunction<SOCKET, int, int, int> socket("socket::socket", "ws2_32.dll", "socket", 23,
			[](int af, int type, int protocol) { return socket.Thunked(af, type, protocol); });
		ImportedFunction<int, SOCKET, const sockaddr*, int> connect("socket::connect", "ws2_32.dll", "connect", 4,
			[](SOCKET s, const sockaddr* name, int namelen) { return connect.Thunked(s, name, namelen); });
		ImportedFunction<int, int, fd_set*, fd_set*, fd_set*, const timeval*> select("socket::select", "ws2_32.dll", "select", 18,
			[](int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, const timeval* timeout) { return select.Thunked(nfds, readfds, writefds, exceptfds, timeout); });
		ImportedFunction<int, SOCKET, char*, int, int> recv("socket::recv", "ws2_32.dll", "recv", 16,
			[](SOCKET s, char* buf, int len, int flags) { return recv.Thunked(s, buf, len, flags); });
		ImportedFunction<int, SOCKET, const char*, int, int> send("socket::send", "ws2_32.dll", "send", 19,
			[](SOCKET s, const char* buf, int len, int flags) { return send.Thunked(s, buf, len, flags); });
		ImportedFunction<int, SOCKET> closesocket("socket::closesocket", "ws2_32.dll", "closesocket", 3,
			[](SOCKET s) { return closesocket.Thunked(s); });
	}

	namespace WinApi {
#ifdef _DEBUG
		// The game client's internal debugging code often trips when this function returns true,
		// so we return false instead for the ease of debugging.
		PointerFunction<BOOL> IsDebuggerPresent("WinApi::IsDebuggerPresent", ::IsDebuggerPresent,
			[]() -> BOOL { return FALSE; }
		);
#endif
	}

	const void* FindImportAddressTableItem(const char* szDllName, const char* szFunctionName, uint32_t hintOrOrdinal) {
		const auto pBaseAddress = reinterpret_cast<const char*>(GetModuleHandleW(nullptr));
		const auto pDosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(pBaseAddress);
		const auto pNtHeader = reinterpret_cast<const IMAGE_NT_HEADERS*>(pBaseAddress + pDosHeader->e_lfanew);

		// https://docs.microsoft.com/en-us/windows/win32/debug/pe-format?redirectedfrom=MSDN#optional-header-data-directories-image-only
		const auto pImportTable = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(pBaseAddress + pNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
		for (size_t i = 0; pImportTable[i].OriginalFirstThunk; ++i) {
			if (!pImportTable[i].Name)
				continue;

			if (_strcmpi(pBaseAddress + pImportTable[i].Name, szDllName))
				continue;

			if (pImportTable[i].OriginalFirstThunk) {
				const auto pImportLookupTable = reinterpret_cast<const size_t*>(pBaseAddress + pImportTable[i].OriginalFirstThunk);
				const auto pImportAddressTable = reinterpret_cast<void* const*>(pBaseAddress + pImportTable[i].FirstThunk);

				for (size_t j = 0; pImportLookupTable[j]; ++j) {
					if (IMAGE_SNAP_BY_ORDINAL(pImportLookupTable[j])) {
						if (hintOrOrdinal && IMAGE_ORDINAL(pImportLookupTable[j]) == hintOrOrdinal)
							return &pImportAddressTable[j];

					} else {
						const auto pName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(pBaseAddress + (pImportLookupTable[j] & 0x7FFFFFFF));
						if ((hintOrOrdinal && pName->Hint == hintOrOrdinal)
							|| (szFunctionName && pName->Name && !strcmp(szFunctionName, pName->Name)))
							return &pImportAddressTable[j];
					}
				}
			}
		}

		return nullptr;
	}
}
