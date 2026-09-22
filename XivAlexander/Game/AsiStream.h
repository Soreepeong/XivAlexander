#pragma once

#include <cstdint>

namespace XivAlexander::Game {
	struct AsiStream;

	enum class AsiStreamFlag : uint8_t {
		None = 0x00,
		NeedsReinit = 0x02,
		EndOfStream = 0x04,
	};

	constexpr AsiStreamFlag operator|(AsiStreamFlag lhs, AsiStreamFlag rhs) {
		return static_cast<AsiStreamFlag>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
	}

	constexpr AsiStreamFlag operator&(AsiStreamFlag lhs, AsiStreamFlag rhs) {
		return static_cast<AsiStreamFlag>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
	}

	constexpr AsiStreamFlag& operator|=(AsiStreamFlag& lhs, AsiStreamFlag rhs) {
		return lhs = lhs | rhs;
	}

	constexpr AsiStreamFlag& operator&=(AsiStreamFlag& lhs, AsiStreamFlag rhs) {
		return lhs = lhs & rhs;
	}

	constexpr AsiStreamFlag operator~(AsiStreamFlag v) {
		return static_cast<AsiStreamFlag>(~static_cast<uint8_t>(v));
	}

	struct AsiStreamUserFfxiv {
		uint32_t StreamerMark;
		AsiStreamFlag Flags;
		uint8_t ObfuscationEnabled;
		uint8_t ObfuscationSeed;
		uint8_t ObfuscationKey;
		AsiStream* Stream;
		void* Window;
	};

	static_assert(offsetof(AsiStreamUserFfxiv, StreamerMark) == 0x000);
	static_assert(offsetof(AsiStreamUserFfxiv, Flags) == 0x004);
	static_assert(offsetof(AsiStreamUserFfxiv, ObfuscationEnabled) == 0x005);
	static_assert(offsetof(AsiStreamUserFfxiv, Stream) == 0x008);
	static_assert(offsetof(AsiStreamUserFfxiv, Window) == 0x010);

	/// AILASIFETCHCB
	using AsiFetchCallback = uint32_t(*)(AsiStreamUserFfxiv* user, uint8_t* dest, uint32_t bytesRequested, uint32_t offset);

	constexpr auto NoFetchOffset = 0xFFFFFFFFU;

	struct AsiStream {
		void* Decoder;
		AsiStreamUserFfxiv* User;
		AsiFetchCallback FetchCallback;
		uint32_t Unknown_0x18;
		uint32_t TotalSize;
		uint64_t Unknown_0x20;
		uint32_t Channels;
		uint32_t BytesPerFrame;
		uint32_t Unknown_0x30;
		uint32_t FetchedBytes;
		uint32_t PendingFetchOffset;
		uint32_t ReentryCount;
		int32_t RequestedRate;
		int32_t RequestedBits;
		int32_t RequestedChannels;
		uint32_t Unknown_0x4C;
		const uint8_t* SourcePtr;
		uint64_t Unknown_0x58;
		uint8_t Buffer[0x10000];
		uint32_t BufferOffset;
		uint32_t BufferRemaining;
	};

	static_assert(offsetof(AsiStream, User) == 0x008);
	static_assert(offsetof(AsiStream, FetchCallback) == 0x010);
	static_assert(offsetof(AsiStream, TotalSize) == 0x01C);
	static_assert(offsetof(AsiStream, Channels) == 0x028);
	static_assert(offsetof(AsiStream, BytesPerFrame) == 0x02C);
	static_assert(offsetof(AsiStream, FetchedBytes) == 0x034);
	static_assert(offsetof(AsiStream, PendingFetchOffset) == 0x038);
	static_assert(offsetof(AsiStream, ReentryCount) == 0x03C);
	static_assert(offsetof(AsiStream, RequestedRate) == 0x040);
	static_assert(offsetof(AsiStream, RequestedChannels) == 0x048);
	static_assert(offsetof(AsiStream, SourcePtr) == 0x050);
	static_assert(offsetof(AsiStream, Buffer) == 0x060);
	static_assert(offsetof(AsiStream, BufferOffset) == 0x10060);
	static_assert(sizeof(AsiStream) == 0x10068); // see ASI_stream_open

	using AsiStreamOpenFn = AsiStream*(*)(AsiStreamUserFfxiv* user, AsiFetchCallback fetchCallback, uint32_t totalSize);

	using AsiStreamSetUpDecoderFn = uint32_t(*)(AsiStream& stream);
	using AsiStreamProcessFn = uint32_t(*)(AsiStream& stream, void* buffer, uint32_t bufferSize);
	using AsiStreamAttributeFn = uint32_t(*)(AsiStream& stream, int attrib);
	using AsiStreamResetFn = void(*)(AsiStream& stream);
}
