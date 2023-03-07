#pragma once

#include <vector>

namespace XivAlexander::Misc::Signatures {

	typedef bool (*SectionFilter)(const IMAGE_SECTION_HEADER&);
	bool SectionFilterTextOnly(const IMAGE_SECTION_HEADER& pSectionHeader);

	[[nodiscard]] std::vector<void*> LookupForData(SectionFilter lookupInSection, const char* sPattern, const char* sMask, size_t length, const std::vector<size_t>& nextOffsets);

	class ScanResult {
		srell::cmatch m_match;

	public:
		ScanResult() = default;
		ScanResult(const ScanResult&) = default;
		ScanResult(ScanResult&&) noexcept = default;
		ScanResult& operator=(const ScanResult&) = default;
		ScanResult& operator=(ScanResult&&) noexcept = default;

		ScanResult(srell::cmatch match)
			: m_match(std::move(match)) {
		}

		template<typename T>
		T& Get(size_t matchIndex) const {
			return *static_cast<T*>(const_cast<void*>(static_cast<const void*>(m_match[matchIndex].first)));
		}

		template<typename T>
		void GetInto(T& into, size_t matchIndex) const {
			into = Get<T>(matchIndex);
		}

		template<typename T>
		T ResolveAddress(size_t matchIndex) const {
			return reinterpret_cast<T>(m_match[matchIndex].first + 4 + Get<int32_t>(matchIndex));
		}

		template<typename T>
		void ResolveAddressInto(T& into, size_t matchIndex) const {
			into = ResolveAddress<T>(matchIndex);
		}

		template<typename T = void>
		T* begin(size_t matchIndex) {
			return static_cast<T*>(const_cast<void*>(static_cast<const void*>(&*m_match[matchIndex].first)));
		}

		template<typename T = void>
		void* end(size_t matchIndex) {
			return static_cast<T*>(const_cast<void*>(static_cast<const void*>(&*m_match[matchIndex].second)));
		}
	};
	
	class RegexSignature {
		const srell::regex m_pattern;

	public:
		template<size_t Length>
		RegexSignature(const char(&data)[Length])
			: m_pattern{ data, data + Length - 1, srell::regex_constants::dotall } {
		}

		bool Lookup(const void* data, size_t length, ScanResult& result, bool next = false) const;

		template<typename T>
		bool Lookup(std::span<T> data, ScanResult& result, bool next = false) const {
			return Lookup(data.data(), data.size_bytes(), result, next);
		}
	};

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
