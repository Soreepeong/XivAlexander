#pragma once

#include <vector>
#include <string>

#include <xivres/common.h>

#include <xivres/common.h>

namespace XivAlexander::Game::CommandLine {
	extern const char ChecksumTable[17];
	extern const char ObfuscationHead[13];
	extern const char ObfuscationTail[5];

	namespace WellKnown {
		namespace Keys {
			static constexpr const char* Language = "language";
			static constexpr const char* Region = "SYS.Region";
		}

		void SetRegion(std::vector<std::pair<std::string, std::string>>& args, xivres::game_publisher region);
		xivres::game_publisher GetRegion(const std::vector<std::pair<std::string, std::string>>& args, xivres::game_publisher fallback = xivres::game_publisher::Unspecified);

		void SetLanguage(std::vector<std::pair<std::string, std::string>>& args, xivres::game_language language);
		xivres::game_language GetLanguage(const std::vector<std::pair<std::string, std::string>>& args, xivres::game_language fallback = xivres::game_language::Unspecified);
	};

	std::vector<std::pair<std::string, std::string>> FromString(const std::wstring& source, bool* wasObfuscated = nullptr);
	std::wstring ToString(const std::vector<std::pair<std::string, std::string>>& args, bool obfuscate);
	void ModifyParameter(std::vector<std::pair<std::string, std::string>>& args, const std::string& key, std::string value);

	void ReverseEvery4Bytes(std::string& s);
	std::vector<std::string> SplitPreserveDelimiter(const std::string& source, char delimiter, size_t maxCount);
}
