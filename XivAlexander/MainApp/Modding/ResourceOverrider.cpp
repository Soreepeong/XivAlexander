#include "pch.h"
#include "MainApp/Modding/ResourceOverrider.h"

#include "Utils/Win32/Process.h"

#include "MainApp/Modding/SqpackRebuildLock.h"
#include "MainApp/Modding/PathRewriter.h"
#include "MainApp/Modding/SqpackFileHooks.h"
#include "MainApp/Modding/SqpackLookupHooks.h"
#include "MainApp/Modding/TextHooks.h"
#include "MainApp/Modding/VirtualSqPacks.h"
#include "Config.h"
#include "Misc/DebuggerDetectionDisabler.h"
#include "Misc/Logger.h"
#include "XivAlexander.h"

struct XivAlexander::Apps::MainApp::Features::Modding::ResourceOverrider::Implementation {
	App& App;
	const std::shared_ptr<Config> Config;
	const std::shared_ptr<Misc::Logger> Logger;
	const std::shared_ptr<Misc::DebuggerDetectionDisabler> AntiDebugger;
	const std::filesystem::path SqpackPath;
	SqpackRebuildLock IoGate;
	std::optional<VirtualSqPacks> Sqpacks;
	std::atomic_bool SqpacksFailed = false;
	std::optional<PathRewriter> Rewriter;
	std::optional<SqpackFileHooks> FileHooks;
	std::optional<SqpackLookupHooks> LookupHooks;
	std::optional<TextHooks> TextHooks;

	const Utils::Win32::Event StopInitEvent = Utils::Win32::Event::Create();
	Utils::Win32::Thread VirtualSqPackInitThread;
	xivres::util::listener_manager<Implementation, void> OnVirtualSqPacksInitialized;

	Implementation(MainApp::App& app)
		: App(app)
		, Config(Config::Acquire())
		, Logger(Misc::Logger::Acquire())
		, AntiDebugger(Misc::DebuggerDetectionDisabler::Acquire())
		, SqpackPath(Utils::Win32::Process::Current().PathOf().remove_filename() / L"sqpack") {

		if (!Dll::IsLoadedFromEntryPoint())
			return;

		if (!Config->Runtime.Modding.Enabled)
			return;

		if (!Config->Runtime.AreVersionSensitiveFeaturesAllowed(LogCategory::GameResourceOverrider, "Modding"))
			return;

		VirtualSqPackInitThread = Utils::Win32::Thread(L"VirtualSqPackInitThread", [this] {
			try {
				Sqpacks.emplace(App, SqpackPath, IoGate, static_cast<HANDLE>(StopInitEvent));
			} catch (const std::exception& e) {
				SqpacksFailed = true;
				if (StopInitEvent.Wait(0) == WAIT_OBJECT_0)
					return;
				Logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, L"Failed to load VirtualSqPacks: {}", e.what());
			}

			if (StopInitEvent.Wait(0) != WAIT_OBJECT_0)
				OnVirtualSqPacksInitialized();
		});

		try {
			FileHooks.emplace(IoGate, [this](const std::filesystem::path& path) -> std::shared_ptr<xivres::stream> {
				VirtualSqPackInitThread.Wait();
				return Sqpacks ? Sqpacks->OpenStream(path) : nullptr;
			});

			Rewriter.emplace(SqpackPath.parent_path(), Sqpacks);
			LookupHooks.emplace(*Rewriter);
			TextHooks.emplace();
		} catch (...) {
			StopInitThread();
			throw;
		}
	}

	~Implementation() {
		StopInitThread();
		FileHooks.reset();
		LookupHooks.reset();
		TextHooks.reset();
	}

	// The init thread uses the members, so it has to finish before they go.
	void StopInitThread() {
		if (!VirtualSqPackInitThread)
			return;
		StopInitEvent.Set();
		VirtualSqPackInitThread.Wait();
	}
};

XivAlexander::Apps::MainApp::Features::Modding::ResourceOverrider::ResourceOverrider(App& app)
	: m_pImpl(std::make_unique<Implementation>(app)) {
}

XivAlexander::Apps::MainApp::Features::Modding::ResourceOverrider::~ResourceOverrider() = default;

std::optional<XivAlexander::Apps::MainApp::Features::Modding::VirtualSqPacks>& XivAlexander::Apps::MainApp::Features::Modding::ResourceOverrider::GetVirtualSqPacks() {
	return m_pImpl->Sqpacks;
}

bool XivAlexander::Apps::MainApp::Features::Modding::ResourceOverrider::IsActive() const {
	return m_pImpl->FileHooks.has_value() && !m_pImpl->SqpacksFailed;
}

xivres::util::on_dtor XivAlexander::Apps::MainApp::Features::Modding::ResourceOverrider::OnVirtualSqPacksInitialized(std::function<void()> f) {
	return m_pImpl->OnVirtualSqPacksInitialized(std::move(f));
}
