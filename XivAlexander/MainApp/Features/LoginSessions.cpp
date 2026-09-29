#include "pch.h"
#include "LoginSessions.h"

#include <XivAlexander/XivAlexander.h>

#include "Game/CommandLine.h"
#include "Game/SignatureDefinitions.h"
#include "Game/Structs.h"
#include "MainApp/App.h"
#include "Utils/Crypt.h"
#include "Misc/Hooks.h"
#include "Misc/Logger.h"

namespace {
	constexpr auto SessionIdKey = "DEV.TestSID";
	constexpr const char* AllowedKeys[]{SessionIdKey, "DEV.MaxEntitledExpansionID"};

	void Validate(const XivAlexander::Apps::MainApp::Features::LoginSessions::Session& session) {
		if (session.Alias.empty() || session.Alias.size() > 64)
			throw std::invalid_argument("Alias must be 1 to 64 bytes long");
		if (std::ranges::any_of(session.Alias, [](char c) { return static_cast<uint8_t>(c) < 0x20; }))
			throw std::invalid_argument("Alias must not contain control characters");

		auto hasSessionId = false;
		for (const auto& [key, value] : session.Parameters) {
			if (std::ranges::find(AllowedKeys, key) == std::end(AllowedKeys))
				throw std::invalid_argument(std::format("Parameter {} is not accepted", key));

			if (value.empty() || value.size() > 1024 || std::ranges::any_of(value, [](char c) { return c <= 0x20 || c >= 0x7F || c == '"'; }))
				throw std::invalid_argument(std::format("Parameter {} has an invalid value", key));

			if (key == SessionIdKey)
				hasSessionId = true;
		}
		if (!hasSessionId)
			throw std::invalid_argument(std::format("Parameter {} is missing", SessionIdKey));
	}

	XivAlexander::Apps::MainApp::Features::LoginSessions::Session SessionFromJson(const nlohmann::json& json) {
		XivAlexander::Apps::MainApp::Features::LoginSessions::Session session;
		session.Alias = json.at("Alias").get<std::string>();
		for (const auto& [key, value] : json.at("Parameters").items())
			session.Parameters.emplace_back(key, value.get<std::string>());
		Validate(session);
		return session;
	}

	nlohmann::json SessionToJson(const XivAlexander::Apps::MainApp::Features::LoginSessions::Session& session) {
		auto parameters = nlohmann::json::object();
		for (const auto& [key, value] : session.Parameters)
			parameters[key] = value;
		return {{"Alias", session.Alias}, {"Parameters", std::move(parameters)}};
	}
}

struct XivAlexander::Apps::MainApp::Features::LoginSessions::Implementation {
	static constexpr uint16_t SessionRejectedErrorCodes[]{13001, 13100};

	enum class LobbyErrorAction : uint16_t {
		Retry = 0,
		Exit = 4,
	};

	LoginSessions& Owner;
	App& App;
	const std::shared_ptr<Misc::Logger> Logger;

	mutable std::mutex Mtx;
	std::vector<Session> Sessions;
	size_t Selected = 0;
	std::string LastLoginSid;

	std::optional<Misc::Hooks::PointerFunctionOf<Game::Resolved::LobbyLoginFn>> LobbyLogin;
	std::optional<Misc::Hooks::PointerFunctionOf<Game::Resolved::LobbyErrorDialogFn>> LobbyErrorDialog;

	xivres::util::on_dtor::multi Cleanup;

	Implementation(LoginSessions& owner, MainApp::App& app)
		: Owner(owner)
		, App(app)
		, Logger(Misc::Logger::Acquire()) {
		LoadStartupSessions();

		try {
			Game::Resolved::LobbyLoginFn lobbyLogin;
			if (const auto status = Game::Resolved::LobbyLoginFunction.Resolve(lobbyLogin); status != Game::Signatures::ResolveError::Ok)
				throw std::runtime_error(status.Detail);
			LobbyLogin.emplace("LobbyLogin", lobbyLogin);
			Cleanup += LobbyLogin->SetHook([this](void* self, void* sessionId, void* arg3, void* arg4, void* arg5, void* arg6, uint8_t arg7, uint8_t arg8) {
				std::string alias, sid;
				auto expired = false;
				{
					const auto lock = std::lock_guard(Mtx);
					if (Selected < Sessions.size()) {
						alias = Sessions[Selected].Alias;
						expired = Sessions[Selected].Expired;
						for (const auto& [key, value] : Sessions[Selected].Parameters)
							if (key == SessionIdKey)
								sid = value;
					}
				}
				if (sid.empty()) {
					Logger->Format(LogCategory::General, "Logging in with the game's own session (mode {})", arg8);
					if (const auto original = static_cast<const Game::Utf8String*>(sessionId); original && original->StringPtr) {
						const auto lock = std::lock_guard(Mtx);
						LastLoginSid.assign(original->StringPtr, static_cast<size_t>(original->StringLength));
					}
					return LobbyLogin->bridge(self, sessionId, arg3, arg4, arg5, arg6, arg7, arg8);
				}

				{
					const auto lock = std::lock_guard(Mtx);
					LastLoginSid = sid;
				}
				if (expired)
					Logger->Format<LogLevel::Warning>(LogCategory::General, "Logging in with session \"{}\" (mode {}), which the lobby rejected before", alias, arg8);
				else
					Logger->Format(LogCategory::General, "Logging in with session \"{}\" (mode {})", alias, arg8);
				Game::Utf8String replacement{
					.StringPtr = sid.c_str(),
					.BufSize = static_cast<int64_t>(sid.size() + 1),
					.BufUsed = static_cast<int64_t>(sid.size() + 1),
					.StringLength = static_cast<int64_t>(sid.size()),
					.IsEmpty = false,
					.IsUsingInlineBuffer = false,
				};
				return LobbyLogin->bridge(self, &replacement, arg3, arg4, arg5, arg6, arg7, arg8);
			});
		} catch (const std::exception& e) {
			Logger->Format<LogLevel::Warning>(LogCategory::General, "Cannot switch login sessions in the running game; restarting will still use the chosen session: {}", e.what());
		}

		try {
			Game::Resolved::LobbyErrorDialogFn lobbyErrorDialog;
			if (const auto status = Game::Resolved::LobbyErrorDialogFunction.Resolve(lobbyErrorDialog); status != Game::Signatures::ResolveError::Ok)
				throw std::runtime_error(status.Detail);
			LobbyErrorDialog.emplace("LobbyErrorDialog", lobbyErrorDialog);
			Cleanup += LobbyErrorDialog->SetHook([this](void* self, void* arg2, Game::AtkValue& result) {
				if (result.Type != Game::AtkValueType::UInt)
					return LobbyErrorDialog->bridge(self, arg2, result);

				const auto code = static_cast<uint16_t>(result.UInt);
				Logger->Format(LogCategory::General, "Lobby error {} dismissed (0x{:08X})", code, result.UInt);

				auto then = LobbyErrorAction::Retry;
				if (std::ranges::find(SessionRejectedErrorCodes, code) != std::end(SessionRejectedErrorCodes)) {
					if (const auto next = RejectLastLogin()) {
						Logger->Format(LogCategory::General, "Trying login session \"{}\" next", *next);
					} else {
						Logger->Format<LogLevel::Warning>(LogCategory::General, "No login session left to try; exiting");
						then = LobbyErrorAction::Exit;
					}
				}
				result.UInt = code | (static_cast<uint32_t>(then) << 16);
				return LobbyErrorDialog->bridge(self, arg2, result);
			});
		} catch (const std::exception& e) {
			Logger->Format<LogLevel::Warning>(LogCategory::General, "Cannot tell expired login sessions apart: {}", e.what());
		}
	}

	std::optional<std::string> RejectLastLogin() {
		std::vector<std::string> expired;
		std::optional<std::string> next;
		{
			const auto lock = std::lock_guard(Mtx);
			const auto rejected = std::pair<std::string, std::string>(SessionIdKey, LastLoginSid);
			for (auto& session : Sessions) {
				if (!session.Expired && !LastLoginSid.empty() && std::ranges::find(session.Parameters, rejected) != session.Parameters.end()) {
					session.Expired = true;
					expired.emplace_back(session.Alias);
				}
			}
			for (size_t i = 1; i <= Sessions.size(); i++) {
				if (const auto index = (Selected + i) % Sessions.size(); !Sessions[index].Expired) {
					Selected = index;
					next = Sessions[index].Alias;
					break;
				}
			}
		}
		for (const auto& alias : expired)
			Logger->Format<LogLevel::Warning>(LogCategory::General, "Login session \"{}\" was rejected; marked as expired", alias);
		Owner.NotifyChanged();
		return next;
	}

	~Implementation() {
		Cleanup.clear();
	}

	bool AddOrReplace(std::vector<Session> sessions, bool selectLast) {
		const auto lock = std::lock_guard(Mtx);
		auto changed = false;
		for (auto& session : sessions) {
			size_t index;
			if (const auto it = std::ranges::find(Sessions, session.Alias, &Session::Alias); it != Sessions.end()) {
				index = it - Sessions.begin();
				if (it->Expired && it->Parameters == session.Parameters)
					session.Expired = true;
				if (*it != session) {
					*it = std::move(session);
					changed = true;
				}
			} else {
				index = Sessions.size();
				Sessions.emplace_back(std::move(session));
				changed = true;
			}
			if (selectLast && Selected != index) {
				Selected = index;
				changed = true;
			}
		}
		return changed;
	}

	[[nodiscard]] std::vector<Session> NamedSessionsExcept(size_t skipIndex) const {
		std::vector<Session> result;
		for (size_t i = 0; i < Sessions.size(); i++)
			if (i != skipIndex && !Sessions[i].Alias.empty() && !Sessions[i].Expired)
				result.emplace_back(Sessions[i]);
		return result;
	}

	void ParseSessionMap(const nlohmann::json& map, std::optional<std::string>& alias, std::vector<Session>& sessions) {
		if (const auto it = map.find("Alias"); it != map.end())
			alias = it->get<std::string>();
		if (const auto it = map.find("Sessions"); it != map.end()) {
			for (const auto& item : *it) {
				try {
					sessions.emplace_back(SessionFromJson(item));
				} catch (const std::exception& e) {
					Logger->Format<LogLevel::Warning>(LogCategory::General, "Ignored a login session from the launch arguments: {}", e.what());
				}
			}
		}
	}

	void LoadStartupSessions() {
		std::vector<std::pair<std::string, std::string>> args;
		try {
			args = Game::CommandLine::FromString(Dll::GetOriginalCommandLine());
		} catch (...) {
			// no launch session then
		}

		std::optional<std::string> alias;
		std::vector<Session> sessions;
		for (const auto& [key, value] : args) {
			if (key != SessionsLaunchParameter)
				continue;
			try {
				const auto decoded = Utils::Crypt::Base64UrlDecode(value);
				ParseSessionMap(nlohmann::json::parse(decoded.begin(), decoded.end()), alias, sessions);
			} catch (const std::exception& e) {
				Logger->Format<LogLevel::Warning>(LogCategory::General, "Ignored login sessions from the launch arguments: {}", e.what());
			}
		}

		Session launchSession;
		for (const auto& allowedKey : AllowedKeys)
			for (const auto& [key, value] : args)
				if (key == allowedKey)
					launchSession.Parameters.emplace_back(key, value);
		if (std::ranges::any_of(launchSession.Parameters, [](const auto& p) { return p.first == SessionIdKey; })) {
			launchSession.Alias = alias.value_or(std::string());
			Sessions.emplace_back(std::move(launchSession));
		}

		std::erase_if(sessions, [this](const Session& s) { return !Sessions.empty() && s.Alias == Sessions.front().Alias; });
		AddOrReplace(std::move(sessions), false);
	}
};

XivAlexander::Apps::MainApp::Features::LoginSessions::LoginSessions(App& app)
	: m_pImpl(std::make_unique<Implementation>(*this, app)) {
}

XivAlexander::Apps::MainApp::Features::LoginSessions::~LoginSessions() {
	m_pImpl.reset();
}

void XivAlexander::Apps::MainApp::Features::LoginSessions::NotifyChanged() {
	OnChange();
}

std::vector<XivAlexander::Apps::MainApp::Features::LoginSessions::Session> XivAlexander::Apps::MainApp::Features::LoginSessions::GetSessions() const {
	const auto lock = std::lock_guard(m_pImpl->Mtx);
	return m_pImpl->Sessions;
}

size_t XivAlexander::Apps::MainApp::Features::LoginSessions::GetSelectedIndex() const {
	const auto lock = std::lock_guard(m_pImpl->Mtx);
	return m_pImpl->Selected;
}

void XivAlexander::Apps::MainApp::Features::LoginSessions::Select(size_t index) {
	{
		const auto lock = std::lock_guard(m_pImpl->Mtx);
		if (index >= m_pImpl->Sessions.size() || index == m_pImpl->Selected)
			return;
		m_pImpl->Selected = index;
	}
	OnChange();
}

std::optional<LRESULT> XivAlexander::Apps::MainApp::Features::LoginSessions::HandleCopyData(const COPYDATASTRUCT& cds) {
	const auto payload = cds.cbData ? std::string_view(static_cast<const char*>(cds.lpData), cds.cbData) : std::string_view();
	try {
		switch (cds.dwData) {
			case CopyDataId: {
				const auto parsed = nlohmann::json::parse(payload);
				auto session = SessionFromJson(parsed);
				const auto alias = session.Alias;
				const auto select = parsed.value("Select", true);
				if (m_pImpl->AddOrReplace({std::move(session)}, select))
					OnChange();
				m_pImpl->Logger->Format(LogCategory::General, "Received login session \"{}\"{}", alias, select ? " (selected)" : "");
				return 1;
			}

			default:
				return std::nullopt;
		}
	} catch (const std::exception& e) {
		m_pImpl->Logger->Format<LogLevel::Warning>(LogCategory::General, "Rejected a login session message: {}", e.what());
		return 0;
	}
}

void XivAlexander::Apps::MainApp::Features::LoginSessions::ApplySelectedTo(std::vector<std::pair<std::string, std::string>>& args) const {
	std::erase_if(args, [](const auto& p) { return p.first == SessionsLaunchParameter; });

	const auto lock = std::lock_guard(m_pImpl->Mtx);
	if (m_pImpl->Selected >= m_pImpl->Sessions.size())
		return;

	const auto& selected = m_pImpl->Sessions[m_pImpl->Selected];
	for (const auto& [key, value] : selected.Parameters)
		Game::CommandLine::ModifyParameter(args, key, value);

	auto map = nlohmann::json::object();
	if (!selected.Alias.empty())
		map["Alias"] = selected.Alias;
	if (auto others = m_pImpl->NamedSessionsExcept(m_pImpl->Selected); !others.empty()) {
		auto list = nlohmann::json::array();
		for (const auto& session : others)
			list.emplace_back(SessionToJson(session));
		map["Sessions"] = std::move(list);
	}
	if (!map.empty()) {
		const auto json = map.dump();
		args.emplace_back(SessionsLaunchParameter, Utils::Crypt::Base64UrlEncode(std::span(json)));
	}
}
