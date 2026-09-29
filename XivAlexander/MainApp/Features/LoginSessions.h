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
		// WM_COPYDATA (dwData: UTF-8 JSON)
		// Adds a session. {"Alias": "...", "Parameters": {"DEV.TestSID": "...", ...}, "Select": true (default)}
		static constexpr ULONG_PTR CopyDataId = 0x58414753;

		static constexpr auto SessionsLaunchParameter = "XivAlexander.LoginSessions";

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

		std::optional<LRESULT> HandleCopyData(const COPYDATASTRUCT& cds);

		void ApplySelectedTo(std::vector<std::pair<std::string, std::string>>& args) const;
	};
}
