#pragma once

#include <cstdint>
#include <string>

#include "Game/DynamicStruct.h"

namespace XivAlexander::Game {
	enum class VoiceFormat : int32_t {
		None = 0,
		Int16 = 1,
		Float = 2,
	};

	constexpr int32_t VoiceMaxQueuedBuffers = 2;
	constexpr int32_t VoiceMaxChannels = 16;
	constexpr uint32_t VoiceFailure = 0xFFFFFFFF;

	enum class SoundVoiceState : uint32_t {
		None [[maybe_unused]] = 0,
		Created [[maybe_unused]] = 1,
		Stopped [[maybe_unused]] = 2,
		Playing [[maybe_unused]] = 3,
		Flushed [[maybe_unused]] = 4,
	};

	constexpr SoundVoiceState operator &(SoundVoiceState a, SoundVoiceState b) {
		return static_cast<SoundVoiceState>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
	}

	constexpr SoundVoiceState operator ~(SoundVoiceState a) {
		return static_cast<SoundVoiceState>(~static_cast<uint32_t>(a));
	}

	struct SoundVoice : DynamicVirtualStruct<64, 0xA48> { // 7.56h
		struct Layout {
			size_t State{}; // 0xA8
			size_t QueuedBuffers{}; // 0xD8

			size_t DestructorSlot{}; // 0
			size_t FlushSlot{}; // 7
			size_t SubmitSlot{}; // 12
			size_t SetMarkerSlot{}; // 19
		};

		[[nodiscard]] SoundVoiceState State(const Layout& layout) const { return Field<SoundVoiceState>(layout.State); }
		[[nodiscard]] int32_t QueuedBuffers(const Layout& layout) const { return Field<int32_t>(layout.QueuedBuffers); }
	};

	struct SoundVoiceCallback : DynamicVirtualStruct<4, 0x328> { // 7.56h
		struct Layout {
			// vtbl[0]: dtor
			// vtbl[1]: buffer start
			// vtbl[2]: buffer end
			// vtbl[3]: marker reached

			size_t EndOfData{}; // 0x270 (7.00), 0x248 (6.50-6.58h), 0x208 (6.0x-6.4x)
		};

		[[nodiscard]] const uint8_t* EndOfData(const Layout& layout) const { return &Field<uint8_t>(layout.EndOfData); }
	};

	/// What the voice's render reads, and the mix rate it compares its own rate with.
	struct SoundVoiceRenderInfo {
		size_t State{};
		size_t QueuedBuffers{};
		uint32_t* MixRate{};
	};

	struct SoundVoiceFunctions {
		uint32_t(*Init)(void* voice, int32_t rate, int32_t channels, VoiceFormat format,
			const SoundVoiceCallback* callback, uint64_t sends, uint64_t sendCount, uint64_t outputs, uint64_t outputCount,
			uint64_t layout, uint64_t flag) {};
		uint32_t(*Submit)(void* voice, const void* data, uint64_t bytes, uint64_t context, int32_t startFrame) {};
		uint32_t(*Flush)(void* voice) {};
		uint32_t(*SetMarker)(void* voice, int32_t frame) {};
		void*(*Destructor)(void* voice, uint64_t flags) {};
		SoundVoice::Layout Layout;
	};

	struct SoundBufferEndInfo {
		void(*Handler)(void* callback) {};
		SoundVoiceCallback::Layout CallbackLayout;
	};

	[[nodiscard]] std::string to_string(const SoundVoiceRenderInfo& value);
	[[nodiscard]] std::string to_string(const SoundVoiceFunctions& value);
	[[nodiscard]] std::string to_string(const SoundBufferEndInfo& value);
}
