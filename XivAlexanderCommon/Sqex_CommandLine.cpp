#include "pch.h"
#include "Sqex_CommandLine.h"

#include "Utils_CallOnDestruction.h"
#include "Utils_Win32.h"

const char Sqex::CommandLine::ChecksumTable[17] = "fX1pGtdS5CAP4_VL";
const char Sqex::CommandLine::ObfuscationHead[13] = "//**sqex0003";
const char Sqex::CommandLine::ObfuscationTail[5] = "**//";

std::vector<std::pair<std::string, std::string>> Sqex::CommandLine::FromString(std::string source, bool* wasObfuscated) {
	std::vector<std::pair<std::string, std::string>> res;

	if (source.starts_with(ObfuscationHead) && source.size() >= 17) {
		auto endPos = source.find_first_of(ObfuscationTail);
		if (endPos == std::string::npos)
			throw std::invalid_argument("bad encoded string");
		source = source.substr(sizeof ObfuscationHead - 1, endPos - (sizeof ObfuscationHead - 1));

		const auto chksum = std::find(ChecksumTable, ChecksumTable + sizeof ChecksumTable, source.back()) - ChecksumTable;

		{
			CryptoPP::Base64URLDecoder b64decoder;
			b64decoder.Put(reinterpret_cast<const uint8_t*>(&source), source.size());
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

				for (const auto& item : Split(decrypted, '/', SIZE_MAX)) {
					const auto keyValue = Split(item, '=', 1);
					if (keyValue.size() == 1)
						res.emplace_back(Utils::StringReplaceAll<std::string>(keyValue[0], "  ", " "), "");
					else
						res.emplace_back(Utils::StringReplaceAll<std::string>(keyValue[0], "  ", " "),
							Utils::StringReplaceAll<std::string>(keyValue[1], "  ", " "));
				}
				if (wasObfuscated)
					*wasObfuscated = true;
				return res;
			}
		}
		throw std::invalid_argument("bad encoded string");

	} else {
		if (int nArgs; LPWSTR* szArgList = CommandLineToArgvW(std::format(L"test.exe {}", source).c_str(), &nArgs)) {
			const auto cleanup = Utils::CallOnDestruction([szArgList]() { LocalFree(szArgList); });
			for (int i = 1; i < nArgs; i++) {
				const auto arg = Utils::ToUtf8(szArgList[i]);
				const auto eq = arg.find_first_of('=');
				if (eq == std::string::npos)
					res.emplace_back(arg, "");
				else
					res.emplace_back(arg.substr(0, eq), arg.substr(eq + 1));
			}
		}
		if (wasObfuscated)
			*wasObfuscated = false;
		return res;
	}
}

std::string Sqex::CommandLine::ToString(const std::vector<std::pair<std::string, std::string>>& map, bool obfuscate) {
	if (obfuscate) {
		const auto tick = static_cast<DWORD>(GetTickCount64() & 0xFFFFFFFF);  // Practically GetTickCount, but silencing the warning
		const auto key = std::format("{:08x}", tick & 0xFFFF0000);
		const auto chksum = ChecksumTable[(tick >> 16) & 0xF];

		std::string encrypted;
		{
			std::ostringstream plain;
			plain << " T =" << tick;
			for (const auto& [k, v] : map) {
				if (k == "T")
					continue;

				plain << " /" << Utils::StringReplaceAll<std::string>(k, " ", "  ")
					<< " =" << Utils::StringReplaceAll<std::string>(v, " ", "  ");
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

		return std::format("{}{}{}{}", ObfuscationHead, encrypted, chksum, ObfuscationTail);

	} else {
		std::vector<std::string> args;
		for (const auto& pair : map) {
			if (pair.first == "T")
				continue;
			args.emplace_back(std::format("{}={}", pair.first, pair.second));
		}
		return Utils::Win32::ReverseCommandLineToArgv(args);
	}
}

void Sqex::CommandLine::ReverseEvery4Bytes(std::string& s) {
	if (s.size() % 4)
		throw std::invalid_argument("string length % 4 != 0");
	for (auto& i : std::span(reinterpret_cast<uint32_t*>(&s[0]), s.size() / 4))
		i = _byteswap_ulong(i);
}

std::vector<std::string> Sqex::CommandLine::Split(const std::string& source, char delimiter, size_t maxCount) {
	std::vector<std::string> split;
	split.resize(1);
	auto begin = false;
	for (const auto c : source) {
		if (c != ' ')
			begin = true;
		else if (!begin)
			continue;

		if (c == delimiter)
			split.emplace_back(&c, 1);
		else
			split.back() += c;
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
