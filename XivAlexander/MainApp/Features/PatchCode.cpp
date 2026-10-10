#include "pch.h"
#include "PatchCode.h"

#include "Config.h"
#include "Apps/MainApp/App.h"
#include "Misc/Logger.h"

struct XivAlexander::Apps::MainApp::Internal::PatchCode::Implementation {
	App& App;
	const std::shared_ptr<Config> Config;
	const std::shared_ptr<Misc::Logger> Logger;
	
	std::set<std::string> EnabledPatchCodes;
	std::map<char*, char> OriginalCodes;

	Utils::CallOnDestruction::Multiple Cleanup;
	
	Implementation(MainApp::App& app)
		: App(app)
		, Config(Config::Acquire())
		, Logger(Misc::Logger::Acquire()) {

		Cleanup += Config->Game.PatchCode.OnChange([&] { Apply(); });
		Cleanup += Config->Runtime.EnabledPatchCodes.OnChange([&] { Apply(); });
		Apply();
	}

	~Implementation() {
		Cleanup.Clear();
		Unapply();
	}

	void Unapply() {
		if (OriginalCodes.empty())
			return;

		Logger->Format<LogLevel::Info>(LogCategory::PatchCode, "Patch cleared");
		for (const auto& [p1, p2] : OriginalCodes) {
			DWORD old;
			if (!VirtualProtect(p1, 1, PAGE_EXECUTE_READWRITE, &old))
				continue;
			*p1 = p2;
			VirtualProtect(p1, 1, old, &old);
		}
		OriginalCodes.clear();
	}
	
	void Apply() {
		ZydisDecoder decoder;
#if INTPTR_MAX == INT64_MAX
		ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
#elif INTPTR_MAX == INT32_MAX
		ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_COMPAT_32, ZYDIS_STACK_WIDTH_32);
#endif

		Unapply();
		
		const auto& digestsVector = Config->Runtime.EnabledPatchCodes.Value();
		std::set digests(digestsVector.begin(), digestsVector.end());
		
		for (auto& p : Config->Game.PatchCode.Value()) {
			if (!digests.contains(p.Digest()))
				continue;
#ifdef _WIN64
			const auto& searchInstructions = p.X64;
#else
			const auto& searchInstructions = p.X86;
#endif
			try {
				char* pointer = nullptr;
				for (const auto& instruction : searchInstructions) {
					if (instruction.at(0) == "find") {
						std::string pattern;
						std::string mask;
						{
							const auto& raw = instruction.at(3);
							bool bHighByte = true;
							for (size_t i = 0; i < raw.size(); i++) {
								int n = -1;
								if ('0' <= raw[i] && raw[i] <= '9')
									n = raw[i] - '0';
								else if ('a' <= raw[i] && raw[i] <= 'f')
									n = 10 + raw[i] - 'a';
								else if ('A' <= raw[i] && raw[i] <= 'F')
									n = 10 + raw[i] - 'A';
								else if (raw[i] == '?' && i + 1 < raw.size() && raw[i + 1] == '?') {
									i++;
									n = -2;
								} else if (raw[i] == '?')
									n = -2;

								if (n == -1)
									continue;
								else if (n == -2) {
									bHighByte = true;
									pattern.push_back(0);
									mask.push_back(0);
									continue;
								}

								if ((bHighByte = !bHighByte)) {
									pattern.back() |= static_cast<char>(n);
								} else {
									pattern.push_back(static_cast<char>(n << 4));
									mask.push_back(static_cast<char>(0xFF));
								}
							}
						}
						
						std::vector<std::span<char>> ranges;
						if (instruction.at(1) == "code") {
							const auto pBase = reinterpret_cast<char*>(GetModuleHandleW(nullptr));
							const auto pDosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(pBase);
							const auto pNtHeader = reinterpret_cast<IMAGE_NT_HEADERS*>(pBase + pDosHeader->e_lfanew);
							const auto pSections = IMAGE_FIRST_SECTION(pNtHeader);
							for (WORD i = 0; i < pNtHeader->FileHeader.NumberOfSections; i++) {
								if (pSections[i].Characteristics & IMAGE_SCN_CNT_CODE)
									ranges.emplace_back(pBase + pSections[i].VirtualAddress, pSections[i].Misc.VirtualSize);
							}
						}

						std::vector<char*> result;
						for (const auto& range : ranges) {
							const auto nUpperLimit = range.size() - pattern.length();
							for (size_t i = 0; i < nUpperLimit; ++i) {
								for (size_t j = 0; j < pattern.length(); ++j) {
									if ((range[i + j] & mask[j]) != (pattern[j] & mask[j]))
										goto next_char;
								}
								result.push_back(&range[i]);
								next_char:;
							}
						}

						pointer = result.at(strtol(instruction.at(2).data(), nullptr, 0));
						
					} else if (instruction.at(0) == "offset") {
						pointer += strtol(instruction.at(1).data(), nullptr, 0);

					} else if (instruction.at(0) == "resolve_ip") {
						ZydisDecodedInstruction inst;
						ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
						if (IsBadReadPtr(pointer, 16))
							throw std::runtime_error(std::format("0x{:X}: bad read ptr", reinterpret_cast<size_t>(pointer)));
						if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, pointer, 16, &inst, operands)))
							throw std::runtime_error(std::format("0x{:X}: bad instruction", reinterpret_cast<size_t>(pointer)));
						if ((inst.meta.category == ZYDIS_CATEGORY_UNCOND_BR || inst.meta.category == ZYDIS_CATEGORY_COND_BR || inst.meta.category == ZYDIS_CATEGORY_CALL) && operands[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
							if (uint64_t resultAddress; ZYAN_STATUS_SUCCESS == ZydisCalcAbsoluteAddress(&inst, &operands[0], reinterpret_cast<size_t>(pointer), &resultAddress)) {
								pointer = reinterpret_cast<char*>(static_cast<size_t>(resultAddress));
							} else {
								throw std::runtime_error(std::format("0x{:X}: failed to resolve branch target", reinterpret_cast<size_t>(pointer)));
							}
						} else {
							throw std::runtime_error(std::format("0x{:X}: not a branch or call", reinterpret_cast<size_t>(pointer)));
						}
						
					} else if (instruction.at(0) == "write") {
						std::vector<char> patchBytes;
						for (size_t i = 1; i < instruction.size(); i++) {
							for (const auto& part : Utils::StringSplit<std::string>(instruction[i], " ")) {
								if (part.empty())
									continue;
								patchBytes.push_back(static_cast<char>(strtol(part.data(), nullptr, 0)));
							}
						}

						MEMORY_BASIC_INFORMATION mbi;
						for (auto remaining = patchBytes.size(); remaining > 0; ) {
							if (!VirtualQuery(pointer, &mbi, sizeof mbi))
								throw std::runtime_error(std::format("VirtualQuery(0x{:X}) failure: {}", reinterpret_cast<size_t>(pointer), GetLastError()));
							const auto available = std::min<size_t>(remaining, static_cast<char*>(mbi.BaseAddress) + mbi.RegionSize - pointer);

							DWORD oldProtect;
							if (!VirtualProtect(mbi.BaseAddress, mbi.RegionSize, PAGE_EXECUTE_READWRITE, &oldProtect))
								throw std::runtime_error(std::format("VirtualProtect#1(0x{:X}) failure: {}", reinterpret_cast<size_t>(pointer), GetLastError()));

							Logger->Format<LogLevel::Info>(LogCategory::PatchCode, "{}: Writing to 0x{:X} (0x{:X} bytes)", p.Name, reinterpret_cast<size_t>(pointer), available);

							for (size_t i = 0; i < available; i++) {
								OriginalCodes[pointer + i] = pointer[i];
								pointer[i] = patchBytes[patchBytes.size() - remaining + i];
							}

							if (!VirtualProtect(mbi.BaseAddress, mbi.RegionSize, oldProtect, &oldProtect))
								throw std::runtime_error(std::format("VirtualProtect#2(0x{:X}) failure: {}", reinterpret_cast<size_t>(pointer), GetLastError()));

							remaining -= available;
							pointer += available;
						}
						
					} else {
						throw std::runtime_error(std::format("invalid instruction {}", instruction[0]));
					}
				}

				Logger->Format<LogLevel::Info>(LogCategory::PatchCode, "{}: Patch applied", p.Name);

			} catch (const std::exception& e){
				Logger->Format<LogLevel::Error>(LogCategory::PatchCode, "{}: Error: {}", p.Name, e.what());
			}
		}

	}
};

XivAlexander::Apps::MainApp::Internal::PatchCode::PatchCode(App& app)
	: m_pImpl(std::make_unique<Implementation>(app)) {
}

XivAlexander::Apps::MainApp::Internal::PatchCode::~PatchCode() = default;
