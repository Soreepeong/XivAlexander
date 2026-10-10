#include "pch.h"
#include "LoginSessions.h"

#include <wincred.h>

#include <XivAlexander/XivAlexander.h>

#include "Config.h"
#include "Game/CommandLine.h"
#include "Game/SignatureDefinitions.h"
#include "Game/Structs.h"
#include "MainApp/App.h"
#include "Misc/Hooks.h"
#include "Misc/Logger.h"

namespace {
	constexpr auto SessionIdKey = "DEV.TestSID";
	constexpr const char* AllowedKeys[]{SessionIdKey, "DEV.MaxEntitledExpansionID"};

	using ArgumentList = std::vector<std::pair<std::string, std::string>>;

	struct StoredSession {
		XivAlexander::Apps::MainApp::Features::LoginSessions::Session Session;
		ArgumentList Arguments;
		FILETIME LastWritten{};
	};

	void ValidateAlias(const std::string& alias) {
		if (alias.empty() || alias.size() > 64)
			throw std::invalid_argument("Alias must be 1 to 64 bytes long");
		if (std::ranges::any_of(alias, [](char c) { return static_cast<uint8_t>(c) < 0x20; }))
			throw std::invalid_argument("Alias must not contain control characters");
	}

	ArgumentList ArgumentsFromJson(const nlohmann::json& json) {
		const auto& items = json.at("Arguments");
		if (!items.is_array() || items.size() > 64)
			throw std::invalid_argument("Arguments must be an array of at most 64 items");

		ArgumentList arguments;
		for (const auto& item : items) {
			const auto argument = item.get<std::string>();
			const auto eq = argument.find('=');
			if (eq == std::string::npos || eq == 0)
				throw std::invalid_argument("Arguments must be in key=value form");

			auto key = argument.substr(0, eq);
			auto value = argument.substr(eq + 1);
			if (key.size() > 64 || std::ranges::any_of(key, [](char c) { return !std::isalnum(static_cast<uint8_t>(c)) && c != '.' && c != '_'; }))
				throw std::invalid_argument("Argument has an invalid key");
			if (value.empty() || value.size() > 1024 || std::ranges::any_of(value, [](char c) { return c <= 0x20 || c >= 0x7F || c == '"'; }))
				throw std::invalid_argument(std::format("Argument {} has an invalid value", key));
			arguments.emplace_back(std::move(key), std::move(value));
		}
		if (std::ranges::none_of(arguments, [](const auto& p) { return p.first == SessionIdKey; }))
			throw std::invalid_argument(std::format("Argument {} is missing", SessionIdKey));
		return arguments;
	}

	StoredSession StoredSessionFromCredential(const CREDENTIALW& cred) {
		const auto blob = std::string_view(reinterpret_cast<const char*>(cred.CredentialBlob), cred.CredentialBlobSize);
		const auto json = nlohmann::json::parse(blob, nullptr, false);
		if (json.is_discarded())
			throw std::invalid_argument("Invalid JSON");

		StoredSession stored{.LastWritten = cred.LastWritten};
		stored.Session.Alias = json.at("Alias").get<std::string>();
		ValidateAlias(stored.Session.Alias);
		stored.Arguments = ArgumentsFromJson(json);
		for (const auto& allowedKey : AllowedKeys)
			for (const auto& [key, value] : stored.Arguments)
				if (key == allowedKey)
					stored.Session.Parameters.emplace_back(key, value);
		return stored;
	}

	std::wstring CredentialTarget(const std::string& alias) {
		return XivAlexander::Apps::MainApp::Features::LoginSessions::CredentialTargetPrefix + xivres::util::unicode::convert<std::wstring>(alias);
	}

	std::vector<StoredSession> ReadStoredSessions(XivAlexander::Misc::Logger& logger) {
		using XivAlexander::Apps::MainApp::Features::LoginSessions;

		DWORD count = 0;
		PCREDENTIALW* creds = nullptr;
		const auto filter = std::format(L"{}*", LoginSessions::CredentialTargetPrefix);
		if (!CredEnumerateW(filter.c_str(), 0, &count, &creds)) {
			if (const auto err = GetLastError(); err != ERROR_NOT_FOUND)
				logger.Format<XivAlexander::LogLevel::Warning>(XivAlexander::LogCategory::General, "Cannot read login sessions from Credential Manager: {}", Utils::Win32::FormatWindowsErrorMessage(err));
			return {};
		}
		const auto freeCreds = xivres::util::on_dtor([creds] { CredFree(creds); });

		const auto prefixLength = std::wstring_view(LoginSessions::CredentialTargetPrefix).size();
		std::vector<StoredSession> result;
		for (DWORD i = 0; i < count; i++) {
			const auto& cred = *creds[i];
			if (cred.Type != CRED_TYPE_GENERIC)
				continue;

			const auto alias = xivres::util::unicode::convert<std::string>(std::wstring(cred.TargetName).substr(prefixLength));
			try {
				auto stored = StoredSessionFromCredential(cred);
				if (stored.Session.Alias != alias)
					throw std::invalid_argument("Alias does not match the credential name");
				result.emplace_back(std::move(stored));
			} catch (const std::exception& e) {
				logger.Format<XivAlexander::LogLevel::Warning>(XivAlexander::LogCategory::General, "Ignored login session \"{}\" in Credential Manager: {}", alias, e.what());
			}
		}
		return result;
	}

	bool DeleteStoredSessionIfSessionId(const std::string& alias, const std::string& sessionId) {
		const auto target = CredentialTarget(alias);
		PCREDENTIALW cred = nullptr;
		if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred))
			return false;

		{
			const auto freeCred = xivres::util::on_dtor([cred] { CredFree(cred); });
			try {
				const auto stored = StoredSessionFromCredential(*cred);
				if (std::ranges::find(stored.Session.Parameters, std::pair<std::string, std::string>(SessionIdKey, sessionId)) == stored.Session.Parameters.end())
					return false;
			} catch (...) {
				return false;
			}
		}

		return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);
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
	const std::shared_ptr<Config> Config;
	const std::shared_ptr<Misc::Logger> Logger;

	mutable std::mutex Mtx;
	std::optional<Session> LaunchSession;
	std::vector<Session> Sessions;
	size_t Selected = 0;
	std::string LastLoginSid;

	std::optional<Misc::Hooks::PointerFunctionOf<Game::Resolved::LobbyLoginFn>> LobbyLogin;
	std::optional<Misc::Hooks::PointerFunctionOf<Game::Resolved::LobbyErrorDialogFn>> LobbyErrorDialog;

	xivres::util::on_dtor::multi Cleanup;
	xivres::util::on_dtor::multi HookCleanup;

	Implementation(LoginSessions& owner, MainApp::App& app)
		: Owner(owner)
		, App(app)
		, Config(Config::Acquire())
		, Logger(Misc::Logger::Acquire()) {
		LoadStartupSessions();
		Reload(false);

		Cleanup += Config->Runtime.Launch.UseLoginSessionSwitching.AddAndCallOnChange([this] { UpdateHooks(); });
		Cleanup += Config->Runtime.OnVersionSensitiveFeaturesAllowedChange([this] { UpdateHooks(); });
	}

	void UpdateHooks() {
		if (!Config->Runtime.Launch.UseLoginSessionSwitching) {
			RemoveHooks();
		} else if (!Config->Runtime.AreVersionSensitiveFeaturesAllowed(LogCategory::General, "Switching login sessions in the running game")) {
			RemoveHooks();
		} else {
			InstallHooks();
		}
	}

	void RemoveHooks() {
		HookCleanup.clear();
		LobbyLogin.reset();
		LobbyErrorDialog.reset();
	}

	void InstallHooks() {
		if (LobbyLogin || LobbyErrorDialog)
			return;

		try {
			Game::Resolved::LobbyLoginFn lobbyLogin;
			if (const auto status = Game::Resolved::LobbyLoginFunction.Resolve(lobbyLogin); status != Game::Signatures::ResolveError::Ok)
				throw std::runtime_error(status.Detail);
			LobbyLogin.emplace("LobbyLogin", lobbyLogin);
			HookCleanup += LobbyLogin->SetHook([this](void* self, void* sessionId, void* arg3, void* arg4, void* arg5, void* arg6, uint8_t arg7, uint8_t arg8) {
				Owner.Reload();

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
			HookCleanup += LobbyErrorDialog->SetHook([this](void* self, void* arg2, Game::AtkValue& result) {
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
		std::string rejectedSid;
		{
			const auto lock = std::lock_guard(Mtx);
			rejectedSid = LastLoginSid;
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
		for (const auto& alias : expired) {
			Logger->Format<LogLevel::Warning>(LogCategory::General, "Login session \"{}\" was rejected; marked as expired", alias);
			if (!alias.empty() && DeleteStoredSessionIfSessionId(alias, rejectedSid))
				Logger->Format(LogCategory::General, "Removed login session \"{}\" from Credential Manager", alias);
		}
		Owner.NotifyChanged();
		return next;
	}

	~Implementation() {
		Cleanup.clear();
		HookCleanup.clear();
	}

	bool Reload(bool selectMostRecentlyWritten) {
		auto stored = ReadStoredSessions(*Logger);

		const auto lock = std::lock_guard(Mtx);
		const auto firstLoad = Sessions.empty();

		if (LaunchSession && LaunchSession->Alias.empty()) {
			const auto launchSid = std::ranges::find(LaunchSession->Parameters, SessionIdKey, &std::pair<std::string, std::string>::first)->second;
			for (const auto& s : stored) {
				if (std::ranges::find(s.Session.Parameters, std::pair<std::string, std::string>(SessionIdKey, launchSid)) != s.Session.Parameters.end()) {
					LaunchSession->Alias = s.Session.Alias;
					break;
				}
			}
		}

		std::vector<Session> sessions;
		if (LaunchSession && (LaunchSession->Alias.empty() || std::ranges::none_of(stored, [this](const auto& s) { return s.Session.Alias == LaunchSession->Alias; })))
			sessions.emplace_back(*LaunchSession);

		std::optional<size_t> mostRecent;
		FILETIME mostRecentTime{};
		for (auto& s : stored) {
			if (!mostRecent || CompareFileTime(&s.LastWritten, &mostRecentTime) > 0) {
				mostRecent = sessions.size();
				mostRecentTime = s.LastWritten;
			}
			sessions.emplace_back(std::move(s.Session));
		}

		// keep what the lobby said about the same sessions
		for (auto& session : sessions) {
			if (const auto it = std::ranges::find(Sessions, session.Alias, &Session::Alias); it != Sessions.end() && it->Parameters == session.Parameters)
				session.Expired = it->Expired;
		}

		size_t selected = 0;
		if (selectMostRecentlyWritten && mostRecent) {
			selected = *mostRecent;
		} else if (firstLoad) {
			// the session the game was launched with
			if (LaunchSession) {
				if (const auto it = std::ranges::find(sessions, LaunchSession->Alias, &Session::Alias); it != sessions.end())
					selected = it - sessions.begin();
			}
		} else if (Selected < Sessions.size()) {
			if (const auto it = std::ranges::find(sessions, Sessions[Selected].Alias, &Session::Alias); it != sessions.end())
				selected = it - sessions.begin();
		}

		if (sessions == Sessions && selected == Selected)
			return false;

		Sessions = std::move(sessions);
		Selected = selected;
		return true;
	}

	void LoadStartupSessions() {
		std::vector<std::pair<std::string, std::string>> args;
		try {
			args = Game::CommandLine::FromString(Dll::GetOriginalCommandLine());
		} catch (...) {
			// no launch session then
		}

		Session launchSession;
		for (const auto& allowedKey : AllowedKeys)
			for (const auto& [key, value] : args)
				if (key == allowedKey)
					launchSession.Parameters.emplace_back(key, value);
		if (std::ranges::any_of(launchSession.Parameters, [](const auto& p) { return p.first == SessionIdKey; }))
			LaunchSession = std::move(launchSession);
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

std::optional<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> XivAlexander::Apps::MainApp::Features::LoginSessions::GetMostRecentlyStoredArguments() {
	auto stored = ReadStoredSessions(*Misc::Logger::Acquire());
	const auto it = std::ranges::max_element(stored, [](const StoredSession& l, const StoredSession& r) {
		return CompareFileTime(&l.LastWritten, &r.LastWritten) < 0;
	});
	if (it == stored.end())
		return std::nullopt;
	return std::make_pair(std::move(it->Session.Alias), std::move(it->Arguments));
}

void XivAlexander::Apps::MainApp::Features::LoginSessions::Reload(bool selectMostRecentlyWritten) {
	if (!m_pImpl->Reload(selectMostRecentlyWritten))
		return;

	if (selectMostRecentlyWritten) {
		const auto lock = std::lock_guard(m_pImpl->Mtx);
		if (m_pImpl->Selected < m_pImpl->Sessions.size())
			m_pImpl->Logger->Format(LogCategory::General, "Selected login session \"{}\"", m_pImpl->Sessions[m_pImpl->Selected].Alias);
	}
	OnChange();
}

void XivAlexander::Apps::MainApp::Features::LoginSessions::ApplySelectedTo(std::vector<std::pair<std::string, std::string>>& args) {
	Reload();

	const auto lock = std::lock_guard(m_pImpl->Mtx);
	if (m_pImpl->Selected >= m_pImpl->Sessions.size())
		return;

	for (const auto& [key, value] : m_pImpl->Sessions[m_pImpl->Selected].Parameters)
		Game::CommandLine::ModifyParameter(args, key, value);
}
