#pragma once

namespace XivAlexander::Game {
	// LoginSessions refuses its lobby login hook unless the game's copy reads these fields here.
	struct Utf8String {
		const char* StringPtr;
		int64_t BufSize;
		int64_t BufUsed;
		int64_t StringLength;
		bool IsEmpty;
		bool IsUsingInlineBuffer;
		[[maybe_unused]] char InlineBuffer[0x40];
	};

	static_assert(sizeof(Utf8String) == 0x68);

	enum class AtkValueType : uint8_t {
		Undefined = 0,
		Null = 0x1,
		Bool = 0x2,
		Int = 0x3,
		Int64 = 0x4,
		UInt = 0x5,
		UInt64 = 0x6,
		Float = 0x7,
		String = 0x8,
		WideString = 0x9,
		ConstString = 0xA,
		Vector = 0xB,
		Pointer = 0xC,
		AtkValues = 0xD,

		TypeMask = 0xF,

		Managed = 0x20,
		ManagedString = Managed | String,
		ManagedVector = Managed | Vector,
	};

	// LoginSessions refuses its lobby error hook unless the game's getter reads these fields here.
	struct AtkValue {
		AtkValueType Type;
		uint8_t Padding_0x1[7];
		union {
			bool Bool;
			uint8_t Byte;
			int32_t Int;
			int64_t Int64;
			uint32_t UInt;
			uint64_t UInt64;
			float Float;
			char* String;
			wchar_t* WideString;
			const char* ConstString;
			std::vector<AtkValue>* Vector;
			void* Pointer;
			AtkValue* AtkValues;
		};
	};
}
