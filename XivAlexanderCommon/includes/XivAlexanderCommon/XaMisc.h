#pragma once

#include <cinttypes>
#include <inaddr.h>
#include <minwinbase.h>
#include <string>
#include <nlohmann/json.hpp>

namespace Utils {

	template<typename T, T DefaultValue = static_cast<T>(0)>
	struct LE {
	private:
		T value;

	public:
		LE(T defaultValue = DefaultValue)
			: value(defaultValue) {
		}

		operator T() const {
			return Value();
		}

		LE<T, DefaultValue>& operator= (T newValue) {
			Value(std::move(newValue));
			return *this;
		}

		LE<T, DefaultValue>& operator+= (T newValue) {
			Value(Value() + std::move(newValue));
			return *this;
		}

		LE<T, DefaultValue>& operator-= (T newValue) {
			Value(Value() - std::move(newValue));
			return *this;
		}

		T Value() const {
			return value;
		}

		void Value(T newValue) {
			value = std::move(newValue);
		}
	};

	template<typename T, T DefaultValue = static_cast<T>(0)>
	struct BE {
	private:
		union {
			T value;
			char buf[sizeof T];
		};

	public:
		BE(T defaultValue = DefaultValue)
			: value(defaultValue) {
			std::reverse(buf, buf + sizeof T);
		}

		operator T() const {
			return Value();
		}

		BE<T, DefaultValue>& operator= (T newValue) {
			Value(std::move(newValue));
			return *this;
		}

		BE<T, DefaultValue>& operator+= (T newValue) {
			Value(Value() + std::move(newValue));
			return *this;
		}

		BE<T, DefaultValue>& operator-= (T newValue) {
			Value(Value() - std::move(newValue));
			return *this;
		}

		T Value() const {
			union {
				char tmp[sizeof T];
				T tval;
			};
			memcpy(tmp, buf, sizeof T);
			std::reverse(tmp, tmp + sizeof T);
			return tval;
		}

		void Value(T newValue) {
			union {
				char tmp[sizeof T];
				T tval;
			};
			tval = newValue;
			std::reverse(tmp, tmp + sizeof T);
			memcpy(buf, tmp, sizeof T);
		}
	};

	SYSTEMTIME EpochToLocalSystemTime(int64_t epochMilliseconds);
	int64_t QpcUs();

	int CompareSockaddr(const void* x, const void* y);

	in_addr ParseIp(const std::string& s);
	uint16_t ParsePort(const std::string& s);

	std::vector<std::pair<uint32_t, uint32_t>> ParseIpRange(const std::string& s, bool allowAll, bool allowPrivate, bool allowLoopback);
	std::vector<std::pair<uint32_t, uint32_t>> ParsePortRange(const std::string& s, bool allowAll);

	template<typename T>
	T Clamp(T v, T min, T max) {
		return std::min(max, std::max(min, v));
	}

	void BoundaryCheck(size_t value, size_t offset, size_t length, const char* description = nullptr);

	template<typename T, typename = std::enable_if_t<std::is_pod_v<T>>>
	void WriteToUnalignedPtr(T val, void* to) {
		for (size_t i = 0; i < sizeof T; ++i)
			static_cast<char*>(to)[i] = reinterpret_cast<const char*>(&val)[i];
	}

	template<typename T>
	void ClearStdContainer(T& c) {
		T().swap(c);
	}

	template<typename T, typename ... Args>
	void ClearStdContainer(T& c, Args ... args) {
		T().swap(c);
		ClearStdContainer(std::forward<Args>(args)...);
	}

	std::map<std::pair<char32_t, char32_t>, SSIZE_T> ParseKerningTable(std::span<const char> data, const std::map<uint16_t, char32_t>& GlyphIndexToCharCodeMap);

	nlohmann::json ParseJsonFromFile(const std::filesystem::path& path, size_t maxSize = 1024 * 1024 * 16);
	void SaveJsonToFile(const std::filesystem::path& path, const nlohmann::json& json);
	void SaveToFile(const std::filesystem::path& path, std::span<const char> s);
}

namespace std::filesystem {
	void to_json(nlohmann::json&, const path&);
	void from_json(const nlohmann::json&, path&);
}
