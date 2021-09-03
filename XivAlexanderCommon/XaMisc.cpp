#include "pch.h"
#include "XaMisc.h"

#include "Utils_Win32.h"
#include "XaStrings.h"

SYSTEMTIME Utils::EpochToLocalSystemTime(int64_t epochMilliseconds) {
	union {
		FILETIME ft{};
		LARGE_INTEGER li;
	};
	FILETIME lft;
	SYSTEMTIME st;

	li.QuadPart = epochMilliseconds * 10 * 1000 + 116444736000000000LL;
	FileTimeToLocalFileTime(&ft, &lft);
	FileTimeToSystemTime(&lft, &st);
	return st;
}

int64_t Utils::GetHighPerformanceCounter(int32_t multiplier) {
	LARGE_INTEGER time, freq;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&time);
	return time.QuadPart * multiplier / freq.QuadPart;
}

template<typename T>
int sockaddr_cmp_helper(T x, T y) {
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

in_addr Utils::ParseIp(const std::string& s) {
	in_addr addr{};
	switch (const auto res = inet_pton(AF_INET, &s[0], &addr); res) {
		case 1:
			return addr;
		case 0:
			throw std::runtime_error(std::format("\"{}\" is an invalid IP address.", s));
		case -1:
			throw Win32::Error(WSAGetLastError(), "Failed to parse IP address \"{}\".", s);
		default:
			throw std::runtime_error(std::format("Invalid result from inet_pton: {}", res));
	}
}

uint16_t Utils::ParsePort(const std::string& s) {
	size_t i = 0;
	const auto parsed = std::stoul(s, &i);
	if (parsed > UINT16_MAX)
		throw std::out_of_range("Not in uint16 range");
	if (i != s.length())
		throw std::out_of_range("Incomplete conversion");
	return static_cast<uint16_t>(parsed);
}

std::vector<std::pair<uint32_t, uint32_t>> Utils::ParseIpRange(const std::string& s, bool allowAll, bool allowPrivate, bool allowLoopback) {
	std::vector<std::pair<uint32_t, uint32_t>> result;
	for (auto& range : StringSplit(s, ",")) {
		try {
			range = StringTrim(range);
			if (range.empty())
				continue;
			uint32_t startIp, endIp;
			if (size_t pos; (pos = range.find('/')) != std::string::npos) {
				const auto subnet = std::stoi(StringTrim(range.substr(pos + 1)));
				startIp = endIp = ntohl(ParseIp(range.substr(0, pos)).s_addr);
				if (subnet == 0) {
					startIp = 0;
					endIp = 0xFFFFFFFFUL;
				} else if (subnet < 32) {
					startIp = (startIp & ~((1 << (32 - subnet)) - 1));
					endIp = (((endIp >> (32 - subnet)) + 1) << (32 - subnet)) - 1;
				}
			} else {
				auto ips = StringSplit(range, "-");
				if (ips.size() > 2)
					throw std::format_error("Too many items in range specification.");
				startIp = ntohl(ParseIp(ips[0]).s_addr);
				endIp = ips.size() == 2 ? ntohl(ParseIp(ips[1]).s_addr) : startIp;
				if (startIp > endIp) {
					const auto t = startIp;
					startIp = endIp;
					endIp = t;
				}
			}
			result.emplace_back(startIp, endIp);
		} catch (std::exception& e) {
			throw std::format_error(std::format("Invalid IP range item \"{}\": {}. It must be in the form of \"0.0.0.0\", \"0.0.0.0-255.255.255.255\", or \"127.0.0.0/8\", delimited by comma(,).", range, e.what()));
		}
	}
	if (!result.empty()) {
		if (allowAll)
			result.clear();
		else {
			if (allowLoopback)
				result.emplace_back(0x7F000000U, 0x7FFFFFFFU);  // 127.0.0.0 ~ 127.255.255.255
			if (allowPrivate) {
				result.emplace_back(0x0A000000U, 0x0AFFFFFFU);  // 10.0.0.0 ~ 10.255.255.255
				result.emplace_back(0xA9FE0000U, 0xA9FEFFFFU);  // 169.254.0.0 ~ 169.254.255.255
				result.emplace_back(0xAC100000U, 0xAC1FFFFFU);  // 172.16.0.0 ~ 172.31.255.255
				result.emplace_back(0xC0A80000U, 0xC0A8FFFFU);  // 192.168.0.0 ~ 192.168.255.255
			}
		}
	}
	return result;
}

std::vector<std::pair<uint32_t, uint32_t>> Utils::ParsePortRange(const std::string& s, bool allowAll) {
	std::vector<std::pair<uint32_t, uint32_t>> result;
	for (auto range : StringSplit(s, ",")) {
		try {
			range = StringTrim(range);
			if (range.empty())
				continue;
			auto ports = StringSplit(range, "-");
			if (ports.size() > 2)
				throw std::format_error("Too many items in range specification.");
			uint32_t start = ParsePort(ports[0]);
			uint32_t end = ports.size() == 2 ? ParsePort(ports[1]) : start;
			if (start > end) {
				const auto t = start;
				start = end;
				end = t;
			}
			result.emplace_back(start, end);
		} catch (std::exception& e) {
			throw std::format_error(std::format("Invalid port range item \"{}\": {}. It must be in the form of \"0-65535\" or single item, delimited by comma(,).", range, e.what()));
		}
	}
	if (allowAll)
		result.clear();
	return result;
}

void Utils::BoundaryCheck(size_t value, size_t offset, size_t length, const char* description) {
	if (value < offset || value > offset + length)
		throw std::out_of_range(description ? std::format("out of boundary ({})", description) : "out of boundary");
}
