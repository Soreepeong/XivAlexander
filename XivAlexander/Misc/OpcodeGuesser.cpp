#include "pch.h"
#include "Misc/OpcodeGuesser.h"

#include "Utils/Win32/LoadedModule.h"

#include "MainApp/App.h"
#include "Game/SignatureDefinitions.h"
#include "Config.h"
#include "Misc/Logger.h"

struct XivAlexander::Misc::OpcodeGuesser::Implementation {
	Apps::MainApp::App& App;
	const std::shared_ptr<Config> Config;
	const std::shared_ptr<Logger> Log;

	void ApplyOne(const char* name, ConfigItem<uint16_t>& configValue, std::optional<uint16_t> guess) {
		const auto value = guess.value_or(GameConfigRepository::InvalidIpcType);
		if (value == GameConfigRepository::InvalidIpcType) {
			if (configValue == GameConfigRepository::InvalidIpcType)
				Log->Format<LogLevel::Warning>(LogCategory::OpcodeGuesser, "{}: failed to guess, need manual resolution", name);
			else
				Log->Format<LogLevel::Info>(LogCategory::OpcodeGuesser, "{}: failed to guess; config=0x{:X}", name, configValue.Value());
		} else if (value == configValue) {
			Log->Format<LogLevel::Info>(LogCategory::OpcodeGuesser, "{}: guessing 0x{:X} == configured value", name, value);
		} else if (configValue == GameConfigRepository::InvalidIpcType) {
			Log->Format<LogLevel::Info>(LogCategory::OpcodeGuesser, "{}: guessing and using 0x{:X}", name, value);
			configValue = value;
		} else {
			Log->Format<LogLevel::Warning>(LogCategory::OpcodeGuesser, "{}: guessing 0x{:X} != configured value 0x{:X}", name, value, configValue.Value());
		}
	}

	void Apply(const Game::Resolved::IpcTypeCandidates& detected) {
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
		Game::Resolved::IpcTypeCandidates detected;
		if (const auto status = Game::Resolved::IpcTypes.Resolve(detected); status != Game::Signatures::ResolveError::Ok)
			throw std::runtime_error(status.Detail);

		for (size_t i = 0; i < detected.PayloadWriters.size(); ++i) {
			Log->Format<LogLevel::Debug>(
				LogCategory::OpcodeGuesser,
				"#{:04} opcode=0x{:04x} size=0x{:04x} offset=0x{:08x}",
				i,
				detected.PayloadWriters[i].Opcode,
				detected.PayloadWriters[i].PayloadSize,
				detected.PayloadWriters[i].Offset);
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
