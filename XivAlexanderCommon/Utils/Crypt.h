#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Utils::Crypt {
	class Sha1 {
	public:
		static constexpr size_t DigestSize = 20;

		Sha1();
		~Sha1();
		Sha1(const Sha1&) = delete;
		Sha1& operator=(const Sha1&) = delete;

		void Update(std::span<const uint8_t> data);

		void Update(const void* data, size_t size) {
			Update(std::span(static_cast<const uint8_t*>(data), size));
		}

		template<typename T>
		void Update(std::span<const T> data) requires std::is_trivially_copyable_v<T> {
			Update(data.data(), data.size_bytes());
		}

		void Final(std::span<uint8_t> digest);

	private:
		void* m_hAlg;
		void* m_hHash;
		std::vector<uint8_t> m_hashObject;
	};

	class HmacSha512 {
	public:
		static constexpr size_t DigestSize = 64;

		HmacSha512(std::span<const uint8_t> key);
		~HmacSha512();
		HmacSha512(const HmacSha512&) = delete;
		HmacSha512& operator=(const HmacSha512&) = delete;

		void Update(std::span<const uint8_t> data);

		void Update(const void* data, size_t size) {
			Update(std::span(static_cast<const uint8_t*>(data), size));
		}

		template<typename T>
		void Update(std::span<const T> data) requires std::is_trivially_copyable_v<T> {
			Update(data.data(), data.size_bytes());
		}

		void Final(std::span<uint8_t> digest);

	private:
		void* m_hAlg;
		void* m_hHash;
		std::vector<uint8_t> m_hashObject;
	};

	std::string Base64Encode(std::span<const uint8_t> data);
	std::vector<uint8_t> Base64Decode(std::string_view str);
	
	template<typename T>
	std::string Base64Encode(std::span<const T> data) requires std::is_trivially_copyable_v<T> {
		return Base64Encode(std::span(reinterpret_cast<const uint8_t*>(data.data()), data.size_bytes()));
	}
	
	template<typename T>
	std::vector<uint8_t> Base64Decode(std::span<const T> data) requires std::is_trivially_copyable_v<T> {
		return Base64Decode(std::span(reinterpret_cast<const uint8_t*>(data.data()), data.size_bytes()));
	}

	std::string Base64UrlEncode(std::span<const uint8_t> data);
	std::vector<uint8_t> Base64UrlDecode(std::string_view str);
	
	template<typename T>
	std::string Base64UrlEncode(std::span<const T> data) requires std::is_trivially_copyable_v<T> {
		return Base64UrlEncode(std::span(reinterpret_cast<const uint8_t*>(data.data()), data.size_bytes()));
	}
	
	template<typename T>
	std::vector<uint8_t> Base64UrlDecode(std::span<const T> data) requires std::is_trivially_copyable_v<T> {
		return Base64UrlDecode(std::span(reinterpret_cast<const uint8_t*>(data.data()), data.size_bytes()));
	}

	void GenerateRandom(std::span<uint8_t> buf);

	void BlowfishEcbEncrypt(std::span<const uint8_t> key, std::span<uint8_t> data);
	void BlowfishEcbDecrypt(std::span<const uint8_t> key, std::span<uint8_t> data);
	
	template<typename T1, typename T2>
	void BlowfishEcbEncrypt(std::span<const T1> key, std::span<T2> data) {
		BlowfishEcbEncrypt(span_cast<const uint8_t>(key), span_cast<uint8_t>(data));
	}
	
	template<typename T1, typename T2>
	void BlowfishEcbDecrypt(std::span<const T1> key, std::span<T2> data) {
		BlowfishEcbDecrypt(span_cast<const uint8_t>(key), span_cast<uint8_t>(data));
	}
}
