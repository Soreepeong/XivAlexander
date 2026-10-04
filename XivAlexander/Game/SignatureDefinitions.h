#pragma once

#include <optional>
#include <vector>

#include "Game/AsiStream.h"
#include "Game/ComplexSignature.h"
#include "Game/Oodle.h"
#include "Game/SoundEngine.h"

namespace XivAlexander::Game {
	struct AtkValue;
}

namespace XivAlexander::Game::Resolved {
	struct MssAsiFunctions {
		AsiStreamAttributeFn Attribute{};
		AsiStreamResetFn Reset{};
		AsiStreamOpenFn Open{};
		AsiStreamSetUpDecoderFn SetUpDecoder{};
		AsiStreamProcessFn Process{};
	};

	struct IpcTypeCandidates {
		struct PayloadWriter {
			size_t Offset;
			uint32_t PayloadSize;
			uint16_t Opcode;
		};

		std::vector<PayloadWriter> PayloadWriters;
		std::optional<uint16_t> S2C_ActionEffects[5];
		std::optional<uint16_t> S2C_ActorControl;
		std::optional<uint16_t> S2C_ActorControlSelf;
		std::optional<uint16_t> S2C_ActorCast;
		std::optional<uint16_t> C2S_ActionRequest;
		std::optional<uint16_t> C2S_ActionRequestGroundTargeted;
	};

	// TextService's IME mode for the indicator in the chat input: 0 when closed, otherwise the indicator shows U+E01F + mode.
	using ImeModeGetterFn = uint32_t(*)(void* textService);

	struct ImeModeGetter {
		ImeModeGetterFn Function{};
		// The HIMC the game keeps for its window, which the getter queries.
		void* const* InputContext{};
	};

	[[nodiscard]] std::string to_string(const MssAsiFunctions& value);
	[[nodiscard]] std::string to_string(const IpcTypeCandidates& value);
	[[nodiscard]] std::string to_string(const ImeModeGetter& value);

	using SqPackIndexLookupFn = bool(*)(void* sqpackManager, const char* path, uint32_t* outOffset, uint32_t* outDatIndex);
	using StringIndirectionResolverFn = const char8_t*(*)(const char8_t* str);
	using CutSceneLanguageGetterFn = int(*)(void* p);
	using MessageLoopFn = bool(*)();
	// Starts a lobby login; sessionId is AgentLobby's Utf8String holding DEV.TestSID, and is only copied from.
	using LobbyLoginFn = bool(*)(void* self, void* sessionId, void* arg3, void* arg4, void* arg5, void* arg6, uint8_t arg7, uint8_t arg8);
	// AgentLobby's handler for OK on a lobby error dialog; result is an AtkValue whose low 16 bits are the error code.
	using LobbyErrorDialogFn = uint64_t(*)(void* self, void* arg2, AtkValue& result);

	extern const Signatures::ComplexSignature<std::vector<SqPackIndexLookupFn>> SqPackIndexLookupFunctions;
	extern const Signatures::ComplexSignature<std::vector<StringIndirectionResolverFn>> StringIndirectionResolverFunctions;
	extern const Signatures::ComplexSignature<CutSceneLanguageGetterFn> CutSceneLanguageGetterFunction;

	extern const Signatures::ComplexSignature<Oodle::OodleNetworkFunctions> OodleNetwork;

	extern const Signatures::ComplexSignature<IpcTypeCandidates> IpcTypes;

	extern const Signatures::ComplexSignature<uint32_t*> MixRateSetup;
	extern const Signatures::ComplexSignature<SoundVoiceRenderInfo> VoiceRender;
	extern const Signatures::ComplexSignature<SoundVoiceFunctions> VoiceFunctions;
	extern const Signatures::ComplexSignature<SoundBufferEndInfo> BufferEndHandler;

	extern const Signatures::ComplexSignature<MssAsiFunctions> MssAsiStream;

	extern const Signatures::ComplexSignature<MessageLoopFn> MessageLoopFunction;

	extern const Signatures::ComplexSignature<LobbyLoginFn> LobbyLoginFunction;
	extern const Signatures::ComplexSignature<LobbyErrorDialogFn> LobbyErrorDialogFunction;

	extern const Signatures::ComplexSignature<ImeModeGetter> ImeModeGetterFunction;
}
