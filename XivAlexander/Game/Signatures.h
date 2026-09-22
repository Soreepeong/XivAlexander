#pragma once

#include <iterator>
#include <span>
#include <string>
#include <string_view>

namespace XivAlexander::Game::Signatures {
	class ScanResult {
		srell::cmatch m_match;

	public:
		ScanResult() = default;
		ScanResult(const ScanResult&) = default;
		ScanResult(ScanResult&&) noexcept = default;
		ScanResult& operator=(const ScanResult&) = default;
		ScanResult& operator=(ScanResult&&) noexcept = default;

		ScanResult(srell::cmatch match)
			: m_match(std::move(match)) {}

		bool ready() const {
			return m_match.ready();
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
			return reinterpret_cast<T>(const_cast<char*>(m_match[matchIndex].first + 4 + Get<int32_t>(matchIndex)));
		}

		template<typename T>
		void ResolveAddressInto(T& into, size_t matchIndex) const {
			into = ResolveAddress<T>(matchIndex);
		}

		template<typename T = void>
		T* begin(size_t matchIndex) const {
			return static_cast<T*>(const_cast<void*>(static_cast<const void*>(&*m_match[matchIndex].first)));
		}

		template<typename T = void>
		void* end(size_t matchIndex) const {
			return static_cast<T*>(const_cast<void*>(static_cast<const void*>(&*m_match[matchIndex].second)));
		}
	};

	enum class MatchCount {
		None,
		One,
		Many,
	};

	[[nodiscard]] constexpr std::string_view ToString(MatchCount count) {
		switch (count) {
			case MatchCount::None:
				return "not found";
			case MatchCount::One:
				return "found once";
			case MatchCount::Many:
				return "found more than once";
		}
		return "invalid";
	}

	class RegexSignature {
		const srell::regex m_pattern;

	public:
		template<size_t Length>
		RegexSignature(const char (&data)[Length])
			: m_pattern{data, data + Length - 1, srell::regex_constants::dotall} {}

		enum LookupFrom {
			FromNextByte,
			FromMatchEnd,
		};

		class LookupResult {
		public:
			class Iterator {
				const srell::regex* m_pattern{};
				const char* m_begin{};
				const char* m_end{};
				LookupFrom m_lookupFrom{};
				ScanResult m_current;

			public:
				using iterator_category = std::forward_iterator_tag;
				using value_type = ScanResult;
				using difference_type = std::ptrdiff_t;
				using pointer = const ScanResult*;
				using reference = const ScanResult&;

				Iterator() = default;
				Iterator(const srell::regex& pattern, const char* begin, const char* end, const char* from, LookupFrom lookupFrom);

				reference operator*() const { return m_current; }
				pointer operator->() const { return &m_current; }

				Iterator& operator++();
				Iterator operator++(int);

				bool operator==(const Iterator& r) const;
				bool operator==(std::default_sentinel_t) const { return !m_pattern; }

			private:
				void SearchFrom(const char* from);
			};

			explicit LookupResult(Iterator first)
				: m_first(std::move(first)) {}

			[[nodiscard]] Iterator begin() const { return m_first; }
			[[nodiscard]] std::default_sentinel_t end() const { return {}; }

			[[nodiscard]] MatchCount Count() const;

		private:
			Iterator m_first;
		};

		[[nodiscard]] LookupResult Lookup(const void* data, size_t length, LookupFrom lookupFrom = FromMatchEnd) const;

		template<typename T>
		[[nodiscard]] LookupResult Lookup(std::span<T> data, LookupFrom lookupFrom = FromMatchEnd) const {
			return Lookup(data.data(), data.size_bytes(), lookupFrom);
		}

		bool Lookup(const void* data, size_t length, ScanResult& result, LookupFrom lookupFrom = FromNextByte) const;

		template<typename T>
		bool Lookup(std::span<T> data, ScanResult& result, LookupFrom lookupFrom = FromNextByte) const {
			return Lookup(data.data(), data.size_bytes(), result, lookupFrom);
		}

		bool MatchAt(const void* data, size_t length, ScanResult& result) const;

		template<typename T>
		bool MatchAt(std::span<T> data, ScanResult& result) const {
			return MatchAt(data.data(), data.size_bytes(), result);
		}

		[[nodiscard]] std::pair<ScanResult, MatchCount> LookupUnique(const void* data, size_t length, LookupFrom lookupFrom = FromMatchEnd) const;

		template<typename T>
		[[nodiscard]] std::pair<ScanResult, MatchCount> LookupUnique(std::span<T> data, LookupFrom lookupFrom = FromMatchEnd) const {
			return LookupUnique(data.data(), data.size_bytes(), lookupFrom);
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
			, m_pAddress(std::move(pAddress)) {}

		virtual ~Signature() = default;

		virtual operator T() const {
			return m_pAddress;
		}

		operator bool() const { return !!m_pAddress; }
	};
}
