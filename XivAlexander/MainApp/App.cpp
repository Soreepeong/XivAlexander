#include "pch.h"
#include "MainApp/App.h"

#include <XivAlexander/XivAlexander.h>
#include "Utils/Win32/Resource.h"
#include "Utils/Win32/TaskDialogBuilder.h"

#include "Config.h"

#include "resource.h"
#include "MainApp/Features/AllIpcMessageLogger.h"
#include "MainApp/Features/AltCodecMusicSupport.h"
#include "MainApp/Features/AudioResampler.h"
#include "MainApp/Features/CrowdFix.h"
#include "MainApp/Features/FontReplacement.h"
#include "MainApp/Features/ImeModeIndicator.h"
#include "MainApp/Features/IpcTypeFinder.h"
#include "MainApp/Features/LoginSessions.h"
#include "MainApp/Features/MainThreadTimingHandler.h"
#include "MainApp/Features/NetworkTimingHandler.h"
#include "MainApp/Features/PatchCode.h"
#include "MainApp/Features/SocketHook.h"
#include "MainApp/Modding/ResourceOverrider.h"
#include "MainApp/Modding/VirtualSqPacks.h"
#include "MainApp/Windows/LogWindow.h"
#include "MainApp/Windows/MainWindow.h"
#include "Misc/DebuggerDetectionDisabler.h"
#include "Misc/FreeGameMutex.h"
#include "Misc/GameInstallationDetector.h"
#include "Misc/Hooks.h"
#include "Misc/Logger.h"
#include "Misc/OpcodeGuesser.h"

struct XivAlexander::Apps::MainApp::App::Implementation_GameWindow final {
	App& App;
	const std::shared_ptr<Config> Config;

	std::mutex RunOnGameLoopMtx;
	std::queue<std::function<void()>> RunOnGameLoopQueue{};

	HWND Handle{};
	DWORD ThreadId{};
	bool IsFocused{};
	std::shared_ptr<Misc::Hooks::WndProcFunction> SubclassHook{};

	std::mutex TitleMtx;
	std::string TitleAlias;
	std::wstring OriginalTitle;
	std::wstring AppliedTitle;

	xivres::util::on_dtor::multi Cleanup;

	const Utils::Win32::Event ReadyEvent = Utils::Win32::Event::Create();
	const Utils::Win32::Event StopEvent = Utils::Win32::Event::Create();
	const Utils::Win32::Thread InitThread;  // Must be the last member variable

	Implementation_GameWindow(MainApp::App& app);
	~Implementation_GameWindow();

	void InitializeThreadBody();

	[[nodiscard]] HWND GetHwnd(bool wait = false) const;
	[[nodiscard]] DWORD GetThreadId(bool wait = false) const;

	void RunOnGameLoop(std::function<void()> f);

	void SetTitleAlias(std::string alias);
	void UpdateWindowTitle(bool removeDecoration = false);

	LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

struct XivAlexander::Apps::MainApp::App::Implementation {
	App& App;
	const Utils::Win32::LoadedModule MyModule;
	const std::shared_ptr<Misc::DebuggerDetectionDisabler> DebuggerDetectionDisabler;
	const std::shared_ptr<Misc::Logger> Logger;
	const std::shared_ptr<Config> Config;

	xivres::util::on_dtor::multi Cleanup;

	bool WindowGhostingDisabled = false;

	std::optional<Features::PatchCode> PatchCode;
	std::optional<Features::AudioResampler> AudioResampler;
	std::optional<Misc::OpcodeGuesser> OpcodeGuesser;
	
	// Mandatory, but initialize late
	std::optional<Features::SocketHook> SocketHook;
	std::optional<Features::Modding::ResourceOverrider> ResourceOverrider;
	std::optional<Features::AltCodecMusicSupport> AltCodecMusicSupport;
	std::optional<Features::LoginSessions> LoginSessions;

	// Optional
	std::optional<Features::ImeModeIndicator> ImeModeIndicator;
	std::optional<Features::CrowdFix> CrowdFix;
	std::optional<Features::FontReplacement> FontReplacement;
	std::optional<Features::NetworkTimingHandler> NetworkTimingHandler;
	std::optional<Features::MainThreadTimingHandler> MainThreadTimingHandler;
	std::optional<Features::IpcTypeFinder> IpcTypeFinder;
	std::optional<Features::AllIpcMessageLogger> AllIpcMessageLogger;

	std::optional<Window::LogWindow> LogWindow;
	std::optional<Window::MainWindow> MainWindow;

	Misc::Hooks::ImportedFunction<void, UINT> ExitProcess{ "kernel32!ExitProcess", "kernel32.dll", "ExitProcess" };

	void SetupMainWindow() {
		if (MainWindow)
			return;

		MainWindow.emplace(App, [this] {
			if (this->App.m_bInternalUnloadInitiated)
				return;

			if (const auto err = App.IsUnloadable(); !err.empty()) {
				Dll::MessageBoxF(MainWindow->Handle(), MB_ICONERROR, Config->Runtime.FormatStringRes(IDS_ERROR_UNLOAD_XIVALEXANDER, err));
				return;
			}

			this->App.m_bInternalUnloadInitiated = true;
			void(Utils::Win32::Thread(L"XivAlexander::App::XivAlexApp::Implementation::SetupTrayWindow::XivAlexUnloader", [] {
				Dll::DisableAllApps(nullptr);
				}, Utils::Win32::LoadedModule::LoadMore(Dll::Module())));
			});
	}

	Implementation(MainApp::App& app)
		: App(app)
		, MyModule(Utils::Win32::LoadedModule::LoadMore(Dll::Module()))
		, DebuggerDetectionDisabler(Misc::DebuggerDetectionDisabler::Acquire())
		, Logger(Misc::Logger::Acquire())
		, Config(Config::Acquire()) {

		Cleanup += Config->Runtime.GameWindow.DisableGhosting.AddAndCallOnChange([this] {
			if (Config->Runtime.GameWindow.DisableGhosting)
				DisableProcessWindowsGhosting();
			else if (WindowGhostingDisabled)
				Logger->Log(LogCategory::General, "Window ghosting stays disabled until the game restarts.");
			WindowGhostingDisabled |= Config->Runtime.GameWindow.DisableGhosting.Value();
		});

		Cleanup += [&app] {
			if (const auto hwnd = app.m_pGameWindow->GetHwnd(false)) {
				// Make sure our window procedure hook isn't in progress
				SendMessageW(hwnd, WM_NULL, 0, 0);
			}
		};
	}

	~Implementation() {
		Cleanup.clear();
	}

	[[nodiscard]] std::vector<std::wstring> GetEnabledVersionSensitiveFeatures() const {
		const auto& runtime = Config->Runtime;
		std::vector<std::wstring> features;
		if (runtime.Modding.Enabled)
			features.emplace_back(runtime.GetStringRes(IDS_VERSIONSENSITIVE_MODDING));
		if (runtime.Launch.UseLoginSessionSwitching)
			features.emplace_back(runtime.GetStringRes(IDS_VERSIONSENSITIVE_LOGINSESSIONS));
		{
			const auto& digests = runtime.Opcodes.EnabledPatchCodes.Value();
			for (const auto& entry : *Config->PatchCode.GetEntries()) {
				if (std::ranges::find(digests, entry.Digest) != digests.end())
					features.emplace_back(runtime.FormatStringRes(IDS_VERSIONSENSITIVE_PATCHCODE, xivres::util::unicode::convert<std::wstring>(entry.Patch.Name)));
			}
		}
		if (runtime.Audio.OutputSamplingRate != Features::AudioResampler::GameDefault)
			features.emplace_back(runtime.GetStringRes(IDS_VERSIONSENSITIVE_SAMPLINGRATE));
		if (runtime.Audio.SoxrResampler.Enabled)
			features.emplace_back(runtime.GetStringRes(IDS_VERSIONSENSITIVE_SOXR));
		if (runtime.Modding.UseAltCodecMusicSupport)
			features.emplace_back(runtime.GetStringRes(IDS_VERSIONSENSITIVE_ALTCODEC));
		if (runtime.GameWindow.UseImeModeIndicator)
			features.emplace_back(runtime.GetStringRes(IDS_VERSIONSENSITIVE_IMEMODEINDICATOR));
		if (runtime.CrowdFix.Enabled)
			features.emplace_back(runtime.GetStringRes(IDS_VERSIONSENSITIVE_CROWDFIX));
		if (runtime.FontReplacement.Enabled)
			features.emplace_back(runtime.GetStringRes(IDS_VERSIONSENSITIVE_FONTREPLACEMENT));
		return features;
	}

	void AskVersionSensitiveFeatures(HWND hParent, bool onStartup) {
		using Decision = RuntimeConfigRepository::VersionSensitiveFeaturesDecision;
		auto& runtime = Config->Runtime;

		const auto features = GetEnabledVersionSensitiveFeatures();
		if (features.empty()) {
			runtime.DecideVersionSensitiveFeatures(Decision::Keep);
			return;
		}

		std::wstring gameVersion;
		try {
			gameVersion = xivres::util::unicode::convert<std::wstring>(Misc::GameInstallationDetector::GetGameReleaseInfo().GameVersion);
		} catch (...) {
			// leave it empty
		}

		std::wstring list;
		for (const auto& feature : features)
			list += std::format(L"\n• {}", feature);

		static constexpr int IdKeep = 1001;
		static constexpr int IdKeepTemporarily = 1002;
		static constexpr int IdDisableTemporarily = 1003;
		static constexpr int IdDisable = 1004;
		const auto choice = Utils::Win32::TaskDialog::Builder()
			.WithWindowTitle(Dll::GetGenericMessageBoxTitle())
			.WithParentWindow(hParent)
			.WithInstance(Dll::Module())
			.WithAllowDialogCancellation()
			.WithMainIcon(IDI_TRAY_ICON)
			.WithMainInstruction(std::wstring(runtime.GetStringRes(IDS_VERSIONSENSITIVE_TITLE)))
			.WithContent(runtime.FormatStringRes(IDS_VERSIONSENSITIVE_CONTENT, gameVersion, list))
			.WithButton({.IdSet = true, .Id = IdKeep, .Text = std::wstring(runtime.GetStringRes(IDS_VERSIONSENSITIVE_KEEP))})
			.WithButton({.IdSet = true, .Id = IdKeepTemporarily, .Text = std::wstring(runtime.GetStringRes(IDS_VERSIONSENSITIVE_KEEPTEMPORARILY))})
			.WithButton({.IdSet = true, .Id = IdDisableTemporarily, .Text = std::wstring(runtime.GetStringRes(IDS_VERSIONSENSITIVE_DISABLETEMPORARILY))})
			.WithButton({.IdSet = true, .Id = IdDisable, .Text = std::wstring(runtime.GetStringRes(IDS_VERSIONSENSITIVE_DISABLE))})
			.WithButtonCommandLinks()
			.WithButtonDefault(IdDisableTemporarily)
			.Build()
			.Show()
			.Button;

		switch (choice) {
			case IdKeep:
				runtime.DecideVersionSensitiveFeatures(Decision::Keep);
				break;
			case IdKeepTemporarily:
				runtime.DecideVersionSensitiveFeatures(Decision::KeepTemporarily);
				break;
			case IdDisable:
				runtime.DecideVersionSensitiveFeatures(Decision::Disable);
				break;
			case IdDisableTemporarily:
				runtime.DecideVersionSensitiveFeatures(Decision::DisableTemporarily);
				break;
			default:
				if (onStartup)
					runtime.DecideVersionSensitiveFeatures(Decision::DisableTemporarily);
				break;
		}
	}

	void LoadAfterThisConstruct() {
		if (!Config->Runtime.IsVersionSensitiveFeaturesDecided()) {
			try {
				AskVersionSensitiveFeatures(nullptr, true);
			} catch (const std::exception& e) {
				Logger->Format<LogLevel::Error>(LogCategory::General, "Failed to ask about game-version-sensitive features: {}", e.what());
			}
		}

		PatchCode.emplace(App);
		// early: the mix rate has to be in place before the game sets up its audio
		AudioResampler.emplace(App);
		Cleanup += [this] { AudioResampler.reset(); };

		OpcodeGuesser.emplace(App);
		
		SocketHook.emplace(App);
		Cleanup += [this] { SocketHook.reset(); };

		ResourceOverrider.emplace(App);
		Cleanup += [this] { ResourceOverrider.reset(); };

		AltCodecMusicSupport.emplace(App);
		Cleanup += [this] { AltCodecMusicSupport.reset(); };

		// before the main window, which lists and receives sessions
		LoginSessions.emplace(App);
		Cleanup += [this] { LoginSessions.reset(); };
		{
			const auto showSelectedAlias = [this] {
				const auto sessions = LoginSessions->GetSessions();
				const auto selected = LoginSessions->GetSelectedIndex();
				App.m_pGameWindow->SetTitleAlias(selected < sessions.size() ? sessions[selected].Alias : std::string());
			};
			Cleanup += LoginSessions->OnChange(showSelectedAlias);
			showSelectedAlias();
		}

		Scintilla_RegisterClasses(Dll::Module());
		Cleanup += [] { Scintilla_ReleaseResources(); };

		SetupMainWindow();
		Cleanup += [this] { MainWindow.reset(); };

		Cleanup += ExitProcess.SetHook([this](UINT exitCode) {
			if (this->MainWindow)
				SendMessageW(this->MainWindow->Handle(), WM_CLOSE, exitCode, 2);
			TerminateProcess(GetCurrentProcess(), exitCode);
			});

		Cleanup += Config->Runtime.NetworkTiming.Enabled.AddAndCallOnBoolChange(
			[this] { NetworkTimingHandler.emplace(App); },
			[this] { NetworkTimingHandler.reset(); });

		Cleanup += Config->Runtime.FramerateControl.UseMainThreadTimingHandler.AddAndCallOnBoolChange(
			[this] { MainThreadTimingHandler.emplace(App); },
			[this] { MainThreadTimingHandler.reset(); });

		Cleanup += Config->Runtime.Opcodes.UseOpcodeFinder.AddAndCallOnBoolChange(
			[this] { IpcTypeFinder.emplace(App); },
			[this] { IpcTypeFinder.reset(); });

		Cleanup += Config->Runtime.Opcodes.UseAllIpcMessageLogger.AddAndCallOnBoolChange(
			[this] { AllIpcMessageLogger.emplace(App); },
			[this] { AllIpcMessageLogger.reset(); });

		Cleanup += Config->Runtime.Ui.LogWindow.Show.AddAndCallOnBoolChange(
			[this] { LogWindow.emplace(); },
			[this] { LogWindow.reset(); });

		{
			const auto updateAltCodecMusicSupport = [this] {
				if (Config->Runtime.Modding.UseAltCodecMusicSupport && Config->Runtime.AreVersionSensitiveFeaturesAllowed(LogCategory::AltCodecMusic, "Alternate codecs for musics"))
					AltCodecMusicSupport->Enable();
				else
					AltCodecMusicSupport->Disable();
			};
			Cleanup += Config->Runtime.Modding.UseAltCodecMusicSupport.AddAndCallOnChange(updateAltCodecMusicSupport, [this] { AltCodecMusicSupport->Disable(); });
			Cleanup += Config->Runtime.OnVersionSensitiveFeaturesAllowedChange(updateAltCodecMusicSupport);
		}

		{
			const auto updateImeModeIndicator = [this] {
				if (Config->Runtime.GameWindow.UseImeModeIndicator && Config->Runtime.AreVersionSensitiveFeaturesAllowed(LogCategory::General, "IME mode indicator for Korean and Chinese IMEs")) {
					if (!ImeModeIndicator)
						ImeModeIndicator.emplace();
				} else {
					ImeModeIndicator.reset();
				}
			};
			Cleanup += Config->Runtime.GameWindow.UseImeModeIndicator.AddAndCallOnChange(updateImeModeIndicator, [this] { ImeModeIndicator.reset(); });
			Cleanup += Config->Runtime.OnVersionSensitiveFeaturesAllowedChange(updateImeModeIndicator);
		}

		{
			using Fix = Features::CrowdFix::Fix;
			auto& runtime = Config->Runtime;
			const std::pair<Fix, ConfigItem<bool>*> crowdFixItems[]{
				{Fix::SkipIdleNotifiers, &runtime.CrowdFix.Fixes.SkipIdleNotifiers},
				{Fix::ChainWorkerWakeups, &runtime.CrowdFix.Fixes.ChainWorkerWakeups},
				{Fix::DedupeSkeletonSyncs, &runtime.CrowdFix.Fixes.DedupeSkeletonSyncs},
				{Fix::TrimCullingClear, &runtime.CrowdFix.Fixes.TrimCullingClear},
				{Fix::ShortenAllocatorLock, &runtime.CrowdFix.Fixes.ShortenAllocatorLock},
				{Fix::PoolStagingBlocks, &runtime.CrowdFix.Fixes.PoolStagingBlocks},
				{Fix::FreezeHiddenMinions, &runtime.CrowdFix.Fixes.FreezeHiddenMinions},
				{Fix::SkipPrepareWait, &runtime.CrowdFix.Fixes.SkipPrepareWait},
				{Fix::InlineBgPrep, &runtime.CrowdFix.Fixes.InlineBgPrep},
				{Fix::SkipHiddenHotbars, &runtime.CrowdFix.Fixes.SkipHiddenHotbars},
				{Fix::ParallelAnimTail, &runtime.CrowdFix.Fixes.ParallelAnimTail},
				{Fix::SplitCharacterCulling, &runtime.CrowdFix.Fixes.SplitCharacterCulling},
				{Fix::PerItemCullingClaims, &runtime.CrowdFix.Fixes.PerItemCullingClaims},
				{Fix::GatherUsedCommands, &runtime.CrowdFix.Fixes.GatherUsedCommands},
			};
			static_assert(std::size(crowdFixItems) == static_cast<size_t>(Fix::Count));

			const auto updateCrowdFix = [this, crowdFixItems] {
				if (Config->Runtime.CrowdFix.Enabled && Config->Runtime.AreVersionSensitiveFeaturesAllowed(LogCategory::General, "CrowdFix")) {
					if (!CrowdFix) {
						CrowdFix.emplace();
						for (const auto& [fix, item] : crowdFixItems)
							CrowdFix->SetEnabled(fix, item->Value());
					}
				} else {
					CrowdFix.reset();
				}
			};
			Cleanup += runtime.CrowdFix.Enabled.AddAndCallOnChange(updateCrowdFix, [this] { CrowdFix.reset(); });
			Cleanup += runtime.OnVersionSensitiveFeaturesAllowedChange(updateCrowdFix);
			for (const auto& [fix, item] : crowdFixItems) {
				Cleanup += item->OnChange([this, fix, item] {
					if (CrowdFix)
						CrowdFix->SetEnabled(fix, item->Value());
				});
			}
		}

		{
			// Set up off the game's thread, which it waits for; a version of the game it can't work with leaves it off.
			const auto updateFontReplacement = [this] {
				if (Config->Runtime.FontReplacement.Enabled && Config->Runtime.AreVersionSensitiveFeaturesAllowed(LogCategory::FontReplacement, "Font replacement")) {
					if (!FontReplacement) {
						try {
							FontReplacement.emplace(App);
						} catch (const std::exception& e) {
							Logger->Format<LogLevel::Error>(LogCategory::FontReplacement, "Font replacement is off: {}", e.what());
						}
					}
				} else {
					FontReplacement.reset();
				}
			};
			// The feature applies its other settings itself; only turning it on or off is acted on here.
			Cleanup += Config->Runtime.FontReplacement.Enabled.AddAndCallOnChange(updateFontReplacement, [this] { FontReplacement.reset(); });
			Cleanup += Config->Runtime.OnVersionSensitiveFeaturesAllowedChange(updateFontReplacement);
		}
	}
};

XivAlexander::Apps::MainApp::App::Implementation_GameWindow::Implementation_GameWindow(MainApp::App& app)
	: App(app)
	, Config(Config::Acquire())
	, InitThread(Utils::Win32::Thread(L"XivAlexApp::Implementation_GameWindow::Initializer", [this] { InitializeThreadBody(); })) {
}

XivAlexander::Apps::MainApp::App::Implementation_GameWindow::~Implementation_GameWindow() {
	StopEvent.Set();
	ReadyEvent.Set();
	InitThread.Wait();
	Cleanup.clear();
}

void XivAlexander::Apps::MainApp::App::Implementation_GameWindow::InitializeThreadBody() {
	do {
		if (StopEvent.Wait(100) == WAIT_OBJECT_0)
			return;
		Handle = Dll::FindGameMainWindow(false);
	} while (!Handle);

	ThreadId = GetWindowThreadProcessId(Handle, nullptr);

	SubclassHook = std::make_shared<Misc::Hooks::WndProcFunction>("GameMainWindow", Handle);
	IsFocused = GetForegroundWindow() == Handle;

	Cleanup += SubclassHook->SetHook([this](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		return SubclassProc(hwnd, msg, wParam, lParam);
		});

	auto& config = App.m_pImpl->Config->Runtime;
	if (config.GameWindow.AlwaysOnTop)
		SetWindowPos(Handle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	else
		SetWindowPos(Handle, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	Cleanup += config.GameWindow.AlwaysOnTop.OnChange([&] {
		if (config.GameWindow.AlwaysOnTop)
			SetWindowPos(Handle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		else
			SetWindowPos(Handle, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		});
	Cleanup += [this] { SetWindowPos(Handle, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); };

	Cleanup += Config->Runtime.GameWindow.TitleMode.AddAndCallOnChange([this] { UpdateWindowTitle(); });
	Cleanup += Config->Runtime.GameWindow.TitlePrefixFormat.OnChange([this] { UpdateWindowTitle(); });
	Cleanup += Config->Runtime.GameWindow.TitleSuffixFormat.OnChange([this] { UpdateWindowTitle(); });
	Cleanup += [this] { UpdateWindowTitle(true); };

	ReadyEvent.Set();
	RunOnGameLoop([&] {
		auto lastStep = "";
		try {
			try {
				IPropertyStorePtr store;
				PROPVARIANT pv{};

				lastStep = "SHGetPropertyStoreForWindow";
				if (const auto r = SHGetPropertyStoreForWindow(Handle, IID_IPropertyStore, reinterpret_cast<void**>(&store)); FAILED(r))
					throw _com_error(r);

				lastStep = "InitPropVariantFromString";
				if (const auto r = InitPropVariantFromString(L"SquareEnix.FFXIV", &pv); FAILED(r))
					throw _com_error(r);

				lastStep = "store->SetValue";
				if (const auto r = store->SetValue(PKEY_AppUserModel_ID, pv); FAILED(r))
					throw _com_error(r);

				void(PropVariantClear(&pv));
			} catch (const _com_error& e) {
				if (e.Error() != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
					throw Utils::Win32::Error(e);
				}
			}
		} catch (const Utils::Win32::Error& e) {
			App.m_pImpl->Logger->Format<LogLevel::Warning>(LogCategory::General, "Failed to set System.AppUserModel.ID for the game window at step {}: {}", lastStep, e.what());
		}
		});
}

void XivAlexander::Apps::MainApp::App::Implementation_GameWindow::RunOnGameLoop(std::function<void()> f) {
	if (App.m_bInternalUnloadInitiated)
		return f();

	ReadyEvent.Wait();
	const auto hEvent = Utils::Win32::Event::Create();
	{
		std::lock_guard _lock(RunOnGameLoopMtx);
		RunOnGameLoopQueue.emplace([this, &f, &hEvent] {
			try {
				try {
					f();
				} catch (const _com_error& e) {
					if (e.Error() != HRESULT_FROM_WIN32(ERROR_CANCELLED))
						throw Utils::Win32::Error(e);
				}
			} catch (const std::exception& e) {
				App.m_pImpl->Logger->Log(LogCategory::General, App.m_pImpl->Config->Runtime.FormatStringRes(IDS_ERROR_UNEXPECTED, e.what()), LogLevel::Error);
			} catch (...) {
				App.m_pImpl->Logger->Log(LogCategory::General, App.m_pImpl->Config->Runtime.FormatStringRes(IDS_ERROR_UNEXPECTED, L"?"), LogLevel::Error);
			}
			hEvent.Set();
			});
	}

	SendMessageW(Handle, WM_NULL, 0, 0);
	hEvent.Wait();
}

void XivAlexander::Apps::MainApp::App::Implementation_GameWindow::SetTitleAlias(std::string alias) {
	{
		const auto lock = std::lock_guard(TitleMtx);
		if (TitleAlias == alias)
			return;
		TitleAlias = std::move(alias);
	}
	UpdateWindowTitle();
}

void XivAlexander::Apps::MainApp::App::Implementation_GameWindow::UpdateWindowTitle(bool removeDecoration) {
	const auto lock = std::lock_guard(TitleMtx);
	if (!Handle)
		return;

	std::wstring current(GetWindowTextLengthW(Handle) + 1, L'\0');
	current.resize(GetWindowTextW(Handle, current.data(), static_cast<int>(current.size())));

	if (AppliedTitle.empty() || current != AppliedTitle) {
		OriginalTitle = std::move(current);

		const auto pid = GetCurrentProcessId();
		if (const auto prefix = std::format(L"{} - ", pid); OriginalTitle.starts_with(prefix))
			OriginalTitle.erase(0, prefix.length());
		if (const auto suffix = std::format(L" ({})", pid); OriginalTitle.ends_with(suffix))
			OriginalTitle.resize(OriginalTitle.size() - suffix.length());
	}

	std::string format;
	if (!removeDecoration) {
		switch (Config->Runtime.GameWindow.TitleMode) {
			case GameWindowTitleMode::None:
				break;
			case GameWindowTitleMode::Prefix:
				format = Config->Runtime.GameWindow.TitlePrefixFormat.Value();
				break;
			case GameWindowTitleMode::Suffix:
				format = Config->Runtime.GameWindow.TitleSuffixFormat.Value();
				break;
		}
	}

	if (format.empty()) {
		if (AppliedTitle.empty())
			return;
		AppliedTitle.clear();
		SetWindowTextW(Handle, OriginalTitle.c_str());
		return;
	}

	const auto pid = std::to_wstring(GetCurrentProcessId());
	const auto alias = xivres::util::unicode::convert<std::wstring>(TitleAlias);
	const std::pair<std::wstring_view, std::wstring_view> placeholders[]{
		{L"{title}", OriginalTitle},
		{L"{pid}", pid},
		{L"{alias}", alias},
		{L"{alias_or_pid}", alias.empty() ? std::wstring_view(pid) : std::wstring_view(alias)},
	};
	const auto formatW = xivres::util::unicode::convert<std::wstring>(format);
	std::wstring title;
	for (size_t i = 0; i < formatW.size();) {
		const auto it = std::ranges::find_if(placeholders, [&](const auto& p) { return std::wstring_view(formatW).substr(i).starts_with(p.first); });
		if (it == std::end(placeholders)) {
			title += formatW[i++];
		} else {
			title += it->second;
			i += it->first.size();
		}
	}

	AppliedTitle = title;
	SetWindowTextW(Handle, title.c_str());
}

HWND XivAlexander::Apps::MainApp::App::Implementation_GameWindow::GetHwnd(bool wait /*= false*/) const {
	if (wait && ReadyEvent.Wait(false, { StopEvent }) == WAIT_OBJECT_0 + 1)
		return nullptr;
	return Handle;
}

DWORD XivAlexander::Apps::MainApp::App::Implementation_GameWindow::GetThreadId(bool wait /*= false*/) const {
	if (wait && ReadyEvent.Wait(false, { StopEvent }) == WAIT_OBJECT_0 + 1)
		return 0;
	return ThreadId;
}

bool XivAlexander::Apps::MainApp::App::IsGameWindowFocused() const {
	return m_pGameWindow && m_pGameWindow->IsFocused;
}

LRESULT CALLBACK XivAlexander::Apps::MainApp::App::Implementation_GameWindow::SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	while (!RunOnGameLoopQueue.empty()) {
		std::function<void()> fn;
		{
			std::lock_guard _lock(RunOnGameLoopMtx);
			fn = std::move(RunOnGameLoopQueue.front());
			RunOnGameLoopQueue.pop();
		}
		fn();
	}

	switch (msg) {
		case WM_SETFOCUS:
			IsFocused = true;
			break;
		case WM_KILLFOCUS:
			IsFocused = false;
			break;
	}

	return SubclassHook->bridge(hwnd, msg, wParam, lParam);
}

xivres::util::listener_manager<XivAlexander::Apps::MainApp::App, void, XivAlexander::Apps::MainApp::App&> XivAlexander::Apps::MainApp::App::OnAppCreated;

XivAlexander::Apps::MainApp::App::App()
	: m_pImpl(std::make_unique<Implementation>(*this))
	, m_pGameWindow(std::make_unique<Implementation_GameWindow>(*this))
	, m_loadCompleteEvent(Utils::Win32::Event::Create())
	, m_myLoop(L"XivAlexander::App::XivAlexApp::CustomMessageLoopBody", [this] { CustomMessageLoopBody(); }) {
	m_loadCompleteEvent.Wait();
}

XivAlexander::Apps::MainApp::App::~App() {
	m_loadCompleteEvent.Set();

	if (!IsUnloadable().empty()) {
		// Unloading despite IsUnloadable being set.
		// Either process is terminating to begin with, or something went wrong,
		// so terminating self is the right choice.
		TerminateProcess(GetCurrentProcess(), 0);
	}

	if (m_pImpl->MainWindow)
		SendMessageW(m_pImpl->MainWindow->Handle(), WM_CLOSE, 0, 1);

	m_myLoop.Wait();

	if (!m_bInternalUnloadInitiated) {
		// Being destructed from DllMain(Process detach).
		// Should have been cleaned up first, but couldn't get a chance,
		// so the only thing possible is to force quit, or an error message will appear.
		TerminateProcess(GetCurrentProcess(), 0);
	}

	m_pImpl.reset();
}

void XivAlexander::Apps::MainApp::App::CustomMessageLoopBody() {
	const auto activationContextCleanup = Dll::ActivationContext().With();

	// The windows' file dialogs need a single-threaded apartment; without one, this thread is in the game's
	// multithreaded one, where they hang.
	const auto comInitialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
	const auto comCleanup = xivres::util::on_dtor([comInitialized] {
		if (comInitialized)
			CoUninitialize();
	});

	m_pImpl->LoadAfterThisConstruct();

	m_pImpl->Logger->Log(LogCategory::General, m_pImpl->Config->Runtime.GetLangId(), IDS_LOG_XIVALEXANDER_INITIALIZED);

	try {
		Misc::FreeGameMutex::FreeGameMutex();
	} catch (const std::exception& e) {
		m_pImpl->Logger->Format<LogLevel::Warning>(LogCategory::General, m_pImpl->Config->Runtime.GetLangId(), IDS_ERROR_FREEGAMEMUTEX, e.what());
	}

	m_loadCompleteEvent.Set();
	OnAppCreated(*this);

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0)) {
		auto processed = false;

		for (const auto pWindow : Window::BaseWindow::All()) {
			if (!pWindow || pWindow->IsDestroyed())
				continue;

			const auto hWnd = pWindow->Handle();

			if (const auto hAccel = pWindow->GetThreadAcceleratorTable()) {
				if (TranslateAcceleratorW(hWnd, hAccel, &msg)) {
					processed = true;
					break;
				}
			}

			if (hWnd != msg.hwnd && !IsChild(hWnd, msg.hwnd))
				continue;

			if (const auto hAccel = pWindow->GetWindowAcceleratorTable()) {
				if (TranslateAcceleratorW(hWnd, hAccel, &msg)) {
					processed = true;
					break;
				}
			}

			if (pWindow->IsDialogLike() && ((processed = IsDialogMessageW(hWnd, &msg))))
				break;
		}

		if (!processed) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
}

HWND XivAlexander::Apps::MainApp::App::GetGameWindowHandle(bool wait) const {
	return m_pGameWindow->GetHwnd(wait);
}

DWORD XivAlexander::Apps::MainApp::App::GetGameWindowThreadId(bool wait) const {
	return m_pGameWindow->GetThreadId(wait);
}

bool XivAlexander::Apps::MainApp::App::IsRunningOnGameMainThread() const {
	return GetCurrentThreadId() == m_pGameWindow->ThreadId;
}

void XivAlexander::Apps::MainApp::App::RunOnGameLoop(std::function<void()> f) {
	m_pGameWindow->RunOnGameLoop(std::move(f));
}

std::string XivAlexander::Apps::MainApp::App::IsUnloadable() const {
	if (const auto pszDisabledReason = Dll::GetUnloadDisabledReason())
		return pszDisabledReason;

	if (Dll::IsLoadedAsDependency())
		return xivres::util::unicode::convert<std::string>(m_pImpl->Config->Runtime.GetStringRes(IDS_NOUNLOADREASON_DEPENDENCY));

	if (m_pImpl == nullptr || m_pGameWindow == nullptr)
		return "";

	if (m_pImpl->ResourceOverrider->GetVirtualSqPacks())
		return xivres::util::unicode::convert<std::string>(m_pImpl->Config->Runtime.GetStringRes(IDS_NOUNLOADREASON_MODACTIVE));

	if (m_pImpl->SocketHook && !m_pImpl->SocketHook->IsUnloadable())
		return xivres::util::unicode::convert<std::string>(m_pImpl->Config->Runtime.GetStringRes(IDS_NOUNLOADREASON_SOCKET));

	if (m_pGameWindow->SubclassHook && !m_pGameWindow->SubclassHook->IsDisableable())
		return xivres::util::unicode::convert<std::string>(m_pImpl->Config->Runtime.GetStringRes(IDS_NOUNLOADREASON_WNDPROC));

	return "";
}

XivAlexander::Apps::MainApp::Features::SocketHook& XivAlexander::Apps::MainApp::App::GetSocketHook() {
	return *m_pImpl->SocketHook;
}

XivAlexander::Apps::MainApp::Features::Modding::ResourceOverrider& XivAlexander::Apps::MainApp::App::GetResourceOverrider() {
	return *m_pImpl->ResourceOverrider;
}

std::optional<XivAlexander::Apps::MainApp::Features::NetworkTimingHandler>& XivAlexander::Apps::MainApp::App::GetNetworkTimingHandler() {
	return m_pImpl->NetworkTimingHandler;
}

std::optional<XivAlexander::Apps::MainApp::Features::MainThreadTimingHandler>& XivAlexander::Apps::MainApp::App::GetMainThreadTimingHelper() {
	return m_pImpl->MainThreadTimingHandler;
}

std::optional<XivAlexander::Apps::MainApp::Features::LoginSessions>& XivAlexander::Apps::MainApp::App::GetLoginSessions() {
	return m_pImpl->LoginSessions;
}

void XivAlexander::Apps::MainApp::App::AskVersionSensitiveFeatures(HWND hParent) {
	m_pImpl->AskVersionSensitiveFeatures(hParent, false);
}

size_t Dll::EnableXivAlexander(size_t bEnable) {
	static std::unique_ptr<XivAlexander::Apps::MainApp::App> s_app;

	if (IsLoadedAsDependency())
		return -1;

	if (!!bEnable == !!s_app)
		return 0;
	try {
		if (s_app && !bEnable) {
			if (const auto reason = s_app->IsUnloadable(); !reason.empty()) {
				Utils::Win32::DebugPrint(L"Cannot unload: {}", reason);
				return -2;
			}
		}
		s_app = bEnable ? std::make_unique<XivAlexander::Apps::MainApp::App>() : nullptr;
		return 0;
	} catch (const std::exception& e) {
		Utils::Win32::DebugPrint(L"LoadXivAlexander error: {}\n", e.what());
		if (bEnable)
			MessageBoxF(nullptr, MB_ICONERROR | MB_OK,
				FindStringResourceEx(Module(), IDS_ERROR_LOAD) + 1,
				e.what());
		return -1;
	}
}

size_t Dll::ReloadConfiguration(void*) {
	const auto config = XivAlexander::Config::Acquire();
	config->Runtime.Reload();
	config->Game.Reload();
	config->PatchCode.Reload();
	return 0;
}
