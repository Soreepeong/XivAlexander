#include "pch.h"
#include "Misc/Hooks.h"

std::vector<char, Utils::Win32::HeapAllocator<char>> XivAlexander::Misc::Hooks::Binder::CreateThunkBody(void* this_, void* templateMethod) {
	static Utils::Win32::HeapAllocator<char> allocator(HEAP_CREATE_ENABLE_EXECUTE);

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
	ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

	std::vector<char, Utils::Win32::HeapAllocator<char>> body(allocator);
	std::map<size_t, size_t> replacementJumps;

	static_assert(sizeof std::vector<char, Utils::Win32::HeapAllocator<char>>::size_type == sizeof size_t);

	ZydisDecodedInstruction instruction;
	ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
	for (size_t offset = 0, funclen = 32768;
		ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, source + offset, funclen - offset, &instruction, operands));
	) {
		auto relativeAddressHandled = true;
		for (size_t i = 0; i < instruction.operand_count && relativeAddressHandled; ++i) {
			const auto& operand = operands[0];
			if (operand.type == ZYDIS_OPERAND_TYPE_MEMORY) {
				switch (operand.mem.base) {
					case ZYDIS_REGISTER_IP:
					case ZYDIS_REGISTER_EIP:
					case ZYDIS_REGISTER_RIP:
						relativeAddressHandled = false;
				}
			} else if (operand.type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
				if (operand.imm.is_relative) {
					relativeAddressHandled = false;
				}
			}
		}

#ifdef _DEBUG
#if INTPTR_MAX == INT64_MAX
		// Just My Code will add additional calls.
		if (instruction.opcode == 0x8d  // lea rcx, [rip+?]
			&& instruction.operand_count == 2
			&& operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER
			&& operands[1].type == ZYDIS_OPERAND_TYPE_MEMORY
			&& operands[1].mem.base == ZYDIS_REGISTER_RIP) {
			// lea
			uint64_t resultAddress = 0;
			ZydisCalcAbsoluteAddress(&instruction, &operands[1],
				reinterpret_cast<size_t>(source) + offset, &resultAddress);
			replacementJumps[body.size() + 3] = resultAddress;
			relativeAddressHandled = true;
		}
#endif
#endif

		auto append = true;
		switch (instruction.meta.category) {
			case ZYDIS_CATEGORY_CALL: {
				if (uint64_t resultAddress;
					instruction.operand_count >= 1
					&& ZYAN_STATUS_SUCCESS == ZydisCalcAbsoluteAddress(&instruction, operands,
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
			throw std::runtime_error("Assertion failure: Could not handle relative address while thunking");
		if (append)
			body.insert(body.end(), source + offset, source + offset + instruction.length);
		offset += instruction.length;
	}

	memcpy(std::search(
		&body[0], &body[0] + body.size(),
		reinterpret_cast<const char*>(&Binder::DummyAddress), reinterpret_cast<const char*>(&Binder::DummyAddress + 1)
	), &this_, sizeof this_);

#if INTPTR_MAX == INT64_MAX
	for (const auto& [pos, ptr] : replacementJumps) {
		const auto displacement = static_cast<uint32_t>(body.size() - 4 - pos);
		static_assert(sizeof displacement == 4);
		memcpy(&body[pos], &displacement, sizeof displacement);
		body.insert(body.end(), reinterpret_cast<const char*>(&ptr), reinterpret_cast<const char*>(&ptr + 1));
	}

#elif INTPTR_MAX == INT32_MAX
	for (const auto& [pos, ptr] : replacementJumps) {
		const auto displacement = ptr - pos - 4 - reinterpret_cast<size_t>(&body[0]);
		static_assert(sizeof displacement == 4);
		memcpy(&body[pos], &displacement, sizeof displacement);
	}
#endif

	return body;
}

XivAlexander::Misc::Hooks::Binder::Binder(void* this_, void* templateMethod)
	: m_impl(CreateThunkBody(this_, templateMethod)) {
}

XivAlexander::Misc::Hooks::Binder::~Binder() = default;

XivAlexander::Misc::Hooks::WndProcFunction::WndProcFunction(const char* szName, HWND hWnd)
	: Super(szName, reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hWnd, GWLP_WNDPROC)))
	, m_hWnd(hWnd)
	, m_prevProc(0) {
}

XivAlexander::Misc::Hooks::WndProcFunction::~WndProcFunction() = default;

bool XivAlexander::Misc::Hooks::WndProcFunction::IsDisableable() const {
	return m_windowDestroyed || !m_detour || reinterpret_cast<WNDPROC>(GetWindowLongPtrW(m_hWnd, GWLP_WNDPROC)) == m_binder.GetBinder<WNDPROC>();
}

LRESULT XivAlexander::Misc::Hooks::WndProcFunction::bridge(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_DESTROY) {
		m_windowDestroyed = true;
	}
	return CallWindowProcW(reinterpret_cast<WNDPROC>(m_prevProc), hwnd, msg, wParam, lParam);
}

void XivAlexander::Misc::Hooks::WndProcFunction::HookEnable() {
	if (m_windowDestroyed)
		return;

	m_prevProc = SetWindowLongPtrW(m_hWnd, GWLP_WNDPROC, m_binder.GetBinder<LONG_PTR>());
}

void XivAlexander::Misc::Hooks::WndProcFunction::HookDisable() {
	if (m_windowDestroyed)
		return;

	if (!IsDisableable())
		throw std::runtime_error("App::Misc::Hooks::WndProcFunction::HookDisable(!IsDisableable)");

	SetWindowLongPtrW(m_hWnd, GWLP_WNDPROC, m_prevProc);
}
