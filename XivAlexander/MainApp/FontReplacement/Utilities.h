#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement {
	inline void ThrowOnError(HRESULT hr, const char* what) {
		if (FAILED(hr))
			throw std::runtime_error(std::format("{} failed: 0x{:08X}", what, static_cast<uint32_t>(hr)));
	}

	// As presets and the game's files compare names.
	[[nodiscard]] inline bool EqualsIgnoringCase(std::string_view a, std::string_view b) {
		return a.size() == b.size() && std::ranges::equal(a, b, [](char x, char y) {
			return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
		});
	}

	[[nodiscard]] inline bool EndsWithIgnoringCase(std::string_view s, std::string_view suffix) {
		return s.size() >= suffix.size() && EqualsIgnoringCase(s.substr(s.size() - suffix.size()), suffix);
	}

	// As presets compare names.
	struct LessIgnoringCase {
		bool operator()(std::string_view a, std::string_view b) const {
			return std::ranges::lexicographical_compare(a, b, [](char x, char y) {
				return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
			});
		}
	};

	template<typename T>
	T& At(uintptr_t address) {
		return *reinterpret_cast<T*>(address);
	}

	template<typename T>
	T& At(const void* p, int offset) {
		return At<T>(reinterpret_cast<uintptr_t>(p) + offset);
	}
}
