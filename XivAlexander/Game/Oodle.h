#pragma once

#include <cinttypes>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace XivAlexander::Game::Oodle {
	struct OodleNetworkFunctions {
		size_t(*SharedSize)(int htbits) {};
		void(*SharedSetWindow)(void* data, int htbits, void* window, int windowSize) {};
		void(*UdpTrain)(void* state, void* shared, const void* const* trainingPacketPointers, const int* trainingPacketSizes, int trainingPacketCount) {};
		bool(*UdpDecode)(const void* state, void* shared, const void* compressed, size_t compressedSize, void* raw, size_t rawSize) {};
		size_t(*UdpEncode)(const void* state, const void* shared, const void* raw, size_t rawSize, void* compressed) {};
		size_t(*UdpStateSize)() {};
		void(*TcpTrain)(void* state, void* shared, const void* const* trainingPacketPointers, const int* trainingPacketSizes, int trainingPacketCount) {};
		bool(*TcpDecode)(void* state, void* shared, const void* compressed, size_t compressedSize, void* raw, size_t rawSize) {};
		size_t(*TcpEncode)(void* state, const void* shared, const void* raw, size_t rawSize, void* compressed) {};
		size_t(*TcpStateSize)() {};
		void(*SetMallocFree)(void*(*pfnMalloc)(size_t size, size_t align), void(*pfnFree)(void* p)) {};
		int HtBits{};
		int WindowSize{};
	};

	[[nodiscard]] std::string to_string(const OodleNetworkFunctions& value);

	class OodleModule : public OodleNetworkFunctions {
	public:
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
