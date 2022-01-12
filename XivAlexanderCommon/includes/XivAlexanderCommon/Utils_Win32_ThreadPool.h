#pragma once

#include "Utils_Win32_Closeable.h"

namespace Utils::Win32 {
	class TpEnvironment {
		const std::wstring m_name;

		const int m_threadPriority;
		const DWORD m_maxCores;
		const PTP_POOL m_pool;
		const PTP_CLEANUP_GROUP m_group;
		TP_CALLBACK_ENVIRON m_environ{};
		PTP_WORK m_lastWork = nullptr;

		std::mutex m_workMtx;

		bool m_cancelling = false;

	public:
		TpEnvironment(std::wstring name, DWORD preferredThreadCount = UINT32_MAX, int threadPriority = THREAD_PRIORITY_BELOW_NORMAL);
		~TpEnvironment();

		[[nodiscard]] size_t ThreadCount() const;

		void SubmitWork(std::function<void()> cb);
		void WaitOutstanding();
		void Cancel();
	};
}