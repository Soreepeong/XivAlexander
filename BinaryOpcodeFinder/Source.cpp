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
		std::set<int> unusedOpcodesB;
		std::set<size_t> claimedOffsetsB;
#if INTPTR_MAX == INT64_MAX
		const auto switchTableB = std::span((const int*)(pModB + *(int*)(switchB + switchTableOffset)), maxOpcodeB - minOpcodeB);
		for (int i = 0; i < static_cast<int>(switchTableB.size()); i++)
			fnToOpcodeMapB[switchTableB[i]].insert(i + minOpcodeB);
#elif INTPTR_MAX == INT32_MAX
		const auto switchTableB = std::span((const int*)*(int*)(switchB + switchTableOffset), maxOpcodeB - minOpcodeB);
		for (int i = 0; i < static_cast<int>(switchTableB.size()); i++)
			fnToOpcodeMapB[switchTableB[i] - reinterpret_cast<int>(pModB)].insert(i + minOpcodeB);
#endif
		for (const auto& [k, v] : fnToOpcodeMapB)
			unusedOpcodesB.insert(v.begin(), v.end());

		auto res = nlohmann::json::array();
		struct candidate_info_t {
			int BaseOffset = 0;
			bool MarkedForRemoval = false;
			std::vector<size_t> Offset;
		};
		std::vector<candidate_info_t> candidates;
		std::vector<size_t> opptrA;
		for (const auto& [offA, opAs] : fnToOpcodeMapA) {
			candidates.clear();
			opptrA.clear();
			opptrA.push_back(static_cast<size_t>(offA));

			candidates.reserve(fnToOpcodeMapB.size());
			for (const auto& [offB, opBs] : fnToOpcodeMapB) {
				if (pModA[offA] != pModB[offB])
					continue;

				candidates.emplace_back().BaseOffset = offB;
				candidates.back().Offset.emplace_back(static_cast<size_t>(offB));
			}

			/*if (opAs.contains(0x354))
				__debugbreak();*/

			ZydisDecodedInstruction instA;
			ZydisDecodedInstruction instB;
			while (candidates.size() > 1 && !opptrA.empty()) {
				if (!ZYAN_SUCCESS(ZydisDecoderDecodeBuffer(&decoder, &pModA[opptrA.back()], 1024, &instA)))
					break;

				auto remaining = candidates.size();
				for (auto & candidate : candidates) {
					const auto& offB = candidate;
					auto& opptrB = candidate.Offset;
					const auto& opBs = fnToOpcodeMapB[candidate.BaseOffset];

					auto fail = false;

					if (!ZYAN_SUCCESS(ZydisDecoderDecodeBuffer(&decoder, &pModB[opptrB.back()], 1024, &instB)))
						break;

					if (instA.opcode != instB.opcode || instA.operand_count != instB.operand_count) {
						fail = true;
					}

					if ((instB.meta.category == ZYDIS_CATEGORY_UNCOND_BR || instB.meta.category == ZYDIS_CATEGORY_COND_BR || instB.meta.category == ZYDIS_CATEGORY_CALL) && instB.operands[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
						void();  // don't care
#if INTPTR_MAX == INT64_MAX
					} else if (instB.mnemonic == ZYDIS_MNEMONIC_MOV
						&& instB.operand_count == 2
						&& instB.operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER
						&& instB.operands[0].reg.value == ZYDIS_REGISTER_R8D
						&& instB.operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE
						&& opAs.contains(static_cast<int>(instA.operands[1].imm.value.s))
						&& opBs.contains(static_cast<int>(instB.operands[1].imm.value.s))) {
						void();  // redirect to a common function accepting opcode as its 3rd parameter
#elif INTPTR_MAX == INT32_MAX
					} else if (instB.mnemonic == ZYDIS_MNEMONIC_PUSH
						&& instB.operand_count == 1
						&& instB.operands[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE
						&& opAs.contains(static_cast<int>(instA.operands[0].imm.value.s))
						&& opBs.contains(static_cast<int>(instB.operands[0].imm.value.s))) {
						void();  // redirect to a common function accepting opcode as its 3rd parameter
#endif
					} else if (instB.mnemonic == ZYDIS_MNEMONIC_CMP
						&& instB.operand_count >= 2
						&& instB.operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER
						&& instB.operands[0].reg.value == ZYDIS_REGISTER_EAX
						&& instB.operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE
						&& opAs.contains(static_cast<int>(instA.operands[1].imm.value.s))
						&& opBs.contains(static_cast<int>(instB.operands[1].imm.value.s))) {
						void();  // CMP EAX, <opcode>
					} else {
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
									else if (opA.mem.index != opB.mem.index)
										fail = true;
									else if (opA.mem.scale != opB.mem.scale)
										fail = true;
									else if (opA.mem.disp.has_displacement != opB.mem.disp.has_displacement)
										fail = true;
									else if (opA.mem.disp.has_displacement && opB.mem.disp.has_displacement
										&& opA.mem.disp.value != opB.mem.disp.value
#if INTPTR_MAX == INT64_MAX
										&& opA.mem.base != ZYDIS_REGISTER_RIP
#elif INTPTR_MAX == INT32_MAX
										&& opA.mem.base != ZYDIS_REGISTER_EIP
#endif
										)
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
					}

					if (fail) {
						candidate.MarkedForRemoval = true;
						remaining--;
					} else {
						if (instB.meta.category == ZYDIS_CATEGORY_UNCOND_BR || instB.meta.category == ZYDIS_CATEGORY_CALL) {
							uint64_t resaddr;
							if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&instB, &instB.operands[0], reinterpret_cast<size_t>(pModB) + opptrB.back(), &resaddr))) {
								if (instB.meta.category == ZYDIS_CATEGORY_UNCOND_BR) {
									opptrB.pop_back();
								} else {
									opptrB.back() += instB.length;
								}
								opptrB.push_back(resaddr - reinterpret_cast<size_t>(pModB));
							} else {
								if (instB.meta.category == ZYDIS_CATEGORY_CALL)
									opptrB.back() += instB.length;
								else
									candidate.MarkedForRemoval = true;
							}
						} else if (instB.meta.category == ZYDIS_CATEGORY_RET) {
							opptrB.pop_back();
							if (opptrB.empty()) {
								candidate.MarkedForRemoval = true;
								remaining--;
							}
						} else {
							opptrB.back() += instB.length;
						}
					}

					while (opptrB.size() > 2)
						opptrB.pop_back();
				}

				if (remaining == 0) {
					if (opptrA.empty())
						break;

					opptrA.pop_back();
					for (auto it = candidates.begin(); it != candidates.end();) {
						if (!it->MarkedForRemoval && it->Offset.size() <= 1) {
							it->MarkedForRemoval = true;
							remaining--;
						} else {
							if (!it->Offset.empty())
								it->Offset.pop_back();
							++it;
						}
					}

					if (remaining == 0)
						break;

					for (auto it = candidates.begin(); it != candidates.end();) {
						if (it->MarkedForRemoval)
							it = candidates.erase(it);
						else
							++it;
					}
					continue;
				}

				for (auto it = candidates.begin(); it != candidates.end();) {
					if (it->MarkedForRemoval)
						it = candidates.erase(it);
					else
						++it;
				}

				if (instA.meta.category == ZYDIS_CATEGORY_UNCOND_BR || instA.meta.category == ZYDIS_CATEGORY_CALL) {
					uint64_t resaddr;
					if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&instA, &instA.operands[0], reinterpret_cast<size_t>(pModA) + opptrA.back(), &resaddr))) {
						if (instA.meta.category == ZYDIS_CATEGORY_UNCOND_BR) {
							opptrA.pop_back();
						} else {
							opptrA.back() += instA.length;
						}
						opptrA.push_back(resaddr - reinterpret_cast<size_t>(pModA));
					} else {
						if (instA.meta.category == ZYDIS_CATEGORY_CALL)
							opptrA.back() += instA.length;
						else
							break;
					}
				} else if (instA.meta.category == ZYDIS_CATEGORY_RET) {
					opptrA.pop_back();
				} else {
					opptrA.back() += instA.length;
				}

				while (opptrA.size() > 2)
					opptrA.pop_back();
			}

			std::cerr << offA << std::endl;
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

			if (candidates.size() == 1) {
				for (const auto& offB : candidates) {
					const auto& opBs = fnToOpcodeMapB[offB.BaseOffset];
					opcodeItem["rva2"] = offB.BaseOffset;
					opcodeItem["rva2h"] = std::format("0x{:x}", offB.BaseOffset);
					opcodeItem["va2"] = baseB + offB.BaseOffset;
					opcodeItem["va2h"] = std::format("0x{:x}", baseB + offB.BaseOffset);
					opcodeItem["opcodes2"] = opBs;
					auto& opctarget = opcodeItem["opcodes2h"] = nlohmann::json::array();
					for (const auto& x : opBs) {
						opctarget.emplace_back(std::format("0x{:x}", x));
						unusedOpcodesB.erase(x);
					}
					claimedOffsetsB.insert(offB.BaseOffset);
				}
			} else {
				auto& candidatesArray = opcodeItem["candidates"] = nlohmann::json::array();
				for (const auto& offB : candidates) {
					const auto& opBs = fnToOpcodeMapB[offB.BaseOffset];
					auto& target = candidatesArray.emplace_back();
					target["rva"] = offB.BaseOffset;
					target["rvah"] = std::format("0x{:x}", offB.BaseOffset);
					target["va"] = baseB + offB.BaseOffset;
					target["vah"] = std::format("0x{:x}", baseB + offB.BaseOffset);
					target["opcodes"] = opBs;
					auto& opctarget = target["opcodesh"] = nlohmann::json::array();
					for (const auto& x : opBs) {
						opctarget.emplace_back(std::format("0x{:x}", x));
						unusedOpcodesB.erase(x);
					}
					target["stack"] = offB.Offset;
					auto& stacktarget = target["stackrvah"] = nlohmann::json::array();
					for (const auto& x : offB.Offset)
						stacktarget.emplace_back(std::format("0x{:x}", baseB + x));
				}
			}
		}

		for (size_t n = 0, m = claimedOffsetsB.size(); n != m; n = m, m = claimedOffsetsB.size()) {
			for (auto& v : res) {
				const auto it = v.find("candidates");
				if (it == v.end())
					continue;
				for (auto it2 = it->begin(); it2 != it->end(); ) {
					if (claimedOffsetsB.contains(it2->value<size_t>("rva", 0)))
						it2 = it->erase(it2);
					else
						++it2;
				}
				if (it->size() == 1) {
					claimedOffsetsB.insert(it->front().value<size_t>("rva", 0));

					v["rva2"] = it->front()["rva2"];
					v["rva2h"] = it->front()["rva2h"];
					v["va2"] = it->front()["va2"];
					v["va2h"] = it->front()["va2h"];
					v["opcodes2"] = it->front()["opcodes2"];
					v["opcodes2h"] = it->front()["opcodes2h"];

					v.erase(it);
				}
			}
		}

		res = nlohmann::json::object({
			{
				"found",
				std::move(res),
			},
			{
				"unused",
				std::vector(unusedOpcodesB.begin(), unusedOpcodesB.end()),
			}
			});
		std::cout << res.dump(1, '\t') << std::endl;
	}
	return 0;
}