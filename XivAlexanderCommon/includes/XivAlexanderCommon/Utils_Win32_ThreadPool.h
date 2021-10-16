#pragma once

#include "Utils_Win32_Closeable.h"

namespace Utils::Win32 {
	class TpEnvironment {
		TP_CALLBACK_ENVIRON m_environ{};
		int m_threadPriority;
		int m_maxCores;
		PTP_POOL m_pool;
		PTP_CLEANUP_GROUP m_group;
		PTP_WORK m_lastWork = nullptr;

		std::mutex m_workMtx;

		bool m_cancelling = false;

	public:
		TpEnvironment(int maxCores = 0, int threadPriority = THREAD_PRIORITY_BELOW_NORMAL);
		~TpEnvironment();

		[[nodiscard]] size_t ThreadCount() const;

		void SubmitWork(std::function<void()> cb);
		void WaitOutstanding();
		void Cancel();
	};
}