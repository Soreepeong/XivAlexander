#pragma once

#include <cstdint>

namespace XivAlexander::EntryPoint::X64 {
	using Pointer = uint64_t;

#pragma pack(push, 1)
	// Dll::InjectEntryPointParameters as laid out in a 64-bit process.
	struct InjectEntryPointParameters {
		Pointer EntryPoint;
		Pointer EntryPointOriginalBytes;
		Pointer EntryPointOriginalLength;
		Pointer TrampolineAddress;
		bool LoadedAsProxy;
		uint8_t Padding[7];
	};

	struct EntryPointThunkTemplate {
		struct DUMMYSTRUCTNAME {
			struct {
				const uint8_t op_mov_rdi[2]{0x48, 0xbf};
				Pointer ptr = 0;
			} fn;

			const uint8_t op_call_rdi[2]{0xff, 0xd7};
		} CallTrampoline;
	};

	struct TrampolineTemplate {
		const struct {
			const uint8_t op_sub_rsp_imm[3]{ 0x48, 0x81, 0xec };
			const uint32_t length = 0x80;
		} stack_alloc;

		struct DUMMYSTRUCTNAME {
			struct {
				const uint8_t op_mov_rcx_imm[2]{0x48, 0xb9};
				Pointer val = 0;
			} lpLibFileName;

			struct {
				const uint8_t op_mov_rdi_imm[2]{0x48, 0xbf};
				Pointer ptr = 0;
			} fn;

			const uint8_t op_call_rdi[2]{0xff, 0xd7};
		} CallLoadLibrary;

		struct {
			const uint8_t hModule_op_mov_rcx_rax[3]{0x48, 0x89, 0xc1};

			struct {
				const uint8_t op_mov_rdx_imm[2]{0x48, 0xba};
				Pointer val = 0;
			} lpProcName;

			struct {
				const uint8_t op_mov_rdi_imm[2]{0x48, 0xbf};
				Pointer ptr = 0;
			} fn;

			const uint8_t op_call_rdi[2]{0xff, 0xd7};
		} CallGetProcAddress;

		struct DUMMYSTRUCTNAME2 {
			const uint8_t op_mov_rdi_rax[3]{0x48, 0x89, 0xc7};

			struct {
				const uint8_t op_mov_rax_ptr_rsp[4]{ 0x48, 0x8B, 0x84, 0x24 };
				const uint32_t length = 0x80;
			} read_return_address;

			struct {
				const uint8_t op_sub_rax_imm4[2]{0x48, 0x2d};
				const uint32_t displacement = sizeof(EntryPointThunkTemplate);
			} op_sub_rax_to_entry_point;

			struct {
				const uint8_t op_mov_ptr_rsp_rax[4]{ 0x48, 0x89, 0x84, 0x24 };
				const uint32_t length = 0x80;
			} write_return_address;

			const uint8_t op_mov_esi_esp[2]{0x89, 0xe6};

			struct {
				const uint8_t op_mov_rcx_imm[2]{0x48, 0xb9};
				Pointer val = 0;
			} param;

			const uint8_t op_call_rdi[2]{ 0xff, 0xd7 };
		} CallInjectEntryPoint;

		struct {
			const uint8_t op_add_rsp_imm[3]{ 0x48, 0x81, 0xc4 };
			const uint32_t length = 0x80;
		} stack_release;

		const uint8_t op_ret[1]{ 0xc3 };

		const char buf_CallGetProcAddress_lpProcName[20] = "XA_InjectEntryPoint";
		uint8_t buf_EntryPointBackup[sizeof(EntryPointThunkTemplate)]{};

		InjectEntryPointParameters parameters{};
	};
#pragma pack(pop)

	static_assert(sizeof(InjectEntryPointParameters) == 40);
}
