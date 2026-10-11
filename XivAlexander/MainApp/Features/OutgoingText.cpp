#include "pch.h"
#include "MainApp/Features/OutgoingText.h"

#include <xivres/network.h>
#include <xivres/util.on_dtor.h>
#include <xivres/xivstring.h>

#include "Config.h"
#include "Game/SignatureDefinitions.h"
#include "Game/Structs.h"
#include "MainApp/App.h"
#include "MainApp/Features/SocketHook.h"
#include "MainApp/Modding/MergedText.h"
#include "Misc/Hooks.h"
#include "Misc/Logger.h"
#include "Utils/Win32/TaskDialogBuilder.h"
#include "resource.h"
#include "XivAlexander.h"

namespace {
	using XivAlexander::Apps::MainApp::Features::Modding::MergedText;
	using XivAlexander::Apps::MainApp::Features::Modding::OutgoingMessageKind;

	// ProcessShellCommandMessage's flag for the form that goes into the packet; the other form is the sender's own log line.
	constexpr uint8_t SentForm = 1;

	constexpr DWORD UnansweredDropMs = 20000;

	constexpr int IdSend = 1001;
	constexpr int IdDrop = 1002;
	constexpr int IdDisconnect = 1003;

	// Macros dropped, for showing in a dialog.
	std::string Readable(std::string_view text) {
		const auto bytes = std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size());
		std::string result;
		for (size_t i = 0; i < text.size();) {
			if (bytes[i] == static_cast<uint8_t>(xivres::xivstring::StartOfText)) {
				if (const auto macro = xivres::xivstring::read_macro(bytes.subspan(i))) {
					i += macro->size;
					continue;
				}
			}
			result.push_back(bytes[i] < 0x20 ? ' ' : text[i]);
			i++;
		}
		return result;
	}
}

struct XivAlexander::Apps::MainApp::Features::OutgoingText::Implementation {
	App& App;
	const std::shared_ptr<Config> Config;
	const std::shared_ptr<Misc::Logger> Logger;

	std::optional<Misc::Hooks::PointerFunctionOf<Game::Resolved::ProcessShellCommandMessageFn>> ProcessShellCommandMessage;

	// The game's main thread adds and removes connections; teardown reads them from another.
	std::mutex Mtx;
	std::map<SOCKET, SingleConnection*> Connections;
	std::map<SOCKET, uint64_t> Holds;
	uint64_t NextHoldId = 1;
	bool Alive = true;
	std::set<HWND> Dialogs;
	std::vector<Utils::Win32::Thread> DialogThreads;

	xivres::util::on_dtor::multi Cleanup;

	Implementation(MainApp::App& app, bool rewriteChat)
		: App(app)
		, Config(Config::Acquire())
		, Logger(Misc::Logger::Acquire()) {
		if (rewriteChat) {
			Game::Resolved::OutgoingTextFunctions functions;
			if (const auto status = Game::Resolved::OutgoingText.Resolve(functions); status != Game::Signatures::ResolveError::Ok) {
				Logger->Format<LogLevel::Warning>(LogCategory::SocketHook, "[OutgoingText] Merged names in chat are not changed back before sending: {}", status.Detail);
			} else {
				ProcessShellCommandMessage.emplace("PronounModule::ProcessShellCommandMessage", functions.ProcessShellCommandMessage);
				Cleanup += ProcessShellCommandMessage->SetHook([this](void* pronounModule, void* input, uint8_t flag) {
					return ProcessShellCommandMessageDetour(pronounModule, input, flag);
				});
			}
		}

		Cleanup += App.GetSocketHook().OnSocketFound([this](SingleConnection& conn) {
			{
				const auto lock = std::lock_guard(Mtx);
				Connections.insert_or_assign(conn.Socket(), &conn);
			}
			conn.AddOutgoingFFXIVMessageInspector(this, [this, &conn](const xivres::network::message* pMessage) { return Inspect(conn, pMessage); });
		});
		Cleanup += App.GetSocketHook().OnSocketGone([this](SingleConnection& conn) {
			conn.RemoveMessageHandlers(this);
			const auto lock = std::lock_guard(Mtx);
			Connections.erase(conn.Socket());
			Holds.erase(conn.Socket());
		});
	}

	~Implementation() {
		std::vector<Utils::Win32::Thread> threads;
		{
			const auto lock = std::lock_guard(Mtx);
			Alive = false;
			for (const auto hwnd : Dialogs)
				PostMessageW(hwnd, WM_CLOSE, 0, 0);
			threads = std::move(DialogThreads);
		}
		for (const auto& thread : threads)
			thread.Wait();

		Cleanup.clear();

		// Held messages are dropped rather than sent: nobody agreed to send them.
		App.RunOnGameLoop([this] {
			std::vector<SingleConnection*> connections;
			{
				const auto lock = std::lock_guard(Mtx);
				for (const auto& conn : Connections | std::views::values)
					connections.push_back(conn);
				Connections.clear();
				Holds.clear();
			}
			for (const auto conn : connections) {
				conn->RemoveMessageHandlers(this);
				if (conn->IsHoldingOutgoing()) {
					Logger->Format<LogLevel::Warning>(LogCategory::SocketHook, "[OutgoingText] {:x}: dropping the held message, as the check is turned off", conn->Socket());
					conn->ReleaseHeldOutgoing(false);
				}
			}
		});
	}

	static std::pair<std::shared_ptr<const MergedText>, const MergedText::LanguageTable*> CurrentTable(xivres::game_language language) {
		auto merged = MergedText::Current();
		const auto table = merged ? merged->Find(language) : nullptr;
		return {std::move(merged), table};
	}

	void* ProcessShellCommandMessageDetour(void* pronounModule, void* input, uint8_t flag) {
		const auto result = ProcessShellCommandMessage->bridge(pronounModule, input, flag);
		if (flag != SentForm || !result)
			return result;

		try {
			const auto [merged, table] = CurrentTable(Config->Runtime.Modding.Languages.ResourceOverride.Value());
			if (!table)
				return result;

			const auto& original = *static_cast<const Game::Utf8String*>(result);
			if (!original.StringPtr || original.BufUsed <= 1)
				return result;
			const auto text = std::string_view(original.StringPtr, strnlen(original.StringPtr, static_cast<size_t>(original.BufUsed)));
			auto rewritten = Modding::RewriteOutgoingText(text, *merged, *table);
			if (!rewritten)
				return result;

			// Every caller copies the result right away, so one string per thread is enough.
			thread_local std::string s_text;
			thread_local Game::Utf8String s_string;
			s_text = std::move(*rewritten);
			s_string = {
				.StringPtr = s_text.c_str(),
				.BufSize = static_cast<int64_t>(s_text.size() + 1),
				.BufUsed = static_cast<int64_t>(s_text.size() + 1),
				.StringLength = 0,
				.IsEmpty = false,  // the game reads this as "length known", so it counts the characters itself
				.IsUsingInlineBuffer = false,
			};
			Logger->Log(LogCategory::SocketHook, "[OutgoingText] Changed merged names in a chat message back to the game's language");
			return &s_string;
		} catch (const std::exception& e) {
			Logger->Format<LogLevel::Warning>(LogCategory::SocketHook, "[OutgoingText] Chat message left as is: {}", e.what());
			return result;
		}
	}

	SingleConnection::OutgoingVerdict Inspect(SingleConnection& conn, const xivres::network::message* pMessage) {
		using xivres::network::message_header;
		using xivres::network::ipc_header;
		using Verdict = SingleConnection::OutgoingVerdict;

		if (pMessage->Type != xivres::network::message_type::Ipc || pMessage->Length < sizeof(message_header) + sizeof(ipc_header))
			return Verdict::Pass;
		const auto [merged, table] = CurrentTable(Config->Runtime.Modding.Languages.ResourceOverride.Value());
		if (!table)
			return Verdict::Pass;

		const auto data = std::span(pMessage->Data.Ipc.Data.Raw, pMessage->Length - sizeof(message_header) - sizeof(ipc_header));
		const auto findings = Modding::FindMergedText(data, pMessage->Data.Ipc.SubType, *table);
		if (findings.Texts.empty())
			return Verdict::Pass;

		const auto& runtime = Config->Runtime;
		std::wstring kind;
		switch (findings.Kind) {
			case OutgoingMessageKind::ZoneChat:
				kind = runtime.FormatStringRes(IDS_OUTGOINGTEXT_KIND_ZONECHAT, findings.ChatType);
				break;
			case OutgoingMessageKind::Tell:
				kind = runtime.GetStringRes(IDS_OUTGOINGTEXT_KIND_TELL);
				break;
			case OutgoingMessageKind::Channel:
				kind = runtime.GetStringRes(IDS_OUTGOINGTEXT_KIND_CHANNEL);
				break;
			case OutgoingMessageKind::Alliance:
				kind = runtime.GetStringRes(IDS_OUTGOINGTEXT_KIND_ALLIANCE);
				break;
			case OutgoingMessageKind::CrossWorld:
				kind = runtime.GetStringRes(IDS_OUTGOINGTEXT_KIND_CROSSWORLD);
				break;
			default:
				kind = runtime.FormatStringRes(IDS_OUTGOINGTEXT_KIND_OTHER, pMessage->Data.Ipc.SubType);
				break;
		}
		std::string texts;
		for (const auto& text : findings.Texts)
			texts += std::format("{}\"{}\"", texts.empty() ? "" : ", ", Readable(text));

		const auto socket = conn.Socket();
		uint64_t id;
		{
			const auto lock = std::lock_guard(Mtx);
			if (!Alive)
				return Verdict::Pass;
			id = NextHoldId++;
			Holds.insert_or_assign(socket, id);
			std::erase_if(DialogThreads, [](const Utils::Win32::Thread& t) { return t.Wait(0) == WAIT_OBJECT_0; });
			DialogThreads.emplace_back(L"OutgoingText::Ask", [this, socket, id, kind, texts] { Ask(socket, id, kind, texts); });
		}
		Logger->Format<LogLevel::Warning>(LogCategory::SocketHook, "[OutgoingText] {:x}: holding an outgoing message (opcode 0x{:04X}, {}) with merged text {}",
			socket, pMessage->Data.Ipc.SubType, xivres::util::unicode::convert<std::string>(kind), texts);
		return Verdict::Hold;
	}

	void Ask(SOCKET socket, uint64_t id, const std::wstring& kind, const std::string& texts) {
		const auto activationContext = Dll::ActivationContext().With();
		const auto& runtime = Config->Runtime;
		auto shownSeconds = UnansweredDropMs / 1000;

		auto dialog = Utils::Win32::TaskDialog::Builder()
			.WithWindowTitle(Dll::GetGenericMessageBoxTitle())
			.WithParentWindow(App.GetMainWindowHandle())
			.WithInstance(Dll::Module())
			.WithAllowDialogCancellation()
			.WithCallbackTimer()
			.WithMainIcon(TD_WARNING_ICON)
			.WithMainInstruction(std::wstring(runtime.GetStringRes(IDS_OUTGOINGTEXT_TITLE)))
			.WithContent(runtime.FormatStringRes(IDS_OUTGOINGTEXT_CONTENT, kind, xivres::util::unicode::convert<std::wstring>(texts)))
			.WithButton({.IdSet = true, .Id = IdSend, .Text = std::wstring(runtime.GetStringRes(IDS_OUTGOINGTEXT_SEND))})
			.WithButton({.IdSet = true, .Id = IdDrop, .Text = std::wstring(runtime.GetStringRes(IDS_OUTGOINGTEXT_DROP))})
			.WithButton({.IdSet = true, .Id = IdDisconnect, .Text = std::wstring(runtime.GetStringRes(IDS_OUTGOINGTEXT_DISCONNECT))})
			.WithButtonCommandLinks()
			.WithButtonDefault(IdDrop)
			.WithFooter(runtime.FormatStringRes(IDS_OUTGOINGTEXT_FOOTER, shownSeconds))
			.Build();
		dialog.OnCreated = [this](Utils::Win32::TaskDialog& d) {
			const auto lock = std::lock_guard(Mtx);
			if (!Alive) {
				PostMessageW(d.GetHwnd(), WM_CLOSE, 0, 0);
				return;
			}
			Dialogs.insert(d.GetHwnd());
			// Over the game, which may cover the screen.
			SetWindowPos(d.GetHwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		};
		dialog.OnDestroyed = [this](Utils::Win32::TaskDialog& d) {
			const auto lock = std::lock_guard(Mtx);
			Dialogs.erase(d.GetHwnd());
		};
		dialog.OnTimer = [&](Utils::Win32::TaskDialog& d, DWORD elapsedMs) {
			if (elapsedMs >= UnansweredDropMs) {
				SendMessageW(d.GetHwnd(), TDM_CLICK_BUTTON, IdDrop, 0);
				return Utils::Win32::TaskDialog::Processed;
			}
			if (const auto seconds = (UnansweredDropMs - elapsedMs + 999) / 1000; seconds != shownSeconds) {
				shownSeconds = seconds;
				const auto footer = runtime.FormatStringRes(IDS_OUTGOINGTEXT_FOOTER, seconds);
				SendMessageW(d.GetHwnd(), TDM_SET_ELEMENT_TEXT, TDE_FOOTER, reinterpret_cast<LPARAM>(footer.c_str()));
			}
			return Utils::Win32::TaskDialog::Processed;
		};

		auto button = IdDrop;
		try {
			button = dialog.Show().Button;
		} catch (const std::exception& e) {
			Logger->Format<LogLevel::Error>(LogCategory::SocketHook, "[OutgoingText] {:x}: could not ask, dropping the held message: {}", socket, e.what());
		}

		{
			const auto lock = std::lock_guard(Mtx);
			if (!Alive)
				return;
		}
		App.RunOnGameLoop([this, socket, id, button] { Decide(socket, id, button); });
	}

	// On the game's main thread.
	void Decide(SOCKET socket, uint64_t id, int button) {
		SingleConnection* conn;
		{
			const auto lock = std::lock_guard(Mtx);
			const auto hold = Holds.find(socket);
			const auto found = Connections.find(socket);
			if (hold == Holds.end() || hold->second != id || found == Connections.end()) {
				Logger->Format(LogCategory::SocketHook, "[OutgoingText] {:x}: the connection holding the message is gone", socket);
				return;
			}
			Holds.erase(hold);
			conn = found->second;
		}

		switch (button) {
			case IdSend:
				Logger->Format<LogLevel::Warning>(LogCategory::SocketHook, "[OutgoingText] {:x}: sending the held message as is, as chosen", socket);
				conn->ReleaseHeldOutgoing(true);
				break;
			case IdDisconnect:
				Logger->Format<LogLevel::Warning>(LogCategory::SocketHook, "[OutgoingText] {:x}: resetting all connections, as chosen", socket);
				App.GetSocketHook().ResetAllConnections();
				break;
			default:
				Logger->Format<LogLevel::Warning>(LogCategory::SocketHook, "[OutgoingText] {:x}: dropping the held message", socket);
				conn->ReleaseHeldOutgoing(false);
				break;
		}
	}
};

XivAlexander::Apps::MainApp::Features::OutgoingText::OutgoingText(App& app, bool rewriteChat)
	: m_pImpl(std::make_unique<Implementation>(app, rewriteChat)) {
}

XivAlexander::Apps::MainApp::Features::OutgoingText::~OutgoingText() = default;
