#pragma once

#include <mutex>
#include <set>

namespace Utils {
	class AntiReentry {
		std::mutex m_lock;
		std::set<DWORD> m_tids;

	public:
		class Lock {
			AntiReentry& p;
			bool re = false;

		public:
			explicit Lock(AntiReentry& p)
				: p(p) {
				const auto tid = GetCurrentThreadId();

				std::lock_guard lock(p.m_lock);
				re = p.m_tids.find(tid) != p.m_tids.end();
				if (!re)
					p.m_tids.insert(tid);
			}

			Lock(const Lock&) = delete;
			Lock& operator=(const Lock&) = delete;
			Lock(Lock&& r) = delete;
			Lock& operator=(Lock&&) = delete;

			~Lock() {
				if (!re) {
					std::lock_guard lock(p.m_lock);
					p.m_tids.erase(GetCurrentThreadId());
				}
			}

			operator bool() const {
				// True if new enter
				return !re;
			}
		};
	};
}
