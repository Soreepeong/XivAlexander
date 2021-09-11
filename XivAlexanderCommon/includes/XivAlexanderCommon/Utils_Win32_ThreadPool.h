#pragma once

#include "Utils_Win32_Closeable.h"

namespace Utils::Win32 {
	class TpEnvironment {
		TP_CALLBACK_ENVIRON m_environ{};
		PTP_POOL m_pool;
		PTP_CLEANUP_GROUP m_group;
		PTP_WORK m_lastWork = nullptr;
		SYSTEM_INFO m_systemInfo;

	public:
		TpEnvironment(DWORD maxCores = 0)
			: m_pool(CreateThreadpool(nullptr))
			, m_group(CreateThreadpoolCleanupGroup()) {

			if (!m_pool || !m_group) {
				if (m_pool)
					CloseThreadpool(m_pool);
				if (m_group)
					CloseThreadpoolCleanupGroup(m_group);
				throw Error("CreateThreadpool or CreateThreadpoolCleanupGroup");
			}

			InitializeThreadpoolEnvironment(&m_environ);
			SetThreadpoolCallbackPool(&m_environ, m_pool);
			SetThreadpoolCallbackCleanupGroup(&m_environ, m_group, [](void* objectCtx, void* cleanupCtx) {
				delete static_cast<std::function<void()>*>(objectCtx);
				});

			GetNativeSystemInfo(&m_systemInfo);
			if (maxCores)
				m_systemInfo.dwNumberOfProcessors = maxCores;
			SetThreadpoolThreadMaximum(m_pool, m_systemInfo.dwNumberOfProcessors);
		}

		~TpEnvironment() {
			CloseThreadpoolCleanupGroupMembers(m_group, true, this);
			CloseThreadpoolCleanupGroup(m_group);
			CloseThreadpool(m_pool);
			DestroyThreadpoolEnvironment(&m_environ);
		}

		[[nodiscard]] size_t ThreadCount() const {
			return m_systemInfo.dwNumberOfProcessors;
		}

		void SubmitWork(std::function<void()> cb) {
			const auto obj = new std::function(std::move(cb));
			const auto work = CreateThreadpoolWork([](PTP_CALLBACK_INSTANCE, void* objectCtx, PTP_WORK) {
				(*static_cast<std::function<void()>*>(objectCtx))();
				}, obj, &m_environ);
			if (!work) {
				const auto error = GetLastError();
				delete obj;
				throw Error(error, "CreateThreadpoolWork");
			}
			m_lastWork = work;
			SubmitThreadpoolWork(work);
		}

		void WaitOutstanding() {
			if (!m_lastWork)
				return;
			WaitForThreadpoolWorkCallbacks(m_lastWork, FALSE);
			CloseThreadpoolWork(m_lastWork);
			CloseThreadpoolCleanupGroupMembers(m_group, true, this);
			m_lastWork = nullptr;
		}
	};
}