#include "pch.h"
#include "App_Misc_Signatures.h"

namespace App::Misc::Signatures {

	bool SectionFilterTextOnly(const IMAGE_SECTION_HEADER& pSectionHeader) {
		return 0 == strncmp(reinterpret_cast<const char*>(pSectionHeader.Name), ".text", 6);
	}

	std::vector<void*> LookupForData(SectionFilter lookupInSection, const char* sPattern, const char* sMask, size_t length, const std::vector<size_t> &nextOffsets) {
		std::vector<void*> result;
		const std::string_view mask(sMask, length);
		const std::string_view pattern(sPattern, length);

		const auto pBaseAddress = reinterpret_cast<const char*>(GetModuleHandleW(nullptr));
		const auto pDosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(pBaseAddress);
		const auto pNtHeader = reinterpret_cast<const IMAGE_NT_HEADERS*>(pBaseAddress + pDosHeader->e_lfanew);

		const auto pSectionHeaders = IMAGE_FIRST_SECTION(pNtHeader);
		for (size_t i = 0; i < pNtHeader->FileHeader.NumberOfSections; ++i) {
			if (lookupInSection(pSectionHeaders[i])) {
				std::string_view section(pBaseAddress + pSectionHeaders[i].VirtualAddress, pSectionHeaders[i].Misc.VirtualSize);
				const auto nUpperLimit = section.length() - pattern.length();
				for (size_t i = 0; i < nUpperLimit; ++i) {
					for (size_t j = 0; j < pattern.length(); ++j) {
						if ((section[i + j] & mask[j]) != (pattern[j] & mask[j]))
							goto next_char;
					}
					result.push_back(const_cast<char*>(section.data()) + i);
				next_char:;
				}
			}
		}
		return result;
	}
}
