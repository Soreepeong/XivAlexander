#pragma once

#include <cinttypes>
#include <span>
#include <type_traits>
#include <vector>

#include "CallOnDestruction.h"

namespace Utils::Oodle {
	using OodleNetwork1_Shared_Size = std::remove_pointer_t<size_t(__stdcall*)(int htbits)>;
	using OodleNetwork1_Shared_SetWindow = std::remove_pointer_t<void (__stdcall*)(void* data, int htbits, void* window, int windowSize)>;
	using OodleNetwork1_Proto_Train = std::remove_pointer_t<void (__stdcall*)(void* state, void* shared, const void* const* trainingPacketPointers, const int* trainingPacketSizes, int trainingPacketCount)>;
	using OodleNetwork1_UDP_Decode = std::remove_pointer_t<bool (__stdcall*)(const void* state, void* shared, const void* compressed, size_t compressedSize, void* raw, size_t rawSize)>;
	using OodleNetwork1_UDP_Encode = std::remove_pointer_t<size_t(__stdcall*)(const void* state, const void* shared, const void* raw, size_t rawSize, void* compressed)>;
	using OodleNetwork1_TCP_Decode = std::remove_pointer_t<bool (__stdcall*)(void* state, void* shared, const void* compressed, size_t compressedSize, void* raw, size_t rawSize)>;
	using OodleNetwork1_TCP_Encode = std::remove_pointer_t<size_t(__stdcall*)(void* state, const void* shared, const void* raw, size_t rawSize, void* compressed)>;
	using OodleNetwork1_Proto_State_Size = std::remove_pointer_t<size_t(__stdcall*)()>;
	using Oodle_Malloc = std::remove_pointer_t<void* (__stdcall*)(size_t size, size_t align)>;
	using Oodle_Free = std::remove_pointer_t<void(__stdcall*)(void* p)>;
	using Oodle_SetMallocFree = std::remove_pointer_t<void(__stdcall*)(Oodle_Malloc* pfnMalloc, Oodle_Free* pfnFree)>;

	class OodleModule {
		std::span<char> m_mem{};
		CallOnDestruction m_memRelease{};

	public:
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
		std::string ErrorStep;

	public:
		OodleModule();
		OodleModule(const OodleModule&) = delete;
		OodleModule(OodleModule&&) = delete;
		OodleModule& operator=(const OodleModule&) = delete;
		OodleModule& operator=(OodleModule&&) = delete;
		~OodleModule();
	};

	class Oodler {
		const OodleModule& m_funcs;

		bool m_udp;
		std::vector<uint8_t> m_state;
		std::vector<uint8_t> m_shared;
		std::vector<uint8_t> m_window;
		std::vector<uint8_t> m_buffer;

	public:
		Oodler(const OodleModule& funcs, bool udp);
		Oodler(const Oodler&) = delete;
		Oodler(Oodler&&) = default;
		Oodler& operator=(const Oodler&) = delete;
		Oodler& operator=(Oodler&&) = default;
		~Oodler();

		std::span<uint8_t> Decode(std::span<const uint8_t> source, size_t decodedLength);

		std::span<uint8_t> Encode(std::span<const uint8_t> source);

		static size_t MaxEncodedSize(size_t n) {
			return n + 8;
		}
	};
}
