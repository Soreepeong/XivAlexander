#pragma once

#include "Game/SignatureDefinitions.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Throws if the game's code doesn't have what the signature looks for.
	template<typename T>
	T ResolveGameCode(const XivAlexander::Game::Signatures::ComplexSignature<T>& signature) {
		T value{};
		if (const auto status = signature.Resolve(value); status != XivAlexander::Game::Signatures::ResolveError::Ok)
			throw std::runtime_error(std::format("{} isn't in this version of the game: {}", signature.Name(), status.Detail));
		return value;
	}
}
