#pragma once

#include <cinttypes>
#include <minwinbase.h>
#include <inaddr.h>
#include <string>

namespace Utils {
	SYSTEMTIME EpochToLocalSystemTime(uint64_t epochMilliseconds);
	uint64_t GetHighPerformanceCounter(int32_t multiplier = 1000);

	int CompareSockaddr(const void* x, const void* y);

	in_addr ParseIp(const std::string& s);
	uint16_t ParsePort(const std::string& s);

	std::vector<std::pair<uint32_t, uint32_t>> ParseIpRange(const std::string& s, bool allowAll, bool allowPrivate, bool allowLoopback);
	std::vector<std::pair<uint32_t, uint32_t>> ParsePortRange(const std::string& s, bool allowAll);

	template<typename T>
	T Clamp(T v, T min, T max) {
		return std::min(max, std::min(max, v));
	}
}
