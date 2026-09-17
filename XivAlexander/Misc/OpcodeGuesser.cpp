#include "pch.h"
#include "Misc/OpcodeGuesser.h"

#include <span>
#include <vector>

#include <XivAlexanderCommon/Utils/Signatures.h>
#include <XivAlexanderCommon/Utils/Win32/LoadedModule.h>

#include "Apps/MainApp/App.h"
#include "Config.h"
#include "Misc/Logger.h"

struct XivAlexander::Misc::OpcodeGuesser::Implementation {
	Apps::MainApp::App& App;
	const std::shared_ptr<Config> Config;
	const std::shared_ptr<Logger> Log;

	struct DetectedOpcodes {
		uint16_t S2C_ActionEffects[5]{
			Config::GameRepository::InvalidIpcType,
			Config::GameRepository::InvalidIpcType,
			Config::GameRepository::InvalidIpcType,
			Config::GameRepository::InvalidIpcType,
			Config::GameRepository::InvalidIpcType,
		};
		uint16_t S2C_ActorControl{Config::GameRepository::InvalidIpcType};
		uint16_t S2C_ActorControlSelf{Config::GameRepository::InvalidIpcType};
		uint16_t S2C_ActorCast{Config::GameRepository::InvalidIpcType};
		uint16_t C2S_ActionRequest{Config::GameRepository::InvalidIpcType};
		uint16_t C2S_ActionRequestGroundTargeted{Config::GameRepository::InvalidIpcType};
	};

	static bool InOpcodeRange(uint32_t v) {
		constexpr uint16_t OpcodeMin = 0x0001;
		constexpr uint16_t OpcodeMax = 0x1000;

		return OpcodeMin <= v && v <= OpcodeMax;
	}

	static uint16_t FindActionRequest(std::span<const uint8_t> span) {
		static const Utils::Signatures::RegexSignature kActionRequest(
			R"(\x48\x8D\x54\x24(.)\x45\x33\xC9\xC7\x44\x24\1(..[\x00-\x03]\x00))");

		for (Utils::Signatures::ScanResult result{}; kActionRequest.Lookup(span, result);) {
			const auto opcode = static_cast<uint16_t>(result.Get<uint32_t>(2));
			if (InOpcodeRange(opcode))
				return opcode;
		}

		return Config::GameRepository::InvalidIpcType;
	}

	static uint16_t FindActionRequestGroundTargeted(std::span<const uint8_t> span) {
		static const Utils::Signatures::RegexSignature kCaller(
			R"((?:\x48\x8B[\x40-\xBF].{0,5}(?:\x41)?\x66\x89[\x44-\x7C]\x24.|(?:\x41)?\x66\x89[\x44-\x7C]\x24.\x48\x8B[\x40-\xBF].{0,5})\xE8(....))");
		static const Utils::Signatures::RegexSignature kGroundTargetedSenderSignature(
			R"(\x66\x89\x44\x24.\xF3\x0F\x11\x4C\x24.\xF3\x0F\x11\x44\x24.\xC7\x44\x24.(....))");

		for (Utils::Signatures::ScanResult call{}; kCaller.Lookup(span, call);) {
			const auto body = Utils::Win32::LoadedModule::MainModule().FunctionAt(call.ResolveAddress<const uint8_t*>(1));
			if (body.empty())
				continue;

			Utils::Signatures::ScanResult res{};
			if (!kGroundTargetedSenderSignature.Lookup(body, res))
				continue;

			const auto value = static_cast<uint16_t>(res.Get<uint32_t>(1));
			if (InOpcodeRange(value))
				return value;
		}

		return Config::GameRepository::InvalidIpcType;
	}

	struct PayloadWriter {
		size_t Offset;
		uint32_t PayloadSize;
		uint16_t Opcode;
	};

	static std::vector<PayloadWriter> FindDutyRecorderPayloadWriters(std::span<const uint8_t> span) {
		// opcode != payload size
		static const Utils::Signatures::RegexSignature kSizeFirst(
			R"(\x48\x83\xEC\x38\x4D\x8B\xC8\x48\xC7\x44\x24\x20(....)\x41\xB8(....))");

		// opcode == payload size
		static const Utils::Signatures::RegexSignature kSizeFromRegister(
			R"(\x48\x83\xEC\x38\x4D\x8B\xC8\x41\xB8(....)\x4C\x89\x44\x24\x20)");

		// opcode != payload size, but conditional
		static const Utils::Signatures::RegexSignature kConditional(
			R"((?:[\x70-\x7F].|\x0F[\x80-\x8F]....)\x41\xB8(....)(\x48\xC7\x44\x24\x20)(....)[\x48\x4C]\x8B.{1,5}\x41?\x8B.{1,5}\x48\x8B.{1,5}\xE8)");

		const auto base = Utils::Win32::LoadedModule::MainModule().Value<ptrdiff_t>();
		
		std::vector<PayloadWriter> result;
		for (Utils::Signatures::ScanResult sr{}; kSizeFirst.Lookup(span, sr);) {
			const auto opcode = static_cast<uint16_t>(sr.Get<uint32_t>(2));
			if (InOpcodeRange(opcode))
				result.push_back(PayloadWriter{reinterpret_cast<ptrdiff_t>(sr.begin(0)) - base, sr.Get<uint32_t>(1), opcode});
		}

		for (Utils::Signatures::ScanResult sr{}; kSizeFromRegister.Lookup(span, sr);) {
			const auto value = sr.Get<uint32_t>(1);
			const auto opcode = static_cast<uint16_t>(value);
			if (InOpcodeRange(opcode))
				result.push_back(PayloadWriter{reinterpret_cast<ptrdiff_t>(sr.begin(0)) - base, value, opcode});
		}

		for (Utils::Signatures::ScanResult sr{}; kConditional.Lookup(span, sr);) {
			const auto opcode = static_cast<uint16_t>(sr.Get<uint32_t>(1));
			if (InOpcodeRange(opcode))
				result.push_back(PayloadWriter{reinterpret_cast<ptrdiff_t>(sr.begin(0)) - base, sr.Get<uint32_t>(3), opcode});
		}

		std::ranges::sort(result, [](const auto& a, const auto& b) { return a.Offset < b.Offset; });
		return result;
	}

	void ApplyOne(const char* name, Config::Item<uint16_t>& configValue, uint16_t value) {
		if (value == Config::GameRepository::InvalidIpcType) {
			if (configValue == Config::GameRepository::InvalidIpcType)
				Log->Format<LogLevel::Warning>(LogCategory::OpcodeGuesser, "{}: failed to guess, need manual resolution", name);
			else
				Log->Format<LogLevel::Info>(LogCategory::OpcodeGuesser, "{}: failed to guess; config=0x{:x}", name, configValue.Value());
		} else if (value == configValue) {
			Log->Format<LogLevel::Info>(LogCategory::OpcodeGuesser, "{}: guessing 0x{:x} == configured value", name, value);
		} else if (configValue == Config::GameRepository::InvalidIpcType) {
			Log->Format<LogLevel::Info>(LogCategory::OpcodeGuesser, "{}: guessing and using 0x{:x}", name, value);
			configValue = value;
		} else {
			Log->Format<LogLevel::Warning>(LogCategory::OpcodeGuesser, "{}: guessing 0x{:x} != configured value 0x{:x}", name, value, configValue.Value());
		}
	}

	void Apply(const DetectedOpcodes& detected) {
		auto& game = Config->Game;
		ApplyOne("S2C_ActionEffect01", game.S2C_ActionEffects[0], detected.S2C_ActionEffects[0]);
		ApplyOne("S2C_ActionEffect08", game.S2C_ActionEffects[1], detected.S2C_ActionEffects[1]);
		ApplyOne("S2C_ActionEffect16", game.S2C_ActionEffects[2], detected.S2C_ActionEffects[2]);
		ApplyOne("S2C_ActionEffect24", game.S2C_ActionEffects[3], detected.S2C_ActionEffects[3]);
		ApplyOne("S2C_ActionEffect32", game.S2C_ActionEffects[4], detected.S2C_ActionEffects[4]);
		ApplyOne("S2C_ActorControl", game.S2C_ActorControl, detected.S2C_ActorControl);
		ApplyOne("S2C_ActorControlSelf", game.S2C_ActorControlSelf, detected.S2C_ActorControlSelf);
		ApplyOne("S2C_ActorCast", game.S2C_ActorCast, detected.S2C_ActorCast);
		ApplyOne("C2S_ActionRequest", game.C2S_ActionRequest[0], detected.C2S_ActionRequest);
		ApplyOne("C2S_ActionRequestGroundTargeted", game.C2S_ActionRequest[1], detected.C2S_ActionRequestGroundTargeted);
	}

	void Run() {
		const auto text = Utils::Win32::LoadedModule::MainModule().SectionFrom(".text");
		DetectedOpcodes detected{};

		detected.C2S_ActionRequest = FindActionRequest(text);
		detected.C2S_ActionRequestGroundTargeted = FindActionRequestGroundTargeted(text);

		// (ActionEffect01,) 08, 16, 24, 32, ActorCast, ActorControl, ActorControlTarget, ActorControlSelf
		// (AE01->08: 0x200,) 08->...->32: 0x240
		const auto payloadWriters = FindDutyRecorderPayloadWriters(text);

		for (size_t i = 0; i < payloadWriters.size(); ++i) {
			Log->Format<LogLevel::Debug>(
				LogCategory::OpcodeGuesser,
				"#{:04} opcode=0x{:04x} size=0x{:04x} offset=0x{:08x}",
				i,
				payloadWriters[i].Opcode,
				payloadWriters[i].PayloadSize,
				payloadWriters[i].Offset);
		}

		for (size_t i = 2, streak = 0; i < payloadWriters.size(); ++i) {
			if (payloadWriters[i].PayloadSize == payloadWriters[i - 1].PayloadSize * 2 - payloadWriters[i - 2].PayloadSize
				&& payloadWriters[i].PayloadSize >= 0x200)
				++streak;
			else
				streak = 1;

			if (streak == 3) {
				detected.S2C_ActionEffects[0] = payloadWriters[i - 4].Opcode;
				detected.S2C_ActionEffects[1] = payloadWriters[i - 3].Opcode;
				detected.S2C_ActionEffects[2] = payloadWriters[i - 2].Opcode;
				detected.S2C_ActionEffects[3] = payloadWriters[i - 1].Opcode;
				detected.S2C_ActionEffects[4] = payloadWriters[i + 0].Opcode;
				detected.S2C_ActorCast = payloadWriters[i + 1].Opcode;
				detected.S2C_ActorControl = payloadWriters[i + 2].Opcode;
				// detected.S2C_ActorControlTarget = payloadWriters[i + 3].Opcode;
				detected.S2C_ActorControlSelf = payloadWriters[i + 4].Opcode;
				break;
			}
		}

		Apply(detected);
	}

	Implementation(Apps::MainApp::App& app)
		: App(app)
		, Config(Config::Acquire())
		, Log(Logger::Acquire()) {
		try {
			Run();
		} catch (const std::exception& e) {
			Log->Format<LogLevel::Warning>(LogCategory::OpcodeGuesser, "Opcode autodetection failed: {}", e.what());
		}
	}
};

XivAlexander::Misc::OpcodeGuesser::OpcodeGuesser(Apps::MainApp::App& app)
	: m_pImpl(std::make_unique<Implementation>(app)) {}

XivAlexander::Misc::OpcodeGuesser::~OpcodeGuesser() = default;
