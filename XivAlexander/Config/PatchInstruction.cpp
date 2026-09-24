#include "pch.h"
#include "PatchInstruction.h"

#include "Utils/Crypt.h"

bool XivAlexander::PatchInstruction::CreateNewHmacKeyIfInvalid() {
	try {
		const auto decoded = Utils::Crypt::Base64Decode(HmacKey);
		if (decoded.size() == HmacKeySize)
			return false;
	} catch (...) {
		// pass
	}

	uint8_t hmacKey[HmacKeySize];
	Utils::Crypt::GenerateRandom(hmacKey);
	HmacKey = Utils::Crypt::Base64Encode(hmacKey);
	return true;
}

std::string XivAlexander::PatchInstruction::Digest() const {
	uint8_t hmacResult[HmacKeySize + Utils::Crypt::HmacSha512::DigestSize];

	const auto keyBytes = Utils::Crypt::Base64Decode(HmacKey);
	if (keyBytes.size() != HmacKeySize)
		return {};
	std::memcpy(hmacResult, keyBytes.data(), HmacKeySize);

	Utils::Crypt::HmacSha512 hmac(std::span(hmacResult).subspan(0, HmacKeySize));

	auto nbuf = Name.size();
	hmac.Update(&nbuf, sizeof nbuf);
	hmac.Update(std::span(Name));

	for (const auto& bitness : {X64, X86}) {
		nbuf = bitness.size();
		hmac.Update(&nbuf, sizeof nbuf);
		for (const auto& b1 : bitness) {
			nbuf = b1.size();
			hmac.Update(&nbuf, sizeof nbuf);
			for (const auto& b2 : b1) {
				nbuf = b2.size();
				hmac.Update(&nbuf, sizeof nbuf);
				hmac.Update(std::span(b2));
			}
		}
	}

	hmac.Final(std::span(hmacResult).subspan(HmacKeySize));
	return Utils::Crypt::Base64Encode(hmacResult);
}

void XivAlexander::to_json(nlohmann::json& j, const PatchInstruction& value) {
	j = nlohmann::json::object({
		{"Name", value.Name},
		{"HmacKey", value.HmacKey},
		{"x64", value.X64},
		{"x86", value.X86},
	});
}

void XivAlexander::from_json(const nlohmann::json& it, PatchInstruction& value) {
	value = {
		.Name = it.value("Name", "(unnamed)"),
		.HmacKey = it.value("HmacKey", ""),
		.X64 = it.value<std::vector<std::vector<std::string>>>("x64", {}),
		.X86 = it.value<std::vector<std::vector<std::string>>>("x86", {}),
	};
	value.CreateNewHmacKeyIfInvalid();
}
