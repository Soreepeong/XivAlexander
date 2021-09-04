#pragma once

#if INTPTR_MAX == INT64_MAX
#include <cstdint>
#include <XivAlexander/XivAlexander.h>

#pragma pack(push, 1)
struct EntryPointThunkTemplate {
	struct DUMMYSTRUCTNAME {
		struct {
			const uint8_t op_mov_rdi[2]{ 0x48, 0xbf };
			void* ptr = nullptr;
		} fn;
		const uint8_t op_call_rdi[2]{ 0xff, 0xd7 };
	} CallTrampoline;
};

struct TrampolineTemplate {
	const struct {
		const uint8_t op_sub_rsp_imm[3]{ 0x48, 0x81, 0xec };
		const uint32_t length = 0x80;
	} stack_alloc;

	struct DUMMYSTRUCTNAME {
		struct {
			const uint8_t op_mov_rcx_imm[2]{ 0x48, 0xb9 };
			void* val = nullptr;
		} lpLibFileName;

		struct {
			const uint8_t op_mov_rdi_imm[2]{ 0x48, 0xbf };
			void* ptr = nullptr;
		} fn;

		const uint8_t op_call_rdi[2]{ 0xff, 0xd7 };
	} CallLoadLibrary;

	struct {
		const uint8_t hModule_op_mov_rcx_rax[3]{ 0x48, 0x89, 0xc1 };

		struct {
			const uint8_t op_mov_rdx_imm[2]{ 0x48, 0xba };
			void* val = nullptr;
		} lpProcName;

		struct {
			const uint8_t op_mov_rdi_imm[2]{ 0x48, 0xbf };
			void* ptr = nullptr;
		} fn;

		const uint8_t op_call_rdi[2]{ 0xff, 0xd7 };
	} CallGetProcAddress;

	struct {
		const uint8_t op_add_rsp_imm[3]{ 0x48, 0x81, 0xc4 };
		const uint32_t length = 0x80;
	} stack_release;

	struct DUMMYSTRUCTNAME2 {
		// rdi := returned value from GetProcAddress
		const uint8_t op_mov_rdi_rax[3]{ 0x48, 0x89, 0xc7 };
		// rax := return address
		const uint8_t op_pop_rax[1]{ 0x58 };
		// rax := rax - sizeof thunk (last instruction must be call)
		struct {
			const uint8_t op_sub_rax_imm4[2]{ 0x48, 0x2d };
			const uint32_t displacement = static_cast<uint32_t>(sizeof EntryPointThunkTemplate);
		} op_sub_rax_to_entry_point;

		const uint8_t op_mov_esi_esp[2]{ 0x89, 0xe6 };

		struct {
			const uint8_t op_mov_rcx_imm[2]{ 0x48, 0xb9 };
			void* val = nullptr;
		} param;

		const uint8_t op_push_rax[1]{ 0x50 };
		const uint8_t op_jmp_rdi[2]{ 0xff, 0xe7 };
	} CallInjectEntryPoint;

	const char buf_CallGetProcAddress_lpProcName[20] = "XA_InjectEntryPoint";
	uint8_t buf_EntryPointBackup[sizeof EntryPointThunkTemplate]{};

#pragma pack(push, 8)
	XivAlexDll::InjectEntryPointParameters parameters{};
#pragma pack(pop)
};
#pragma pack(pop)

#endif
