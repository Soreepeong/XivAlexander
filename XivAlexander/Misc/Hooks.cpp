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

	std::memcpy(std::search(
		&body[0], &body[0] + body.size(),
		reinterpret_cast<const char*>(&Binder::DummyAddress), reinterpret_cast<const char*>(&Binder::DummyAddress + 1)
	), &this_, sizeof this_);

#if INTPTR_MAX == INT64_MAX
	for (const auto& [pos, ptr] : replacementJumps) {
		const auto displacement = static_cast<uint32_t>(body.size() - 4 - pos);
		static_assert(sizeof displacement == 4);
		std::memcpy(&body[pos], &displacement, sizeof displacement);
		body.insert(body.end(), reinterpret_cast<const char*>(&ptr), reinterpret_cast<const char*>(&ptr + 1));
	}

#elif INTPTR_MAX == INT32_MAX
	for (const auto& [pos, ptr] : replacementJumps) {
		const auto displacement = ptr - pos - 4 - reinterpret_cast<size_t>(&body[0]);
		static_assert(sizeof displacement == 4);
		std::memcpy(&body[pos], &displacement, sizeof displacement);
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

namespace {
	constexpr size_t CallSiteStubSize = 16;

	// Within a call rel32's reach of address (±2 GB), from the nearest free regions, looking down first.
	uint8_t* AllocateNear(const void* address, size_t size) {
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		const auto granularity = static_cast<uintptr_t>(si.dwAllocationGranularity);
		const auto origin = reinterpret_cast<uintptr_t>(address);
		const auto reach = static_cast<uintptr_t>(0x7FF00000);
		const auto low = (std::max)(origin > reach ? origin - reach : 0, reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress));
		const auto high = (std::min)(origin + reach, reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress));
		const auto alignDown = [granularity](uintptr_t p) { return p / granularity * granularity; };

		const auto tryAt = [size](uintptr_t p) {
			return static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(p), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		};

		MEMORY_BASIC_INFORMATION mbi;
		for (auto p = alignDown(origin); p >= low + granularity;) {
			p -= granularity;
			if (!VirtualQuery(reinterpret_cast<void*>(p), &mbi, sizeof mbi))
				break;
			if (mbi.State == MEM_FREE) {
				if (const auto res = tryAt(p))
					return res;
			} else {
				// skip the rest of that allocation
				p = alignDown(reinterpret_cast<uintptr_t>(mbi.AllocationBase));
			}
		}

		for (auto p = alignDown(origin) + granularity; p + size <= high;) {
			if (!VirtualQuery(reinterpret_cast<void*>(p), &mbi, sizeof mbi))
				break;
			if (mbi.State == MEM_FREE) {
				if (const auto res = tryAt(p))
					return res;
			}
			p = alignDown(reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize + granularity - 1);
		}

		throw std::runtime_error("No memory within reach of the call site could be allocated.");
	}
}

void* XivAlexander::Misc::Hooks::CallSitePatch::TargetOf(const void* callInstruction) {
	const auto call = static_cast<const uint8_t*>(callInstruction);
	if (call[0] != 0xE8)
		throw std::runtime_error(std::format("0x{:X} is not a call rel32", reinterpret_cast<uintptr_t>(call)));
	return const_cast<uint8_t*>(call + 5 + *reinterpret_cast<const int32_t*>(call + 1));
}

XivAlexander::Misc::Hooks::CallSitePatch::CallSitePatch(void* callInstruction, void* destination)
	: m_call(static_cast<uint8_t*>(callInstruction))
	, m_original(*reinterpret_cast<const int32_t*>(m_call + 1)) {
	void(TargetOf(m_call));

	// A locked store split across cache lines may not be atomic everywhere.
	if ((reinterpret_cast<uintptr_t>(m_call + 1) & 63) > 60)
		throw std::runtime_error("The call's displacement spans two cache lines.");

	m_stub = AllocateNear(m_call, CallSiteStubSize);
	const auto displacement = reinterpret_cast<intptr_t>(m_stub) - reinterpret_cast<intptr_t>(m_call + 5);
	if (displacement < INT32_MIN || displacement > INT32_MAX) {
		VirtualFree(m_stub, 0, MEM_RELEASE);
		throw std::runtime_error("The stub is out of the call's reach.");
	}
	m_redirected = static_cast<int32_t>(displacement);

	constexpr uint8_t jump[8]{0xFF, 0x25, 0x02, 0x00, 0x00, 0x00, 0xCC, 0xCC};
	std::memcpy(m_stub, jump, sizeof jump);
	std::memcpy(m_stub + 8, &destination, sizeof destination);
	DWORD oldProtect;
	VirtualProtect(m_stub, CallSiteStubSize, PAGE_EXECUTE_READ, &oldProtect);
	FlushInstructionCache(GetCurrentProcess(), m_stub, CallSiteStubSize);
}

XivAlexander::Misc::Hooks::CallSitePatch::~CallSitePatch() {
	Restore();
	if (m_stub)
		VirtualFree(m_stub, 0, MEM_RELEASE);
}

bool XivAlexander::Misc::Hooks::CallSitePatch::IsRedirected() const {
	return m_stub && Current() == m_redirected;
}

bool XivAlexander::Misc::Hooks::CallSitePatch::IsRestorable() const {
	const auto current = Current();
	return current == m_original || (m_stub && current == m_redirected);
}

bool XivAlexander::Misc::Hooks::CallSitePatch::Redirect() {
	if (!m_stub)
		return false;
	return Exchange(m_original, m_redirected);
}

void XivAlexander::Misc::Hooks::CallSitePatch::Restore() {
	if (!m_stub || Current() == m_original || Exchange(m_redirected, m_original))
		return;

	// Patched over by something that may call the stub: keep it, going where the call went before.
	SetStubDestination(m_call + 5 + m_original);
	m_stub = nullptr;
}

int32_t XivAlexander::Misc::Hooks::CallSitePatch::Current() const {
	return *reinterpret_cast<const volatile int32_t*>(m_call + 1);
}

bool XivAlexander::Misc::Hooks::CallSitePatch::Exchange(int32_t from, int32_t to) {
	DWORD oldProtect;
	if (!VirtualProtect(m_call + 1, sizeof to, PAGE_EXECUTE_READWRITE, &oldProtect))
		return false;
	const auto previous = InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(m_call + 1), to, from);
	VirtualProtect(m_call + 1, sizeof to, oldProtect, &oldProtect);
	FlushInstructionCache(GetCurrentProcess(), m_call, 5);
	return previous == from;
}

void XivAlexander::Misc::Hooks::CallSitePatch::SetStubDestination(const void* destination) {
	DWORD oldProtect;
	VirtualProtect(m_stub, CallSiteStubSize, PAGE_READWRITE, &oldProtect);
	*reinterpret_cast<const void* volatile*>(m_stub + 8) = destination;
	VirtualProtect(m_stub, CallSiteStubSize, PAGE_EXECUTE_READ, &oldProtect);
	FlushInstructionCache(GetCurrentProcess(), m_stub, CallSiteStubSize);
}
