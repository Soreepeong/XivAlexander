#pragma once

#if INTPTR_MAX == INT32_MAX
#include <cstdint>
#include <XivAlexander/XivAlexander.h>

#pragma pack(push, 1)
struct EntryPointThunkTemplate {
	struct DUMMYSTRUCTNAME {
		struct {
			const uint8_t op_mov_edi[1]{ 0xbf };
			void* ptr = nullptr;
		} fn;
		const uint8_t op_call_edi[2]{ 0xff, 0xd7 };
	} CallTrampoline;
};

struct TrampolineTemplate {
	const struct {
		const uint8_t op_sub_esp_imm[2]{ 0x81, 0xec };
		const uint32_t length = 0x80;
	} stack_alloc;

	struct DUMMYSTRUCTNAME {
		struct {
			const uint8_t op_push_imm[1]{ 0x68 };
			void* val = nullptr;
		} lpLibFileName;

		struct {
			const uint8_t op_mov_edi_imm[1]{ 0xbf };
			decltype(&LoadLibraryW) ptr = nullptr;
		} fn;

		const uint8_t op_call_edi[2]{ 0xff, 0xd7 };
	} CallLoadLibrary;

	struct {
		struct {
			const uint8_t op_push_imm[1]{ 0x68 };
			void* val = nullptr;
		} lpProcName;

		const uint8_t hModule_op_push_eax[1]{ 0x50 };

		struct {
			const uint8_t op_mov_edi_imm[1]{ 0xbf };
			decltype(&GetProcAddress) ptr = nullptr;
		} fn;

		const uint8_t op_call_edi[2]{ 0xff, 0xd7 };
	} CallGetProcAddress;

	struct {
		const uint8_t op_add_esp_imm[2]{ 0x81, 0xc4 };
		const uint32_t length = 0x80;
	} stack_release;

	struct DUMMYSTRUCTNAME2 {
		// edi := returned value from GetProcAddress
		const uint8_t op_mov_edi_eax[2]{ 0x89, 0xc7 };
		// eax := return address
		const uint8_t op_pop_eax[1]{ 0x58 };
		// eax := eax - sizeof thunk (last instruction must be call)
		struct {
			const uint8_t op_sub_eax[1]{ 0x2d };
			const uint32_t displacement = static_cast<uint32_t>(sizeof EntryPointThunkTemplate);
		} op_sub_eax_to_entry_point;

		struct {
			const uint8_t op_push_imm[1]{ 0x68 };
			void* val = nullptr;
		} param;

		const uint8_t op_push_eax[1]{ 0x50 };

		const uint8_t op_jmp_edi[2]{ 0xff, 0xe7 };
	} CallInjectEntryPoint;

	const char buf_CallGetProcAddress_lpProcName[20] = "XA_InjectEntryPoint";
	uint8_t buf_EntryPointBackup[sizeof EntryPointThunkTemplate]{};

#pragma pack(push, 4)
	XivAlexDll::InjectEntryPointParameters parameters{};
#pragma pack(pop)
};
#pragma pack(pop)

#endif
