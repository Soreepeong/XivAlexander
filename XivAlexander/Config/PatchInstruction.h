#pragma once

#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace XivAlexander {
	struct PatchInstruction {
		static constexpr auto HmacKeySize = 32;

		std::string Name;
		std::string HmacKey;
		std::vector<std::vector<std::string>> X64;
		std::vector<std::vector<std::string>> X86;

		bool CreateNewHmacKeyIfInvalid();

		[[nodiscard]]
		std::string Digest() const;

		auto operator<=>(const PatchInstruction&) const = default;
	};

	void to_json(nlohmann::json&, const PatchInstruction&);
	void from_json(const nlohmann::json&, PatchInstruction&);
}
