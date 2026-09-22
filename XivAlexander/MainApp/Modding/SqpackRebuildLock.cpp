#include "pch.h"
#include "MainApp/Modding/SqpackRebuildLock.h"

namespace XivAlexander::Apps::MainApp::Features::Modding {
	void SqpackRebuildLock::OnSqpackRead() {
		m_openEvent.Wait();
		m_lastRequestTimestamp = GetTickCount64();
		m_requestEvent.Set();
	}

	xivres::util::on_dtor SqpackRebuildLock::Hold() {
		while (true) {
			const auto waitFor = static_cast<int64_t>(100LL + m_lastRequestTimestamp - GetTickCount64());
			if (waitFor < 0)
				break;
			m_requestEvent.Reset();
			if (WAIT_TIMEOUT == m_requestEvent.Wait(static_cast<DWORD>(waitFor)))
				break;
		}
		m_openEvent.Reset();
		return xivres::util::on_dtor([this] { m_openEvent.Set(); });
	}
}
