#pragma once
#include <vector>

namespace App::Misc::Signatures {

	typedef bool (*SectionFilter)(const IMAGE_SECTION_HEADER&);
	bool SectionFilterTextOnly(const IMAGE_SECTION_HEADER& pSectionHeader);

	[[nodiscard]]
	std::vector<void*> LookupForData(SectionFilter lookupInSection, const char* sPattern, const char* sMask, size_t length, const std::vector<size_t>& nextOffsets);

	template<typename T>
	class Signature {
	protected:
		const std::string m_sName;
		const T m_pAddress;

	public:
		Signature(const char* szName, T pAddress)
			: m_sName(szName)
			, m_pAddress(std::move(pAddress)) {
		}
		virtual ~Signature() = default;

		virtual operator T () const {
			return m_pAddress;
		}

		operator bool() const { return !!m_pAddress; }
	};

	template<typename T>
	class DataSignature : public Signature<T> {
	public:
		DataSignature(const char* szName, SectionFilter sectionFilter, const char* sPattern, const char* sMask, size_t length, std::vector<size_t> nextOffsets = {})
			: Signature(szName, LookupForData(sectionFilter, sPattern, sMask, length, nextOffsets)) {
		}
	};
}
