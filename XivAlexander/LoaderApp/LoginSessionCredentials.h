#pragma once

#include <optional>
#include <string>
#include <vector>

#include <xivres/common.h>

// Login sessions kept in Windows Credential Manager under XivAlexander/LoginSession/*.
// The loader uses this during leftover cleanup; it must not depend on Config or the main app.
namespace XivAlexander::LoaderApp::LoginSessionCredentials {
	static constexpr auto CredentialTargetPrefix = L"XivAlexander/LoginSession/";

	struct StoredSession {
		std::wstring TargetName;
		std::string Alias;
		// Unspecified for a credential written before the region was recorded.
		xivres::game_release_publisher Publisher = xivres::game_release_publisher::Unspecified;
	};

	std::vector<StoredSession> Enumerate();
}
