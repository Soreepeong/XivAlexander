#pragma once
namespace App::Hooks {

	using namespace App::Signatures;

	template<typename R, typename ...Args>
	class Function : public Signature<R(*)(Args...)> {
		typedef R(*type)(Args...);

		const type m_pfnBinder;

		type m_pfnBridge = nullptr;
		std::function<std::remove_pointer_t<type>> m_pfnDetour = nullptr;

	public:
		Function(const char* szName, void* pAddress, type pfnBinder)
			: Signature<type>(szName, pAddress)
			, m_pfnBinder(pfnBinder) {
		}

		Function(const char* szName, std::function<void* ()> fnResolver, type pfnBinder)
			: Signature<type>(szName, fnResolver)
			, m_pfnBinder(pfnBinder) {
		}

		Function(const char* szName, SectionFilter sectionFilter, const char* sPattern, const char* szMask, type pfnBinder)
			: Signature<type>(szName, sectionFilter, sPattern, szMask)
			, m_pfnBinder(pfnBinder) {
		}

		Function(const char* szName, SectionFilter sectionFilter, const char* sPattern, const char* szMask, std::vector<size_t> nextOffsets, type pfnBinder)
			: Signature<type>(szName, sectionFilter, sPattern, szMask, nextOffsets)
			, m_pfnBinder(pfnBinder) {
		}

		R Thunked(Args...args) const {
			if (m_pfnDetour)
				return m_pfnDetour(std::forward<Args>(args)...);
			else
				return m_pfnBridge(std::forward<Args>(args)...);
		}

		R operator()(Args ...args) const {
			return reinterpret_cast<type>(this->operator void* ())(std::forward<Args>(args)...);
		}

		R bridge(Args ...args) const {
			return m_pfnBridge(std::forward<Args>(args)...);
		}

		void Setup() {
			Signature<type>::Setup();
			void* bridge;
			const auto res = MH_CreateHook(this->operator void* (), static_cast<void*>(m_pfnBinder), &bridge);
			if (res != MH_OK)
				throw std::exception(Utils::FormatString("SetupHook error for %s: %d", this->m_sName.c_str(), res).c_str());
			m_pfnBridge = static_cast<type>(bridge);
		}

		void SetupHook(std::function<std::remove_pointer_t<type>> pfnDetour) {
			MH_DisableHook(this->operator void* ());
			m_pfnDetour = pfnDetour;
			MH_EnableHook(this->operator void* ());
		}
	};

	namespace Socket {
		extern Function<SOCKET, int, int, int> socket;
		extern Function<int, SOCKET, const sockaddr*, int> connect;
		extern Function<int, int, fd_set*, fd_set*, fd_set*, const timeval*> select;
		extern Function<int, SOCKET, char*, int, int> recv;
		extern Function<int, SOCKET, const char*, int, int> send;
		extern Function<int, SOCKET> closesocket;
	};

	namespace WinApi {
		extern Function<BOOL> IsDebuggerPresent;
	}
}
