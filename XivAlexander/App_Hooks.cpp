#include "pch.h"
#include "App_Hooks.h"

namespace App::Hooks {
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
