#include "pch.h"
#include "App_Misc_Hooks.h"

HANDLE App::Misc::Hooks::Binder::s_hHeap = nullptr;

App::Misc::Hooks::Binder::Binder(void* this_, void* templateMethod) {
	auto source = static_cast<const char*>(templateMethod);

	/*
	 * Extremely compiler implementation specific! May break anytime. Shouldn't be too difficult to fix though.
	 */
	
#ifdef _DEBUG
	while (*source == '\xE9') {  // JMP in case the program's compiled in Debug mode
		const auto displacement = *reinterpret_cast<const int*>(source + 1);
		source += 5 + displacement;
	}
#endif

	ZydisDecoder decoder;
	ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_ADDRESS_WIDTH_64);

	std::vector<char> body;
	std::map<size_t, size_t> replacementJumps;
	
	ZydisDecodedInstruction instruction;
	for (size_t offset = 0, funclen = 32768;
		ZYAN_SUCCESS(ZydisDecoderDecodeBuffer(&decoder, source + offset, funclen - offset, &instruction));
		) {

#ifdef _DEBUG
		// Just My Code will add additional calls.
		if (instruction.opcode == 0x8d  // lea rcx, [rip+?]
			&& instruction.operand_count == 2
			&& instruction.operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER
			&& instruction.operands[1].type == ZYDIS_OPERAND_TYPE_MEMORY
			&& instruction.operands[1].mem.base == ZYDIS_REGISTER_RIP) {
			// lea
			size_t resultAddress = 0;
			ZydisCalcAbsoluteAddress(&instruction, &instruction.operands[1],
				reinterpret_cast<size_t>(source) + offset, &resultAddress);
			replacementJumps[body.size() + 3] = resultAddress;
		}
#endif
		
		switch (instruction.meta.category) {
			case ZYDIS_CATEGORY_CALL:
			{
				if (size_t resultAddress;
					instruction.operand_count == 1
					&& 0 == ZydisCalcAbsoluteAddress(&instruction, &instruction.operands[0],
					reinterpret_cast<size_t>(source) + offset, &resultAddress)) {

					// call QWORD PTR [rip+0x00000000]
					// FF 15 00 00 00 00
					body.push_back('\xFF');
					body.push_back('\x15');
					replacementJumps[body.size()] = resultAddress;
					body.resize(body.size() + 4, '\0');
				} else {
					body.insert(body.end(), source + offset, source + offset + instruction.length);
				}
				break;
			}
			
			case ZYDIS_CATEGORY_RET:
				funclen = offset + instruction.length;
				// falls through
			
			default:
				body.insert(body.end(), source + offset, source + offset + instruction.length);
				break;
		}
		offset += instruction.length;
	}
	for (const auto& [pos, ptr] : replacementJumps) {
		*reinterpret_cast<uint32_t*>(&body[pos]) = static_cast<uint32_t>(body.size() - 4 - pos);
		body.insert(body.end(), reinterpret_cast<const char*>(&ptr), reinterpret_cast<const char*>(&ptr + 1));
	}
	
	if (s_hHeap == nullptr) {
		s_hHeap = HeapCreate(HEAP_CREATE_ENABLE_EXECUTE, 0, 0);
		if (!s_hHeap)
			throw Utils::Win32::Error("HeapCreate");
	}
	m_cleanup += [this]() {
		PROCESS_HEAP_ENTRY entry{};
		if (HeapWalk(s_hHeap, &entry))
			return;
		if (const auto err = GetLastError(); err != ERROR_NO_MORE_ITEMS)
			OutputDebugStringW(std::format(L"HeapWalk failure: {}\n", err).c_str());
		HeapDestroy(s_hHeap);
	};
	
	m_pAddress = HeapAlloc(s_hHeap, 0, body.size());
	if (!m_pAddress)
		throw Utils::Win32::Error("HeapAlloc");
	
	memcpy(m_pAddress, &body[0], body.size());
	*reinterpret_cast<void**>(std::search(
		static_cast<char*>(m_pAddress), static_cast<char*>(m_pAddress) + body.size(),
		reinterpret_cast<const char*>(&DummyAddress), reinterpret_cast<const char*>(&DummyAddress + 1)
	)) = this_;
}

App::Misc::Hooks::Binder::~Binder() = default;

const void* App::Misc::Hooks::FindImportAddressTableItem(const char* szDllName, const char* szFunctionName, uint32_t hintOrOrdinal) {
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
