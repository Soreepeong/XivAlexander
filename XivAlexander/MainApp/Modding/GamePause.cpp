#include "pch.h"
#include "MainApp/Modding/GamePause.h"

#include "MainApp/App.h"

namespace XivAlexander::Apps::MainApp::Features::Modding {
	GamePause::GamePause(App& app)
		: m_resume(Utils::Win32::Event::Create()) {
		const auto stalled = Utils::Win32::Event::Create();
		m_staller = Utils::Win32::Thread(L"Staller", [&app, stalled, resume = m_resume] {
			app.RunOnGameLoop([stalled, resume] {
				stalled.Set();
				resume.Wait();
			});
		});
		stalled.Wait();
	}

	GamePause::~GamePause() {
		m_resume.Set();
	}
}
