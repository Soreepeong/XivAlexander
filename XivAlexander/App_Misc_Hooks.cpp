#include "pch.h"
#include "App_Misc_Hooks.h"

HANDLE App::Misc::Hooks::Binder::s_hHeap = nullptr;
std::mutex App::Misc::Hooks::Binder::s_hHeapMutex;

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

		auto relativeAddressHandled = true;
		for (size_t i = 0; i < instruction.operand_count && relativeAddressHandled; ++i) {
			const auto& operand = instruction.operands[0];
			if (operand.type == ZYDIS_OPERAND_TYPE_MEMORY) {
				switch (operand.mem.base) {
					case ZYDIS_REGISTER_IP:
					case ZYDIS_REGISTER_EIP:
					case ZYDIS_REGISTER_RIP:
						relativeAddressHandled = false;
				}
			} else if (operand.type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
				if (operand.imm.is_relative){
					relativeAddressHandled = false;
				}
			}
		}

#ifdef _DEBUG
#if INTPTR_MAX == INT64_MAX
		// Just My Code will add additional calls.
		if (instruction.opcode == 0x8d  // lea rcx, [rip+?]
			&& instruction.operand_count == 2
			&& instruction.operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER
			&& instruction.operands[1].type == ZYDIS_OPERAND_TYPE_MEMORY
			&& instruction.operands[1].mem.base == ZYDIS_REGISTER_RIP) {
			// lea
			uint64_t resultAddress = 0;
			ZydisCalcAbsoluteAddress(&instruction, &instruction.operands[1],
				reinterpret_cast<size_t>(source) + offset, &resultAddress);
			replacementJumps[body.size() + 3] = resultAddress;
			relativeAddressHandled = true;
		}
#endif
#endif

		auto append = true;
		switch (instruction.meta.category) {
			case ZYDIS_CATEGORY_CALL:
			{
				if (uint64_t resultAddress;
					instruction.operand_count >= 1
					&& ZYAN_STATUS_SUCCESS == ZydisCalcAbsoluteAddress(&instruction, &instruction.operands[0],
						reinterpret_cast<size_t>(source) + offset, &resultAddress)) {

#if INTPTR_MAX == INT32_MAX
					// call relative_addr
					body.push_back('\xE8');
					replacementJumps[body.size()] = static_cast<size_t>(resultAddress);
					body.resize(body.size() + 4, '\0');
					
#elif INTPTR_MAX == INT64_MAX
					// call QWORD PTR [rip+0x00000000]
					// FF 15 00 00 00 00
					body.push_back('\xFF');
					body.push_back('\x15');
					replacementJumps[body.size()] = resultAddress;
					body.resize(body.size() + 4, '\0');
					
#else
#error "Environment not x86 or x64."
#endif

					append = false;
					relativeAddressHandled = true;
				}
				break;
			}
			
			case ZYDIS_CATEGORY_RET:
			case ZYDIS_CATEGORY_UNCOND_BR:
				funclen = offset + instruction.length;
				break;
		}
		if (!relativeAddressHandled)
			throw std::runtime_error("Could not handle relative address while thunking");
		if (append)
			body.insert(body.end(), source + offset, source + offset + instruction.length);
		offset += instruction.length;
	}

	*reinterpret_cast<void**>(std::search(
		&body[0], &body[0] + body.size(),
		reinterpret_cast<const char*>(&DummyAddress), reinterpret_cast<const char*>(&DummyAddress + 1)
	)) = this_;

#if INTPTR_MAX == INT64_MAX
	for (const auto& [pos, ptr] : replacementJumps) {
		*reinterpret_cast<uint32_t*>(&body[pos]) = static_cast<uint32_t>(body.size() - 4 - pos);
		body.insert(body.end(), reinterpret_cast<const char*>(&ptr), reinterpret_cast<const char*>(&ptr + 1));
	}
#endif

	std::lock_guard lock(s_hHeapMutex);
	if (s_hHeap == nullptr) {
		s_hHeap = HeapCreate(HEAP_CREATE_ENABLE_EXECUTE, 0, 0);
		if (!s_hHeap)
			throw Utils::Win32::Error("HeapCreate");
	}
	m_cleanup += [this, size = body.size()]() {
#ifdef _DEBUG
		memset(m_pAddress, 0xCC, size);
#endif
		HeapFree(s_hHeap, 0, m_pAddress);
		PROCESS_HEAP_ENTRY entry{};
		while (HeapWalk(s_hHeap, &entry)) {
			if (entry.wFlags & PROCESS_HEAP_ENTRY_BUSY)
				return;
		}
		if (const auto err = GetLastError(); err != ERROR_NO_MORE_ITEMS)
			OutputDebugStringW(std::format(L"HeapWalk failure: {}\n", err).c_str());
		HeapDestroy(s_hHeap);
	};

	m_pAddress = HeapAlloc(s_hHeap, 0, body.size());
	if (!m_pAddress)
		throw Utils::Win32::Error("HeapAlloc");

#if INTPTR_MAX == INT32_MAX
	for (const auto& [pos, ptr] : replacementJumps) {
		// From (m_pAddress+pos+5) to ptr 
		*reinterpret_cast<uint32_t*>(&body[pos]) = ptr - pos - 4 - reinterpret_cast<size_t>(m_pAddress);
	}
#endif

	memcpy(m_pAddress, &body[0], body.size());
}

App::Misc::Hooks::Binder::~Binder() {
	m_cleanup.Clear();
}

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
					else
						OutputDebugStringA(std::format("{} / {}\n", pName->Name, hintOrOrdinal).c_str());
				}
			}
		}
	}

	return nullptr;
}

App::Misc::Hooks::WndProcFunction::WndProcFunction(const char* szName, HWND hWnd)
	: Super(szName, reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hWnd, GWLP_WNDPROC)))
	, m_hWnd(hWnd) {
}

App::Misc::Hooks::WndProcFunction::~WndProcFunction() = default;

bool App::Misc::Hooks::WndProcFunction::IsDisableable() const {
	return m_windowDestroyed || !m_detour || reinterpret_cast<WNDPROC>(GetWindowLongPtrW(m_hWnd, GWLP_WNDPROC)) == m_binder.GetBinder<WNDPROC>();
}

LRESULT App::Misc::Hooks::WndProcFunction::bridge(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_DESTROY) {
		m_windowDestroyed = true;
	}
	return CallWindowProcW(reinterpret_cast<WNDPROC>(m_prevProc), hwnd, msg, wParam, lParam);
}

void App::Misc::Hooks::WndProcFunction::HookEnable() {
	if (m_windowDestroyed)
		return;
	
	m_prevProc = SetWindowLongPtrW(m_hWnd, GWLP_WNDPROC, m_binder.GetBinder<LONG_PTR>());
}

void App::Misc::Hooks::WndProcFunction::HookDisable() {
	if (m_windowDestroyed)
		return;
	
	if (!IsDisableable())
		throw std::runtime_error("Cannot disable hook");
	
	SetWindowLongPtrW(m_hWnd, GWLP_WNDPROC, m_prevProc);
}

