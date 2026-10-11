#pragma once

#include <cinttypes>
#include <minwinbase.h>
#include <string>
#include <nlohmann/json.hpp>

namespace Utils {
	SYSTEMTIME EpochToLocalSystemTime(int64_t epochMilliseconds);
	int64_t QpcUs();
	constexpr int64_t SecondToMicrosecondMultiplier = 1000000;

	int CompareSockaddr(const void* x, const void* y);

	std::vector<std::pair<uint32_t, uint32_t>> ParseIpRange(const std::string& s, bool allowAll, bool allowPrivate, bool allowLoopback);
	std::vector<std::pair<uint32_t, uint32_t>> ParsePortRange(const std::string& s, bool allowAll);

	void BoundaryCheck(size_t value, size_t offset, size_t length, const char* description = nullptr);

	nlohmann::json ParseJsonFromFile(const std::filesystem::path& path, size_t maxSize = 1024 * 1024 * 16);
	void SaveJsonToFile(const std::filesystem::path& path, const nlohmann::json& json);
	void SaveToFile(const std::filesystem::path& path, std::span<const char> s);
}

namespace std::filesystem {
	void to_json(nlohmann::json&, const path&);
	void from_json(const nlohmann::json&, path&);
}
