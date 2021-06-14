#include "pch.h"
#include "App_Signatures.h"

namespace App::Signatures {

	bool SectionFilterTextOnly(const IMAGE_SECTION_HEADER& pSectionHeader) {
		return 0 == strncmp(reinterpret_cast<const char*>(pSectionHeader.Name), ".text", 6);
	}

	void* LookupForData(SectionFilter lookupInSection, const char* sPattern, const char* szMask, const std::vector<size_t> &nextOffsets) {
		const std::string_view mask(szMask, strlen(szMask));
		const std::string_view pattern(sPattern, mask.length());

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
					return const_cast<char*>(section.data()) + i;
				next_char:;
				}
			}
		}
		return nullptr;
	}

	BaseSignature::BaseSignature(const char* szName)
	: m_sName(szName) {
		AllSignatures().push_back(this);
	}

	BaseSignature::~BaseSignature() = default;

	std::vector<BaseSignature*>& AllSignatures() {
		static std::vector<BaseSignature*> signatures;
		return signatures;
	}
}
