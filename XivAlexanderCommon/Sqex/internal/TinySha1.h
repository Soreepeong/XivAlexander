/*
 *
 * TinySHA1 - a header only implementation of the SHA1 algorithm in C++. Based
 * on the implementation in boost::uuid::details.
 *
 * SHA1 Wikipedia Page: http://en.wikipedia.org/wiki/SHA-1
 *
 * Copyright (c) 2012-22 SAURAV MOHAPATRA <mohaps@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef _XIVRES_INTERNAL_TINYSHA1_H_
#define _XIVRES_INTERNAL_TINYSHA1_H_

#include <cstdint>
#include <memory>
#include <span>

#include "Misc.h"

namespace XivRes::Internal {
	class SHA1 {
	public:
		typedef uint32_t digest32_t[5];
		typedef uint8_t digest8_t[20];

	private:
		digest32_t m_digest;
		uint8_t m_block[64];
		size_t m_blockByteIndex;
		size_t m_byteCount;

	public:
		SHA1() {
			Reset();
		}

		SHA1(const SHA1& s) {
			*this = s;
		}

		const SHA1& operator=(const SHA1& s) {
			memcpy(m_digest, s.m_digest, 5 * sizeof(uint32_t));
			memcpy(m_block, s.m_block, 64);
			m_blockByteIndex = s.m_blockByteIndex;
			m_byteCount = s.m_byteCount;
			return *this;
		}

		SHA1& Reset() {
			m_digest[0] = 0x67452301;
			m_digest[1] = 0xEFCDAB89;
			m_digest[2] = 0x98BADCFE;
			m_digest[3] = 0x10325476;
			m_digest[4] = 0xC3D2E1F0;
			m_blockByteIndex = 0;
			m_byteCount = 0;
			return *this;
		}

		SHA1& ProcessByte(uint8_t octet) {
			this->m_block[this->m_blockByteIndex++] = octet;
			++this->m_byteCount;
			if (m_blockByteIndex == 64) {
				this->m_blockByteIndex = 0;
				ProcessBlock();
			}
			return *this;
		}

		SHA1& ProcessBlock(const void* const start, const void* const end) {
			const uint8_t* begin = static_cast<const uint8_t*>(start);
			const uint8_t* finish = static_cast<const uint8_t*>(end);
			while (begin != finish) {
				ProcessByte(*begin);
				begin++;
			}
			return *this;
		}

		SHA1& ProcessBytes(const void* const data, size_t len) {
			const uint8_t* block = static_cast<const uint8_t*>(data);
			ProcessBlock(block, block + len);
			return *this;
		}

		const uint32_t* GetDigest(digest32_t digest) {
			size_t bitCount = this->m_byteCount * 8;
			ProcessByte(0x80);
			if (this->m_blockByteIndex > 56) {
				while (m_blockByteIndex != 0) {
					ProcessByte(0);
				}
				while (m_blockByteIndex < 56) {
					ProcessByte(0);
				}
			} else {
				while (m_blockByteIndex < 56) {
					ProcessByte(0);
				}
			}
			ProcessByte(0);
			ProcessByte(0);
			ProcessByte(0);
			ProcessByte(0);
			ProcessByte(static_cast<unsigned char>((bitCount >> 24) & 0xFF));
			ProcessByte(static_cast<unsigned char>((bitCount >> 16) & 0xFF));
			ProcessByte(static_cast<unsigned char>((bitCount >> 8) & 0xFF));
			ProcessByte(static_cast<unsigned char>((bitCount) & 0xFF));

			memcpy(digest, m_digest, 5 * sizeof(uint32_t));
			return digest;
		}

		const uint8_t* GetDigestBytes(digest8_t digest) {
			digest32_t d32;
			GetDigest(d32);
			size_t di = 0;
			digest[di++] = ((d32[0] >> 24) & 0xFF);
			digest[di++] = ((d32[0] >> 16) & 0xFF);
			digest[di++] = ((d32[0] >> 8) & 0xFF);
			digest[di++] = ((d32[0]) & 0xFF);

			digest[di++] = ((d32[1] >> 24) & 0xFF);
			digest[di++] = ((d32[1] >> 16) & 0xFF);
			digest[di++] = ((d32[1] >> 8) & 0xFF);
			digest[di++] = ((d32[1]) & 0xFF);

			digest[di++] = ((d32[2] >> 24) & 0xFF);
			digest[di++] = ((d32[2] >> 16) & 0xFF);
			digest[di++] = ((d32[2] >> 8) & 0xFF);
			digest[di++] = ((d32[2]) & 0xFF);

			digest[di++] = ((d32[3] >> 24) & 0xFF);
			digest[di++] = ((d32[3] >> 16) & 0xFF);
			digest[di++] = ((d32[3] >> 8) & 0xFF);
			digest[di++] = ((d32[3]) & 0xFF);

			digest[di++] = ((d32[4] >> 24) & 0xFF);
			digest[di++] = ((d32[4] >> 16) & 0xFF);
			digest[di++] = ((d32[4] >> 8) & 0xFF);
			digest[di++] = ((d32[4]) & 0xFF);
			return digest;
		}

	private:
		void ProcessBlock() {
			uint32_t w[80];
			for (size_t i = 0; i < 16; i++) {
				w[i] = (m_block[i * 4 + 0] << 24);
				w[i] |= (m_block[i * 4 + 1] << 16);
				w[i] |= (m_block[i * 4 + 2] << 8);
				w[i] |= (m_block[i * 4 + 3]);
			}
			for (size_t i = 16; i < 80; i++) {
				w[i] = LeftRotate((w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]), 1);
			}

			uint32_t a = m_digest[0];
			uint32_t b = m_digest[1];
			uint32_t c = m_digest[2];
			uint32_t d = m_digest[3];
			uint32_t e = m_digest[4];

			for (std::size_t i = 0; i < 80; ++i) {
				uint32_t f = 0;
				uint32_t k = 0;

				if (i < 20) {
					f = (b & c) | (~b & d);
					k = 0x5A827999;
				} else if (i < 40) {
					f = b ^ c ^ d;
					k = 0x6ED9EBA1;
				} else if (i < 60) {
					f = (b & c) | (b & d) | (c & d);
					k = 0x8F1BBCDC;
				} else {
					f = b ^ c ^ d;
					k = 0xCA62C1D6;
				}
				uint32_t temp = LeftRotate(a, 5) + f + e + k + w[i];
				e = d;
				d = c;
				c = LeftRotate(b, 30);
				b = a;
				a = temp;
			}

			m_digest[0] += a;
			m_digest[1] += b;
			m_digest[2] += c;
			m_digest[3] += d;
			m_digest[4] += e;
		}

		static uint32_t LeftRotate(uint32_t value, size_t count) {
			return (value << count) ^ (value >> (32 - count));
		}
	};
}

namespace XivRes {
	struct Sha1Value {
		uint8_t Value[20]{};

		bool operator==(const Sha1Value& r) const {
			return memcmp(r.Value, Value, sizeof Value) == 0;
		}

		bool operator!=(const Sha1Value& r) const {
			return memcmp(r.Value, Value, sizeof Value) != 0;
		}

		bool operator<(const Sha1Value& r) const {
			return memcmp(r.Value, Value, sizeof Value) < 0;
		}

		bool operator<=(const Sha1Value& r) const {
			return memcmp(r.Value, Value, sizeof Value) <= 0;
		}

		bool operator>(const Sha1Value& r) const {
			return memcmp(r.Value, Value, sizeof Value) > 0;
		}

		bool operator>=(const Sha1Value& r) const {
			return memcmp(r.Value, Value, sizeof Value) >= 0;
		}

		bool operator==(const char(&r)[20]) const {
			return memcmp(r, Value, sizeof Value) == 0;
		}

		bool operator!=(const char(&r)[20]) const {
			return memcmp(r, Value, sizeof Value) != 0;
		}

		[[nodiscard]] bool IsZero() const {
			return Internal::IsAllSameValue(Value);
		}

		void SetFromPointer(const void* data, size_t size) {
			Internal::SHA1 hasher;
			hasher.ProcessBytes(data, size);
			hasher.GetDigestBytes(Value);
		}

		template<typename T>
		void SetFrom(std::span<T> data) {
			SetFromPointer(data.data(), data.size_bytes());
		}

		template<typename ...Args>
		void SetFromSpan(Args...args) {
			SetFrom(std::span(std::forward<Args>(args)...));
		}

		void Verify(const void* data, size_t size, const char* errorMessage) const {
			Sha1Value t;
			t.SetFromPointer(data, size);
			if (*this != t) {
				if (!size && Internal::IsAllSameValue(Value))  // all zero values can be in place of SHA-1 value of empty value
					return;
				throw CorruptDataException(errorMessage);
			}
		}

		template<typename T>
		void Verify(std::span<T> data, const char* errorMessage) const {
			Verify(data.data(), data.size_bytes(), errorMessage);
		}
	};
}

#endif
