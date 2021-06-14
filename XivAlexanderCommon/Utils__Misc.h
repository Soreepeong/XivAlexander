#pragma once

#include <cinttypes>
#include <minwinbase.h>

namespace Utils {
	SYSTEMTIME EpochToLocalSystemTime(uint64_t epochMilliseconds);
	uint64_t GetHighPerformanceCounter(int32_t multiplier = 1000);

	int CompareSockaddr(const void* x, const void* y);
}
