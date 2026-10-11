#pragma once

#include <vector>
#include <string>

#include <xivres/common.h>

namespace XivAlexander::Game::CommandLine {
	using ArgumentList = std::vector<std::pair<std::string, std::string>>;

	extern const char ChecksumTable[17];
	extern const char ObfuscationHead[13];
	extern const char ObfuscationTail[5];

	namespace WellKnown {
		namespace Keys {
			static constexpr const char* Language = "language";
			static constexpr const char* Region = "SYS.Region";
		}

		void SetRegion(ArgumentList& args, xivres::game_publisher region);
		xivres::game_publisher GetRegion(const ArgumentList& args, xivres::game_publisher fallback = xivres::game_publisher::Unspecified);

		void SetLanguage(ArgumentList& args, xivres::game_language language);
		xivres::game_language GetLanguage(const ArgumentList& args, xivres::game_language fallback = xivres::game_language::Unspecified);
	};

	ArgumentList FromString(const std::wstring& source, bool* wasObfuscated = nullptr);
	std::wstring ToString(const ArgumentList& args, bool obfuscate);
	void ModifyParameter(ArgumentList& args, const std::string& key, std::string value);

	void ReverseEvery4Bytes(std::string& s);
	std::vector<std::string> SplitPreserveDelimiter(const std::string& source, char delimiter, size_t maxCount);
}
