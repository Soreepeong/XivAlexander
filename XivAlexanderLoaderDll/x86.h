#pragma once

#include <cstdint>

// Built by both the 32-bit DLL and the 64-bit loader, so addresses are explicit 32-bit integers.
namespace XivAlexander::EntryPoint::X86 {
	using Pointer = uint32_t;

#pragma pack(push, 1)
	// Dll::InjectEntryPointParameters as laid out in a 32-bit process.
	struct InjectEntryPointParameters {
		Pointer EntryPoint;
		Pointer EntryPointOriginalBytes;
		Pointer EntryPointOriginalLength;
		Pointer TrampolineAddress;
		bool LoadedAsProxy;
		uint8_t Padding[3];
	};

	struct EntryPointThunkTemplate {
		struct DUMMYSTRUCTNAME {
			struct {
				const uint8_t op_mov_edi[1]{ 0xbf };
				Pointer ptr = 0;
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
				Pointer val = 0;
			} lpLibFileName;

			struct {
				const uint8_t op_mov_edi_imm[1]{ 0xbf };
				Pointer ptr = 0;
			} fn;

			const uint8_t op_call_edi[2]{ 0xff, 0xd7 };
		} CallLoadLibrary;

		struct {
			struct {
				const uint8_t op_push_imm[1]{ 0x68 };
				Pointer val = 0;
			} lpProcName;

			const uint8_t hModule_op_push_eax[1]{ 0x50 };

			struct {
				const uint8_t op_mov_edi_imm[1]{ 0xbf };
				Pointer ptr = 0;
			} fn;

			const uint8_t op_call_edi[2]{ 0xff, 0xd7 };
		} CallGetProcAddress;

		struct {
			const uint8_t op_add_esp_imm[2]{ 0x81, 0xc4 };
			const uint32_t length = 0x80;
		} stack_release;

		struct DUMMYSTRUCTNAME2 {
			const uint8_t op_mov_edi_eax[2]{ 0x89, 0xc7 };
			const uint8_t op_pop_eax[1]{ 0x58 };
			struct {
				const uint8_t op_sub_eax[1]{ 0x2d };
				const uint32_t displacement = static_cast<uint32_t>(sizeof(EntryPointThunkTemplate));
			} op_sub_eax_to_entry_point;

			struct {
				const uint8_t op_push_imm[1]{ 0x68 };
				Pointer val = 0;
			} param;

			const uint8_t op_push_eax[1]{ 0x50 };

			const uint8_t op_jmp_edi[2]{ 0xff, 0xe7 };
		} CallInjectEntryPoint;

		const char buf_CallGetProcAddress_lpProcName[20] = "XA_InjectEntryPoint";
		uint8_t buf_EntryPointBackup[sizeof(EntryPointThunkTemplate)]{};

		InjectEntryPointParameters parameters{};
	};
#pragma pack(pop)

	static_assert(sizeof(InjectEntryPointParameters) == 20);
}
