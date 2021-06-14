#pragma once
namespace App::Signatures {

	typedef bool (*SectionFilter)(const IMAGE_SECTION_HEADER&);
	bool SectionFilterTextOnly(const IMAGE_SECTION_HEADER& pSectionHeader);

	void* LookupForData(SectionFilter lookupInSection, const char* sPattern, const char* szMask, const std::vector<size_t>& nextOffsets);

	class BaseSignature {
		static std::map<std::string, const BaseSignature*> c_signatures;

	protected:
		const std::string m_sName;

	public:
		explicit BaseSignature(const char* szName);
		virtual ~BaseSignature();

		virtual operator void* () const = 0;

		virtual void Startup() = 0;
		virtual void Cleanup() = 0;
		virtual void Setup() = 0;

		operator bool() const { return !!this->operator void* (); }
	};

	std::vector<BaseSignature*>& AllSignatures();

	template<typename T>
	class Signature : public BaseSignature {
	protected:
		const SectionFilter m_sectionFilter;
		const std::string m_sMask;
		const std::string m_sPattern;
		const std::vector<size_t> m_nextOffsets;

		std::function<void* ()> m_fnResolver;
		mutable void* m_pAddress = 0;

	public:
		Signature(const char* szName)
			: BaseSignature(szName)
			, m_sectionFilter(nullptr) {
		}

		Signature(const char* szName, void* pAddress)
			: BaseSignature(szName)
			, m_sectionFilter(nullptr)
			, m_pAddress(pAddress) {
		}

		Signature(const char* szName, std::function<void* ()> fnResolver)
			: BaseSignature(szName)
			, m_fnResolver(std::move(fnResolver))
			, m_sectionFilter(nullptr) {
		}

		Signature(const char* szName, SectionFilter sectionFilter, const char* sPattern, const char* szMask, std::vector<size_t> nextOffsets = {}) 
			: BaseSignature(szName)
			, m_sectionFilter(sectionFilter)
			, m_sMask(szMask, szMask + strlen(szMask))
			, m_sPattern(sPattern, sPattern + m_sMask.length())
			, m_nextOffsets(std::move(nextOffsets)) {
		}

		void Setup() override {
			if (m_pAddress)
				return;
			else if (m_fnResolver)
				m_pAddress = m_fnResolver();
			else
				m_pAddress = LookupForData(m_sectionFilter, m_sPattern.c_str(), m_sMask.c_str(), m_nextOffsets);
		}

		void Startup() override {}

		void Cleanup() override {}

		operator void* () const override {
			return m_pAddress;
		}
	};
}
