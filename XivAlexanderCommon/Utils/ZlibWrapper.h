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

	using OodleNetwork1_Shared_Size = int(int htbits);
	using OodleNetwork1_Shared_SetWindow = void(void* data, int htbits, void* window, int windowSize);
	using OodleNetwork1UDP_Train = void(void* state, void* shared, const void* const* trainingPacketPointers, const int* trainingPacketSizes, int trainingPacketCount);
	using OodleNetwork1UDP_Decode = bool(void* state, void* shared, const void* compressed, size_t compressedSize, void* raw, size_t rawSize);
	using OodleNetwork1UDP_Encode = int(const void* state, const void* shared, const void* raw, size_t rawSize, void* compressed);
	using OodleNetwork1UDP_State_Size = int();

	struct OodleNetworkFunctions {
		OodleNetwork1_Shared_Size* OodleNetwork1_Shared_Size;
		OodleNetwork1_Shared_SetWindow* OodleNetwork1_Shared_SetWindow;
		OodleNetwork1UDP_Train* OodleNetwork1UDP_Train;
		OodleNetwork1UDP_Decode* OodleNetwork1UDP_Decode;
		OodleNetwork1UDP_Encode* OodleNetwork1UDP_Encode;
		OodleNetwork1UDP_State_Size* OodleNetwork1UDP_State_Size;
		int htbits;
	};
	
	class Oodler {
		static constexpr int WindowSize = 0x8000;
		
		const OodleNetworkFunctions m_funcs;
		
		std::vector<uint8_t> m_state;
		std::vector<uint8_t> m_shared;
		std::vector<uint8_t> m_window;
		
		std::vector<uint8_t> m_buffer;
		
	public:
		Oodler(const OodleNetworkFunctions& funcs)
			: m_funcs(funcs)
			, m_state(m_funcs.OodleNetwork1UDP_State_Size())
			, m_shared(m_funcs.OodleNetwork1_Shared_Size(m_funcs.htbits))
			, m_window(WindowSize)
			, m_buffer(65536) {

			m_funcs.OodleNetwork1_Shared_SetWindow(&m_shared[0], m_funcs.htbits, &m_window[0], static_cast<int>(m_window.size()));
			m_funcs.OodleNetwork1UDP_Train(&m_state[0], &m_shared[0], nullptr, nullptr, 0);
		}

		std::span<uint8_t> decode(std::span<const uint8_t> source, size_t decodedLength) {
			m_buffer.resize(decodedLength);
			if (!m_funcs.OodleNetwork1UDP_Decode(&m_state[0], &m_shared[0], &source[0], static_cast<int>(source.size()), &m_buffer[0], static_cast<int>(decodedLength)))
				throw std::runtime_error("OodleNetwork1UDP_Decode error");
			return std::span(m_buffer);
		}

		std::span<uint8_t> encode(std::span<const uint8_t> source) {
			m_buffer.resize(source.size());
			const auto size = m_funcs.OodleNetwork1UDP_Encode(&m_state[0], &m_shared[0], &source[0], static_cast<int>(source.size()), &m_buffer[0]);
			if (!size)
				throw std::runtime_error("OodleNetwork1UDP_Encode error");
			return std::span(m_buffer).subspan(0, size);
		}
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
