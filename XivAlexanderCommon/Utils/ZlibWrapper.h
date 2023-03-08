#pragma once

#include <stdexcept>
#include <vector>
#include <zlib.h>

namespace Utils {
	class ZlibError : public std::runtime_error {
	public:
		static std::string DescribeReturnCode(int code);
		explicit ZlibError(int returnCode);
	};

	class ZlibReusableInflater {
		const int m_windowBits;
		const size_t m_defaultBufferSize;
		z_stream m_zstream{};
		bool m_initialized = false;
		std::vector<uint8_t> m_buffer;

		void Initialize();

	public:
		explicit ZlibReusableInflater(int windowBits = 15, int defaultBufferSize = 16384);

		~ZlibReusableInflater();

		std::span<uint8_t> operator()(std::span<const uint8_t> source);

		std::span<uint8_t> operator()(std::span<const uint8_t> source, size_t maxSize);

		std::span<uint8_t> operator()(std::span<const uint8_t> source, std::span<uint8_t> target);
	};

	class ZlibReusableDeflater {
		const int m_level;
		const int m_method;
		const int m_windowBits;
		const int m_memLevel;
		const int m_strategy;
		const size_t m_defaultBufferSize;
		z_stream m_zstream{};
		bool m_initialized = false;
		std::vector<uint8_t> m_buffer;

		std::span<uint8_t> m_latestResult;

		void Initialize();

	public:
		explicit ZlibReusableDeflater(
			int level = Z_DEFAULT_COMPRESSION,
			int method = Z_DEFLATED,
			int windowBits = 15,
			int memLevel = 8,
			int strategy = Z_DEFAULT_STRATEGY,
			size_t defaultBufferSize = 16384);

		~ZlibReusableDeflater();

		std::span<uint8_t> Deflate(std::span<const uint8_t> source);

		std::span<uint8_t> operator()(std::span<const uint8_t> source) {
			return Deflate(source);
		}

		const std::span<uint8_t>& Result() const {
			return m_latestResult;
		}
	};
}
