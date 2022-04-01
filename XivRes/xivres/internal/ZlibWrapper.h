#ifndef _XIVRES_INTERNAL_ZLIBWRAPPER_H_
#define _XIVRES_INTERNAL_ZLIBWRAPPER_H_

#include <cstdint>
#include <format>
#include <stdexcept>
#include <span>
#include <system_error>
#include <vector>
#include <zlib.h>

namespace XivRes::Internal {
	class ZlibError : public std::runtime_error {
	public:
		static std::string DescribeReturnCode(int code) {
			switch (code) {
				case Z_OK: return "OK";
				case Z_STREAM_END: return "Stream end";
				case Z_NEED_DICT: return "Need dict";
				case Z_ERRNO: return std::generic_category().message(code);
				case Z_STREAM_ERROR: return "Stream error";
				case Z_DATA_ERROR: return "Data error";
				case Z_MEM_ERROR: return "Memory error";
				case Z_BUF_ERROR: return "Buffer error";
				case Z_VERSION_ERROR: return "Version error";
				default: return std::format("Unknown return code {}", code);
			}
		}

		explicit ZlibError(int returnCode)
			: std::runtime_error(DescribeReturnCode(returnCode)) {
		}
	};

	class ZlibReusableInflater {
		const int m_windowBits;
		const size_t m_defaultBufferSize;
		z_stream m_zstream{};
		bool m_initialized = false;
		std::vector<uint8_t> m_buffer;

		void Initialize() {
			int res;
			if (!m_initialized) {
				res = inflateInit2(&m_zstream, m_windowBits);
				m_initialized = true;
			} else
				res = inflateReset2(&m_zstream, m_windowBits);
			if (res != Z_OK)
				throw ZlibError(res);
		}

	public:
		explicit ZlibReusableInflater(int windowBits = 15, int defaultBufferSize = 16384)
			: m_windowBits(windowBits)
			, m_defaultBufferSize(defaultBufferSize) {
		}

		~ZlibReusableInflater() {
			if (m_initialized)
				inflateEnd(&m_zstream);
		}

		std::span<uint8_t> operator()(std::span<const uint8_t> source) {
			Initialize();

			m_zstream.next_in = const_cast<Bytef*>(&source[0]);
			m_zstream.avail_in = static_cast<uint32_t>(source.size());

			if (m_buffer.size() < m_defaultBufferSize)
				m_buffer.resize(m_defaultBufferSize);
			while (true) {
				m_zstream.next_out = &m_buffer[m_zstream.total_out];
				m_zstream.avail_out = static_cast<uint32_t>(m_buffer.size() - m_zstream.total_out);

				if (const auto res = inflate(&m_zstream, Z_FINISH);
					res != Z_OK && res != Z_BUF_ERROR && res != Z_STREAM_END) {
					throw ZlibError(res);
				} else {
					if (res == Z_STREAM_END)
						break;
					m_buffer.resize(m_buffer.size() + std::min<size_t>(m_buffer.size(), 65536));
				}
			}

			return std::span(m_buffer).subspan(0, m_zstream.total_out);
		}

		std::span<uint8_t> operator()(std::span<const uint8_t> source, size_t maxSize) {
			if (m_buffer.size() < maxSize)
				m_buffer.resize(maxSize);

			return operator()(source, std::span(m_buffer));
		}

		std::span<uint8_t> operator()(std::span<const uint8_t> source, std::span<uint8_t> target) {
			Initialize();

			m_zstream.next_in = const_cast<Bytef*>(&source[0]);
			m_zstream.avail_in = static_cast<uint32_t>(source.size());
			m_zstream.next_out = &target[0];
			m_zstream.avail_out = static_cast<uint32_t>(target.size());

			if (const auto res = inflate(&m_zstream, Z_FINISH);
				res != Z_OK && res != Z_BUF_ERROR && res != Z_STREAM_END) {
				throw ZlibError(res);
			}

			return target.subspan(0, target.size() - m_zstream.avail_out);
		}
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

		void Initialize() {
			int res;
			if (!m_initialized) {
				res = deflateInit2(&m_zstream, m_level, m_method, m_windowBits, m_memLevel, m_strategy);
				m_initialized = true;
			} else
				res = deflateReset(&m_zstream);
			if (res != Z_OK)
				throw ZlibError(res);
		}

	public:
		explicit ZlibReusableDeflater(
			int level = Z_DEFAULT_COMPRESSION,
			int method = Z_DEFLATED,
			int windowBits = 15,
			int memLevel = 8,
			int strategy = Z_DEFAULT_STRATEGY,
			size_t defaultBufferSize = 16384)
			: m_level(level)
			, m_method(method)
			, m_windowBits(windowBits)
			, m_memLevel(memLevel)
			, m_strategy(strategy)
			, m_defaultBufferSize(defaultBufferSize) {
		}

		~ZlibReusableDeflater() {
			if (m_initialized)
				deflateEnd(&m_zstream);
		}

		std::span<uint8_t> Deflate(std::span<const uint8_t> source) {
			Initialize();

			m_zstream.next_in = const_cast<Bytef*>(&source[0]);
			m_zstream.avail_in = static_cast<uint32_t>(source.size());

			if (m_buffer.size() < m_defaultBufferSize)
				m_buffer.resize(m_defaultBufferSize);
			while (true) {
				m_zstream.next_out = &m_buffer[m_zstream.total_out];
				m_zstream.avail_out = static_cast<uint32_t>(m_buffer.size() - m_zstream.total_out);

				if (const auto res = deflate(&m_zstream, Z_FINISH);
					res != Z_OK && res != Z_BUF_ERROR && res != Z_STREAM_END)
					throw ZlibError(res);
				else {
					if (res == Z_STREAM_END)
						break;
					m_buffer.resize(m_buffer.size() + std::min<size_t>(m_buffer.size(), 65536));
				}
			}

			return m_latestResult = std::span(m_buffer).subspan(0, m_zstream.total_out);
		}

		std::span<uint8_t> operator()(std::span<const uint8_t> source) {
			return Deflate(source);
		}

		const std::span<uint8_t>& Result() const {
			return m_latestResult;
		}
	};
}

#endif
