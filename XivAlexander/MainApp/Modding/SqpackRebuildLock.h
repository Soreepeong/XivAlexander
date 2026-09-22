#pragma once

#include <atomic>

#include <xivres/util.on_dtor.h>

#include "Utils/Win32/Handle.h"

namespace XivAlexander::Apps::MainApp::Features::Modding {
	class SqpackRebuildLock {
		std::atomic<uint64_t> m_lastRequestTimestamp = 0;
		Utils::Win32::Event m_requestEvent = Utils::Win32::Event::Create();
		Utils::Win32::Event m_openEvent = Utils::Win32::Event::Create(nullptr, TRUE, TRUE);

	public:
		void OnSqpackRead();
		[[nodiscard]] xivres::util::on_dtor Hold();
	};
}
