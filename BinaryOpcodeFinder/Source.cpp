#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <span>
#include <map>
#include <vector>
#include <nlohmann/json.hpp>
#include <Zydis/Zydis.h>

size_t get_base_address(const std::filesystem::path& path) {
	std::ifstream in(path, std::ios::binary);
	IMAGE_DOS_HEADER dosh{};
	IMAGE_NT_HEADERS nth{};
	in.read(reinterpret_cast<char*>(&dosh), sizeof dosh);
	in.seekg(dosh.e_lfanew, std::ios::beg);
	in.read(reinterpret_cast<char*>(&nth), sizeof nth);
	return nth.OptionalHeader.ImageBase;
}

int wmain(int argc, wchar_t** argv) {
	if (argc < 2 || (argc < 4 && wcsncmp(argv[1], L"diff", 5) == 0 || (argc < 4 && wcsncmp(argv[1], L"find", 5) == 0))) {
		std::wcerr << L"Usage:" << std::endl;
#if INTPTR_MAX == INT64_MAX
		std::wcerr << std::format(L"{} diff path/to/old/ffxiv_dx11.exe path/to/new/ffxiv_dx11.exe", argv[0]) << std::endl;
		std::wcerr << std::format(L"{} find path/to/old/ffxiv_dx11.exe 0x1234", argv[0]) << std::endl;
#elif INTPTR_MAX == INT32_MAX
		std::wcerr << std::format(L"{} diff path/to/old/ffxiv.exe path/to/new/ffxiv.exe", argv[0]) << std::endl;
		std::wcerr << std::format(L"{} find path/to/old/ffxiv.exe 0x1234", argv[0]) << std::endl;
#endif
		return -1;
	}

	ZydisDecoder decoder;
#if INTPTR_MAX == INT64_MAX
	const std::string pattern("\x48\x89\x5c\x24\x18\x56\x48\x83\xec\x50\x8b\xf2\x49\x8b\xd8\x41\x0f\xb7\x50\x02");
	const auto opcodeMinOffset = 0x21;
	const auto opcodeCountOffset = 0x23;
	const auto switchTableOffset = 0x3F;
	ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_ADDRESS_WIDTH_64);

#elif INTPTR_MAX == INT32_MAX
	const std::string pattern("\x55\x8b\xec\x53\x8b\x5d\x08\x56\x8b\x75\x0c\x0f\xb7\x46\x02\x50\x53\xe8");
	const auto opcodeMinOffset = 0x1F;
	const auto opcodeCountOffset = 0x21;
	const auto switchTableOffset = 0x2F;
	ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_COMPAT_32, ZYDIS_ADDRESS_WIDTH_32);

#endif

	const auto pModA = (const uint8_t*)LoadLibraryW(argv[2]);
	if (!pModA) {
		std::wcerr << std::format(L"Not found: {}", argv[2]) << std::endl;
		return -1;
	}
	const auto baseA = get_base_address(argv[2]);
	const auto& doshA = *(IMAGE_DOS_HEADER*)&pModA[0];
	const auto& nthA = *(IMAGE_NT_HEADERS*)&pModA[doshA.e_lfanew];
	const auto& sectA = *(IMAGE_SECTION_HEADER*)(&(&nthA)[1]);
	const auto textA = std::span((char*)&pModA[sectA.VirtualAddress], sectA.SizeOfRawData);

	if (wcsncmp(argv[1], L"find", 5) == 0) {
		ZydisFormatter formatter;
		ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_INTEL);
#if INTPTR_MAX == INT64_MAX
		std::cout << std::format("{:>16}\t{:>16}\t{}", "VA", "RVA", "Assembly") << std::endl;
#elif INTPTR_MAX == INT32_MAX
		std::cout << std::format("{:>8}\t{:>8}\t{}", "VA", "RVA", "Assembly") << std::endl;
#endif

		int val = std::wcstol(argv[3], nullptr, 0);
		char buffer[256];
		for (size_t i = 0; i < textA.size();) {
			ZydisDecodedInstruction inst;
			if (!ZYAN_SUCCESS(ZydisDecoderDecodeBuffer(&decoder, &textA[i], 1024, &inst))) {
				i++;
				continue;
			}
			if (inst.mnemonic == ZYDIS_MNEMONIC_MOV && inst.operand_count == 2 && inst.operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && inst.operands[1].imm.value.s == val) {
				ZydisFormatterFormatInstruction(&formatter, &inst, buffer, sizeof(buffer), baseA);
#if INTPTR_MAX == INT64_MAX
				std::cout << std::format("{:>16X}\t{:>16X}\t{}", (size_t)(baseA + ((uint8_t*)&textA[i] - pModA)), (uint8_t*)&textA[i] - pModA, buffer) << std::endl;
#elif INTPTR_MAX == INT32_MAX
				std::cout << std::format("{:>8X}\t{:>8X}\t{}", (size_t)(baseA + ((uint8_t*)&textA[i] - pModA)), (uint8_t*)&textA[i] - pModA, buffer) << std::endl;
#endif
			}
			i += inst.length;
		}
		std::cout << std::endl;

	} else if (wcsncmp(argv[1], L"diff", 5) == 0) {
		const auto switchA = &std::search(textA.begin(), textA.end(), pattern.begin(), pattern.end())[0];
		const auto minOpcodeA = (int)-*(char*)(switchA + opcodeMinOffset);
		const auto maxOpcodeA = minOpcodeA + *(int*)(switchA + opcodeCountOffset);
		std::map<int, std::set<int>> fnToOpcodeMapA;
#if INTPTR_MAX == INT64_MAX
		const auto switchTableA = std::span((const int*)(pModA + *(int*)(switchA + switchTableOffset)), maxOpcodeA - minOpcodeA);
		for (int i = 0; i < static_cast<int>(switchTableA.size()); i++)
			fnToOpcodeMapA[switchTableA[i]].insert(minOpcodeA + i);
#elif INTPTR_MAX == INT32_MAX
		const auto switchTableA = std::span((const int*)*(int*)(switchA + switchTableOffset), maxOpcodeA - minOpcodeA);
		for (int i = 0; i < static_cast<int>(switchTableA.size()); i++)
			fnToOpcodeMapA[switchTableA[i] - reinterpret_cast<int>(pModA)].insert(i + minOpcodeA);
#endif

		const auto pModB = (const uint8_t*)LoadLibraryW(argv[3]);
		if (!pModB) {
			std::wcerr << std::format(L"Not found: {}", argv[3]) << std::endl;
			return -2;
		}

		const auto baseB = get_base_address(argv[3]);
		const auto& doshB = *(IMAGE_DOS_HEADER*)&pModB[0];
		const auto& nthB = *(IMAGE_NT_HEADERS*)&pModB[doshB.e_lfanew];
		const auto& sectB = *(IMAGE_SECTION_HEADER*)(&(&nthB)[1]);
		const auto textB = std::span((char*)&pModB[sectB.VirtualAddress], sectB.SizeOfRawData);
		const auto switchB = &std::search(textB.begin(), textB.end(), pattern.begin(), pattern.end())[0];
		const auto minOpcodeB = (int)-*(char*)(switchB + opcodeMinOffset);
		const auto maxOpcodeB = minOpcodeB + *(int*)(switchB + opcodeCountOffset);
		std::map<int, std::set<int>> fnToOpcodeMapB;
#if INTPTR_MAX == INT64_MAX
		const auto switchTableB = std::span((const int*)(pModB + *(int*)(switchB + switchTableOffset)), maxOpcodeB - minOpcodeB);
		for (int i = 0; i < static_cast<int>(switchTableB.size()); i++)
			fnToOpcodeMapB[switchTableB[i]].insert(i + minOpcodeB);
#elif INTPTR_MAX == INT32_MAX
		const auto switchTableB = std::span((const int*)*(int*)(switchB + switchTableOffset), maxOpcodeB - minOpcodeB);
		for (int i = 0; i < static_cast<int>(switchTableB.size()); i++)
			fnToOpcodeMapB[switchTableB[i] - reinterpret_cast<int>(pModB)].insert(i + minOpcodeB);
#endif

		auto res = nlohmann::json::array();
		for (const auto& [offA, opAs] : fnToOpcodeMapA) {
			std::vector<int> candidates;
			size_t opptr = 0;
			candidates.reserve(fnToOpcodeMapB.size());
			for (const auto& [offB, opBs] : fnToOpcodeMapB) {
				if (pModA[offA] != pModB[offB])
					continue;
				candidates.emplace_back(offB);
			}

			ZydisDecodedInstruction instA;
			ZydisDecodedInstruction instB;
			std::vector<size_t> callOffsets;
			while (candidates.size() > 1) {
				if (!ZYAN_SUCCESS(ZydisDecoderDecodeBuffer(&decoder, &pModA[offA + opptr], 1024, &instA)))
					break;

				for (auto it = candidates.begin(); it != candidates.end();) {
					const auto& offB = *it;
					const auto& opBs = fnToOpcodeMapB[offB];

					auto fail = false;

					if (!ZYAN_SUCCESS(ZydisDecoderDecodeBuffer(&decoder, &pModB[offB + opptr], 1024, &instB)))
						break;

					if (instA.opcode != instB.opcode || instA.operand_count != instB.operand_count) {
						fail = true;
					}

					for (size_t i = 0; !fail && i < instA.operand_count; i++) {
						const auto& opA = instA.operands[i];
						const auto& opB = instB.operands[i];
						if (opA.type != opB.type) {
							fail = true;
							break;
						}

						switch (opA.type) {
							case ZYDIS_OPERAND_TYPE_REGISTER:
								if (opA.reg.value != opB.reg.value)
									fail = true;
								break;
							case ZYDIS_OPERAND_TYPE_MEMORY:
								if (opA.mem.base != opB.mem.base)
									fail = true;
								break;
							case ZYDIS_OPERAND_TYPE_POINTER:
								if (opA.ptr.offset != opB.ptr.offset || opA.ptr.segment != opB.ptr.segment)
									fail = true;
								break;
							case ZYDIS_OPERAND_TYPE_IMMEDIATE:
								if (opA.imm.is_relative != opB.imm.is_relative)
									fail = true;
								else if (!opA.imm.is_relative && !opB.imm.is_relative && opA.imm.value.s != opB.imm.value.s)
									fail = true;
								break;
						}
					}

					if (fail)
						it = candidates.erase(it);
					else
						++it;
				}

				// unconditional jump
				if (instA.meta.category == ZYDIS_CATEGORY_UNCOND_BR)
					break;

				// unconditional call
				if (instA.meta.category == ZYDIS_CATEGORY_CALL)
					callOffsets.push_back(opptr);

				opptr += instA.length;
			}

			if (candidates.size() > 1 && !callOffsets.empty()) {
				for (const auto& callOffset : callOffsets) {
					int len1 = 0;
					if (!ZYAN_SUCCESS(ZydisDecoderDecodeBuffer(&decoder, &pModA[offA + callOffset], 1024, &instA)))
						continue;

					uint64_t nSubBase1;
					if (!ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&instA, &instA.operands[0], reinterpret_cast<size_t>(&pModA[offA + callOffset]), &nSubBase1)))
						continue;
					const auto pSubBase1 = reinterpret_cast<const char*>(nSubBase1);
					for (int i = 0, j = 0; i < 1024 && j < 8; j++) {
						if (!ZYAN_SUCCESS(ZydisDecoderDecodeBuffer(&decoder, &pSubBase1[i], 1024, &instA)))
							break;
						if (instA.mnemonic == ZYDIS_MNEMONIC_MOV && instA.operand_count == 2 && instA.operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
							len1 = (int)instA.operands[1].imm.value.s;
							break;
						}
						i += instA.length;
					}
					if (len1 == 0)
						continue;

					for (auto it = candidates.begin(); it != candidates.end();) {
						const auto& offB = *it;

						int len2 = 0;
						if (!ZYAN_SUCCESS(ZydisDecoderDecodeBuffer(&decoder, &pModB[offB + callOffset], 1024, &instB)))
							continue;

						uint64_t nSubBase2;
						if (!ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&instB, &instB.operands[0], reinterpret_cast<size_t>(&pModB[offB + callOffset]), &nSubBase2)))
							continue;
						const auto pSubBase2 = reinterpret_cast<const char*>(nSubBase2);
						for (int i = 0, j = 0; i < 1024 && j < 8; j++) {
							if (!ZYAN_SUCCESS(ZydisDecoderDecodeBuffer(&decoder, &pSubBase2[i], 1024, &instB)))
								break;
							if (instB.mnemonic == ZYDIS_MNEMONIC_MOV && instB.operand_count == 2 && instB.operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
								len2 = (int)instB.operands[1].imm.value.s;
								break;
							}
							i += instB.length;
						}

						if (len2 != len1)
							it = candidates.erase(it);
						else
							++it;
					}
				}
			}

			auto& opcodeItem = res.emplace_back(nlohmann::json::object());

			opcodeItem["rva1"] = offA;
			opcodeItem["rva1h"] = std::format("0x{:x}", offA);
			opcodeItem["va1"] = baseA + offA;
			opcodeItem["va1h"] = std::format("0x{:x}", baseA + offA);
			opcodeItem["opcodes1"] = opAs;
			{
				auto& opctarget = opcodeItem["opcodes1h"] = nlohmann::json::array();
				for (const auto& x : opAs)
					opctarget.emplace_back(std::format("0x{:x}", x));
			}

			for (const auto& offB : candidates) {
				const auto& opBs = fnToOpcodeMapB[offB];
				opcodeItem["rva1"] = offB;
				opcodeItem["rva1h"] = std::format("0x{:x}", offB);
				opcodeItem["va1"] = baseB + offB;
				opcodeItem["va1h"] = std::format("0x{:x}", baseB + offB);
				opcodeItem["opcodes2"] = opBs;
				auto& opctarget = opcodeItem["opcodes2h"] = nlohmann::json::array();
				for (const auto& x : opBs)
					opctarget.emplace_back(std::format("0x{:x}", x));
			}
		}

		std::cout << res.dump(1, '\t') << std::endl;
	}
	return 0;
}