#include "pch.h"
#include "Sqex_CommandLine.h"

#include "Utils_CallOnDestruction.h"
#include "Utils_Win32.h"

const char Sqex::CommandLine::ChecksumTable[17] = "fX1pGtdS5CAP4_VL";
const char Sqex::CommandLine::ObfuscationHead[13] = "//**sqex0003";
const char Sqex::CommandLine::ObfuscationTail[5] = "**//";

std::vector<std::pair<std::string, std::string>> Sqex::CommandLine::FromString(const std::wstring& source, bool* wasObfuscated) {
	const auto args = Utils::Win32::CommandLineToArgsU8(source);
	std::vector<std::pair<std::string, std::string>> res;

	if (args.size() == 2 && args[1].starts_with(ObfuscationHead) && args[1].size() >= 17) {
		auto endPos = args[1].find(ObfuscationTail);
		if (endPos == std::string::npos)
			throw std::invalid_argument("bad encoded string");
		auto source = args[1].substr(sizeof ObfuscationHead - 1, endPos - (sizeof ObfuscationHead - 1));

		const auto chksum = std::find(ChecksumTable, ChecksumTable + sizeof ChecksumTable, source.back()) - ChecksumTable;
		source.pop_back();

		{
			CryptoPP::Base64URLDecoder b64decoder;
			b64decoder.Put(reinterpret_cast<const uint8_t*>(&source[0]), source.size());
			b64decoder.MessageEnd();
			source.resize(static_cast<size_t>(b64decoder.MaxRetrievable()));
			b64decoder.Get(reinterpret_cast<uint8_t*>(&source[0]), source.size());
			ReverseEvery4Bytes(source);
		}

		FILETIME ct, xt, kt, ut, nft;
		if (!GetProcessTimes(GetCurrentProcess(), &ct, &xt, &kt, &ut))
			throw Utils::Win32::Error("GetProcessTimes(GetCurrentProcess(), ...)");
		GetSystemTimeAsFileTime(&nft);
		const auto creationTickCount = GetTickCount64() - (ULARGE_INTEGER{{nft.dwLowDateTime, nft.dwHighDateTime}}.QuadPart - ULARGE_INTEGER{{ct.dwLowDateTime, ct.dwHighDateTime}}.QuadPart) / 10000;

		CryptoPP::ECB_Mode<CryptoPP::Blowfish>::Decryption dec;
		for (auto [val, count] : {
				std::make_pair(chksum << 16 | (creationTickCount & 0xFF000000), 16),
				std::make_pair(chksum << 16 | 0ULL, 0xFFF),
			}) {
			for (; --count; val += 0x100000) {
				const auto key = std::format("{:08x}", val & 0xFFFF0000);

				std::string decrypted = source;
				dec.SetKey(reinterpret_cast<const uint8_t*>(&key[0]), key.size());
				dec.ProcessString(reinterpret_cast<uint8_t*>(&decrypted[0]), decrypted.size());
				if (!decrypted.starts_with("= T ") && !decrypted.starts_with(" T/ "))
					continue;

				ReverseEvery4Bytes(decrypted);
				source.clear();

				for (const auto& item : SplitPreserveDelimiter(decrypted, '/', SIZE_MAX)) {
					const auto keyValue = SplitPreserveDelimiter(item, '=', 1);
					if (keyValue.size() == 1)
						res.emplace_back(Utils::StringReplaceAll<std::string>(Utils::ToUtf8(Utils::FromUtf8(keyValue[0], CP_OEMCP)), "  ", " "), "");
					else
						res.emplace_back(Utils::StringReplaceAll<std::string>(Utils::ToUtf8(Utils::FromUtf8(keyValue[0], CP_OEMCP)), "  ", " "),
							Utils::StringReplaceAll<std::string>(Utils::ToUtf8(Utils::FromUtf8(keyValue[1], CP_OEMCP)), "  ", " "));
				}
				if (wasObfuscated)
					*wasObfuscated = true;
				return res;
			}
		}
		throw std::invalid_argument("bad encoded string");

	} else {
		for (size_t i = 1; i < args.size(); ++i) {
			const auto eq = args[i].find('=');
			if (eq == std::string::npos)
				res.emplace_back(args[i], "");
			else
				res.emplace_back(args[i].substr(0, eq), args[i].substr(eq + 1));
		}
		if (wasObfuscated)
			*wasObfuscated = false;
		return res;
	}
}

std::wstring Sqex::CommandLine::ToString(const std::vector<std::pair<std::string, std::string>>& args, bool obfuscate) {
	if (obfuscate) {
		const auto tick = static_cast<DWORD>(GetTickCount64() & 0xFFFFFFFF);  // Practically GetTickCount, but silencing the warning
		const auto key = std::format("{:08x}", tick & 0xFFFF0000);
		const auto chksum = ChecksumTable[(tick >> 16) & 0xF];

		std::string encrypted;
		{
			std::ostringstream plain;
			plain << " T =" << tick;
			for (const auto& [k, v] : args) {
				if (k == "T")
					continue;

				plain << " /" << Utils::StringReplaceAll<std::string>(Utils::ToUtf8(Utils::FromUtf8(k), CP_OEMCP), " ", "  ")
					<< " =" << Utils::StringReplaceAll<std::string>(Utils::ToUtf8(Utils::FromUtf8(v), CP_OEMCP), " ", "  ");
			}
			encrypted = plain.str();
		}
		encrypted.resize((encrypted.size() + 7) / 8 * 8, '\0');
		{
			CryptoPP::ECB_Mode<CryptoPP::Blowfish>::Encryption enc;
			enc.SetKey(reinterpret_cast<const uint8_t*>(&key[0]), key.size());
			ReverseEvery4Bytes(encrypted);
			enc.ProcessString(reinterpret_cast<uint8_t*>(&encrypted[0]), encrypted.size());
			ReverseEvery4Bytes(encrypted);
		}
		{
			CryptoPP::Base64URLEncoder b64encoder;
			b64encoder.Put(reinterpret_cast<const uint8_t*>(&encrypted[0]), encrypted.size());
			b64encoder.MessageEnd();
			encrypted.resize(static_cast<size_t>(b64encoder.MaxRetrievable()));
			b64encoder.Get(reinterpret_cast<uint8_t*>(&encrypted[0]), encrypted.size());
		}

		return Utils::FromUtf8(std::format("{}{}{}{}", ObfuscationHead, encrypted, chksum, ObfuscationTail));

	} else {
		std::vector<std::wstring> res;
		for (const auto& pair : args) {
			if (pair.first == "T")
				continue;
			res.emplace_back(std::format(L"{}={}", pair.first, pair.second));
		}
		return Utils::Win32::ReverseCommandLineToArgv(res);
	}
}

void Sqex::CommandLine::ReverseEvery4Bytes(std::string& s) {
	if (s.size() % 4)
		throw std::invalid_argument("string length % 4 != 0");
	for (auto& i : std::span(reinterpret_cast<uint32_t*>(&s[0]), s.size() / 4))
		i = _byteswap_ulong(i);
}

std::vector<std::string> Sqex::CommandLine::SplitPreserveDelimiter(const std::string& source, char delimiter, size_t maxCount) {
	std::vector<std::string> split;
	split.resize(1);
	auto begin = false;
	for (const auto c : source) {
		if (c != ' ')
			begin = true;
		else if (!begin)
			continue;

		if (c == delimiter)
			split.emplace_back();
		split.back().push_back(c);
	}
	for (size_t i = 1; i < split.size();) {
		const auto nonspace = split[i - 1].find_last_not_of(' ');
		if (i > maxCount || nonspace == std::string::npos || (split[i - 1].size() - nonspace - 1) % 2 == 0) {
			split[i - 1] += split[i];
			split.erase(std::next(split.begin(), static_cast<decltype(split)::difference_type>(i)));
		} else {
			++i;
		}
	}
	for (auto& s : split) {
		const auto off = !s.empty() && s.front() == delimiter ? 1 : 0;
		const auto to = !s.empty() && s.back() == ' ' ? s.size() - 1 : s.size();
		s = s.substr(off, to - off);
	}
	return split;
}

void Sqex::CommandLine::ModifyParameter(std::vector<std::pair<std::string, std::string>>& args, const std::string& key, std::string value) {
	for (auto& pair : args) {
		if (pair.first == key) {
			pair.second = std::move(value);
			return;
		}
	}
	args.emplace_back(key, std::move(value));
}

void Sqex::CommandLine::WellKnown::SetRegion(std::vector<std::pair<std::string, std::string>>& args, Region region) {
	if (region != Region::Unspecified)
		return ModifyParameter(args, Keys::Region, std::format("{}", static_cast<int>(region)));
}

Sqex::Region Sqex::CommandLine::WellKnown::GetRegion(const std::vector<std::pair<std::string, std::string>>& args, Region fallback /*= Region::Unspecified*/) {
	for (const auto& pair : args) {
		if (pair.first == Keys::Region)
			return static_cast<Region>(std::strtol(pair.second.c_str(), nullptr, 0));
	}
	return fallback;
}

void Sqex::CommandLine::WellKnown::SetLanguage(std::vector<std::pair<std::string, std::string>>& args, Language language) {
	if (language != Language::Unspecified)
		return ModifyParameter(args, Keys::Language, std::format("{}", static_cast<int>(language) - 1));
}

Sqex::Language Sqex::CommandLine::WellKnown::GetLanguage(const std::vector<std::pair<std::string, std::string>>& args, Language fallback /*= Language::Unspecified*/) {
	for (const auto& pair : args) {
		if (pair.first == Keys::Language)
			return static_cast<Language>(1 + std::strtol(pair.second.c_str(), nullptr, 0));
	}
	return fallback;
}
