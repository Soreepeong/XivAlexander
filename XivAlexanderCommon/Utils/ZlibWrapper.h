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

	using OodleNetwork1_Shared_Size = size_t(__stdcall)(int htbits);
	using OodleNetwork1_Shared_SetWindow = void(__stdcall)(void* data, int htbits, void* window, int windowSize);
	using OodleNetwork1_Proto_Train = void(__stdcall)(void* state, void* shared, const void* const* trainingPacketPointers, const int* trainingPacketSizes, int trainingPacketCount);
	using OodleNetwork1_UDP_Decode = bool(__stdcall)(const void* state, void* shared, const void* compressed, size_t compressedSize, void* raw, size_t rawSize);
	using OodleNetwork1_UDP_Encode = size_t(__stdcall)(const void* state, const void* shared, const void* raw, size_t rawSize, void* compressed);
	using OodleNetwork1_TCP_Decode = bool(__stdcall)(void* state, void* shared, const void* compressed, size_t compressedSize, void* raw, size_t rawSize);
	using OodleNetwork1_TCP_Encode = size_t(__stdcall)(void* state, const void* shared, const void* raw, size_t rawSize, void* compressed);
	using OodleNetwork1_Proto_State_Size = int(__stdcall)();
	using Oodle_Malloc = std::remove_pointer_t<void*(__stdcall*)(size_t size, int align)>;
	using Oodle_Free = std::remove_pointer_t<void(__stdcall*)(void* p)>;
	using Oodle_SetMallocFree = std::remove_pointer_t<void(__stdcall*)(Oodle_Malloc* pfnMalloc, Oodle_Free* pfnFree)>;

	struct OodleNetworkFunctions {
		OodleNetwork1_Shared_Size* SharedSize{};
		OodleNetwork1_Shared_SetWindow* SharedSetWindow{};
		OodleNetwork1_Proto_Train* UdpTrain{};
		OodleNetwork1_UDP_Decode* UdpDecode{};
		OodleNetwork1_UDP_Encode* UdpEncode{};
		OodleNetwork1_Proto_State_Size* UdpStateSize{};
		OodleNetwork1_Proto_Train* TcpTrain{};
		OodleNetwork1_TCP_Decode* TcpDecode{};
		OodleNetwork1_TCP_Encode* TcpEncode{};
		OodleNetwork1_Proto_State_Size* TcpStateSize{};
		Oodle_SetMallocFree* SetMallocFree{};
		int HtBits{};
		int WindowSize{};
		bool Found{};
	};
	
	class Oodler {
		OodleNetworkFunctions m_funcs;

		bool m_udp;
		std::vector<uint8_t> m_state;
		std::vector<uint8_t> m_shared;
		std::vector<uint8_t> m_window;
		std::vector<uint8_t> m_buffer;
		
	public:
		Oodler(const OodleNetworkFunctions& funcs, bool udp);

		std::span<uint8_t> Decode(std::span<const uint8_t> source, size_t decodedLength);

		std::span<uint8_t> Encode(std::span<const uint8_t> source);

		static size_t MaxEncodedSize(size_t n) {
			return n + 8;
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
