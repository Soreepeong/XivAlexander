#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <xivres/util.listener_manager.h>

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace XivAlexander::Apps::MainApp::Features {
	class LoginSessions {
	public:
		static constexpr auto CredentialTargetPrefix = L"XivAlexander/LoginSession/";

		static constexpr UINT ReloadMessage = WM_APP + 0x100;

		struct Session {
			std::string Alias;
			std::vector<std::pair<std::string, std::string>> Parameters;

			bool Expired = false;

			bool operator==(const Session&) const = default;
		};

	private:
		struct Implementation;
		std::unique_ptr<Implementation> m_pImpl;

		void NotifyChanged();

	public:
		LoginSessions(App& app);
		~LoginSessions();

		xivres::util::listener_manager<LoginSessions, void> OnChange;

		[[nodiscard]] std::vector<Session> GetSessions() const;
		[[nodiscard]] size_t GetSelectedIndex() const;
		void Select(size_t index);

		void Reload(bool selectMostRecentlyWritten = false);

		/// Removes every session stored in Credential Manager, leaving those of the launch arguments; gets how many.
		size_t ForgetStoredSessions();

		/// Removes the session of the alias from Credential Manager; gets whether it was there.
		bool ForgetStoredSession(const std::string& alias);

		[[nodiscard]] static std::optional<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> GetMostRecentlyStoredArguments();

		void ApplySelectedTo(std::vector<std::pair<std::string, std::string>>& args);
	};
}
