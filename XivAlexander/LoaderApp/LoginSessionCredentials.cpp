#include "pch.h"
#include "LoginSessionCredentials.h"

#include <wincred.h>

namespace {
	using namespace XivAlexander::LoaderApp::LoginSessionCredentials;

	// Reads the publisher from the blob: an explicit "Publisher", else "SYS.Region" in the launch arguments
	// (DC regions 1/2/3 are all Square Enix, 101 is Actoz Soft), else Unspecified for legacy credentials.
	xivres::game_release_publisher PublisherFromBlob(const CREDENTIALW& cred) {
		try {
			const auto blob = std::string_view(reinterpret_cast<const char*>(cred.CredentialBlob), cred.CredentialBlobSize);
			const auto json = nlohmann::json::parse(blob, nullptr, false);
			if (json.is_discarded() || !json.is_object())
				return xivres::game_release_publisher::Unspecified;

			if (const auto it = json.find("Publisher"); it != json.end() && it->is_string()) {
				xivres::game_release_publisher publisher{};
				xivres::from_json(*it, publisher);
				return publisher;
			}
			if (const auto it = json.find("Arguments"); it != json.end() && it->is_array()) {
				for (const auto& item : *it) {
					if (!item.is_string())
						continue;
					const auto arg = item.get<std::string>();
					if (!arg.starts_with("SYS.Region="))
						continue;
					switch (std::strtol(arg.c_str() + 11, nullptr, 10)) {
						case 1: case 2: case 3: return xivres::game_release_publisher::SquareEnix;
						case 101: return xivres::game_release_publisher::ActozSoft;
						default: return xivres::game_release_publisher::Unspecified;
					}
				}
			}
		} catch (...) {
			// unreadable blob; treat as legacy
		}
		return xivres::game_release_publisher::Unspecified;
	}
}

std::vector<StoredSession> XivAlexander::LoaderApp::LoginSessionCredentials::Enumerate() {
	DWORD count = 0;
	PCREDENTIALW* creds = nullptr;
	const auto filter = std::format(L"{}*", CredentialTargetPrefix);
	if (!CredEnumerateW(filter.c_str(), 0, &count, &creds))
		return {};
	const auto freeCreds = xivres::util::on_dtor([creds] { CredFree(creds); });

	const auto prefixLength = std::wstring_view(CredentialTargetPrefix).size();
	std::vector<StoredSession> result;
	for (DWORD i = 0; i < count; i++) {
		const auto& cred = *creds[i];
		if (cred.Type != CRED_TYPE_GENERIC)
			continue;
		result.emplace_back(StoredSession{
			.TargetName = cred.TargetName,
			.Alias = xivres::util::unicode::convert<std::string>(std::wstring(cred.TargetName).substr(prefixLength)),
			.Publisher = PublisherFromBlob(cred),
		});
	}
	return result;
}
