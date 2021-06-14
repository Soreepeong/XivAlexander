#include "pch.h"
#include "Utils__Misc.h"

SYSTEMTIME Utils::EpochToLocalSystemTime(uint64_t epochMilliseconds) {
	union {
		FILETIME ft;
		LARGE_INTEGER li;
	};
	FILETIME lft;
	SYSTEMTIME st;

	li.QuadPart = epochMilliseconds * 10 * 1000ULL + 116444736000000000ULL;
	FileTimeToLocalFileTime(&ft, &lft);
	FileTimeToSystemTime(&lft, &st);
	return st;
}

uint64_t Utils::GetHighPerformanceCounter(int32_t multiplier) {
	LARGE_INTEGER time, freq;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&time);
	return time.QuadPart * multiplier / freq.QuadPart;
}

static int sockaddr_cmp_helper(int x, int y) {
	if (x < y)
		return -1;
	else if (x > y)
		return 1;
	return 0;
}
static int sockaddr_cmp_helper(int x) {
	return x;
}

int Utils::CompareSockaddr(const void* x, const void* y) {
	const auto family1 = static_cast<const sockaddr*>(x)->sa_family;
	const auto family2 = static_cast<const sockaddr*>(y)->sa_family;
	int n;
	if ((n = sockaddr_cmp_helper(family1, family2))) return n;
	if (family1 == AF_INET) {
		const auto addr1 = static_cast<const sockaddr_in*>(x);
		const auto addr2 = static_cast<const sockaddr_in*>(y);
		if ((n = sockaddr_cmp_helper(ntohl(addr1->sin_addr.s_addr), ntohl(addr2->sin_addr.s_addr)))) return n;
		if ((n = sockaddr_cmp_helper(ntohs(addr1->sin_port), ntohs(addr2->sin_port)))) return n;
	} else if (family1 == AF_INET6) {
		const auto addr1 = static_cast<const sockaddr_in6*>(x);
		const auto addr2 = static_cast<const sockaddr_in6*>(y);
		if ((n = sockaddr_cmp_helper(memcmp(addr1->sin6_addr.s6_addr, addr2->sin6_addr.s6_addr, sizeof addr1->sin6_addr.s6_addr)))) return n;
		if ((n = sockaddr_cmp_helper(ntohs(addr1->sin6_port), ntohs(addr2->sin6_port)))) return n;
		if ((n = sockaddr_cmp_helper(addr1->sin6_flowinfo, addr2->sin6_flowinfo))) return n;
		if ((n = sockaddr_cmp_helper(addr1->sin6_scope_id, addr2->sin6_scope_id))) return n;
	}
	return 0;
}
