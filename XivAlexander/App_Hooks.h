#pragma once
namespace App::Hooks {

	using namespace Signatures;

	template<typename R, typename ...Args>
	class Function : public Signature<R(*)(Args...)> {
	protected:
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
			return reinterpret_cast<type>(this->m_pAddress)(std::forward<Args>(args)...);
		}

		R bridge(Args ...args) const {
			return m_pfnBridge(std::forward<Args>(args)...);
		}

		virtual void Setup() {
			Signature<type>::Setup();
		}

		void SetupHook(std::function<std::remove_pointer_t<type>> pfnDetour) {
			m_pfnDetour = pfnDetour;
		}

		virtual void Startup() {
			Signature<type>::Startup();
		}

		virtual void Cleanup() {
			Signature<type>::Cleanup();
		}
	};

	template<typename R, typename ...Args>
	class PointerFunction : public Function<R, Args...> {
		typedef R(*type)(Args...);

	public:
		using Function<R, Args...>::Function;

		virtual void Setup() {
			Function<R, Args...>::Setup();
			void* bridge;
			const auto res = MH_CreateHook(this->m_pAddress, static_cast<void*>(this->m_pfnBinder), &bridge);
			if (res != MH_OK)
				throw std::runtime_error(std::format("SetupHook error for {}: {}", this->m_sName, static_cast<int>(res)));
			this->m_pfnBridge = static_cast<type>(bridge);
		}

		virtual void Startup() {
			Function<R, Args...>::Startup();
			MH_EnableHook(this->m_pAddress);
		}

		virtual void Cleanup() {
			Function<R, Args...>::Cleanup();
			MH_DisableHook(this->m_pAddress);
		}
	};

	const void* FindImportAddressTableItem(const char* szDllName, const char* szFunctionName, uint32_t hintOrOrdinal);

	template<typename R, typename ...Args>
	class ImportedFunction : public Function<R, Args...> {
		typedef R(*type)(Args...);

	public:
		ImportedFunction(const char* szName, const char* szDllName, const char* szFunctionName, uint32_t hintOrOrdinal, type pfnBinder)
			: Function<R, Args...>(szName, [szDllName, szFunctionName, hintOrOrdinal]() {return const_cast<void*>(FindImportAddressTableItem(szDllName, szFunctionName, hintOrOrdinal)); }, pfnBinder) {
		}

		virtual void Setup() {
			Function<R, Args...>::Setup();
			this->m_pfnBridge = reinterpret_cast<type>(*(reinterpret_cast<void**>(this->m_pAddress)));
		}

		virtual void Startup() {
			Function<R, Args...>::Startup();

			MEMORY_BASIC_INFORMATION mbi;
			VirtualQuery(this->m_pAddress, &mbi, sizeof(MEMORY_BASIC_INFORMATION));
			VirtualProtect(mbi.BaseAddress, mbi.RegionSize, PAGE_EXECUTE_READWRITE, &mbi.Protect);
			*(reinterpret_cast<void**>(this->m_pAddress)) = this->m_pfnBinder;
			VirtualProtect(mbi.BaseAddress, mbi.RegionSize, mbi.Protect, &mbi.Protect);
		}

		virtual void Cleanup() {
			Function<R, Args...>::Cleanup();

			MEMORY_BASIC_INFORMATION mbi;
			VirtualQuery(this->m_pAddress, &mbi, sizeof(MEMORY_BASIC_INFORMATION));
			VirtualProtect(mbi.BaseAddress, mbi.RegionSize, PAGE_EXECUTE_READWRITE, &mbi.Protect);
			*(reinterpret_cast<void**>(this->m_pAddress)) = this->m_pfnBridge;
			VirtualProtect(mbi.BaseAddress, mbi.RegionSize, mbi.Protect, &mbi.Protect);
		}
	};

	namespace Socket {
		extern ImportedFunction<SOCKET, int, int, int> socket;
		extern ImportedFunction<int, SOCKET, const sockaddr*, int> connect;
		extern ImportedFunction<int, int, fd_set*, fd_set*, fd_set*, const timeval*> select;
		extern ImportedFunction<int, SOCKET, char*, int, int> recv;
		extern ImportedFunction<int, SOCKET, const char*, int, int> send;
		extern ImportedFunction<int, SOCKET> closesocket;
	};

	namespace WinApi {
#ifdef _DEBUG
		extern PointerFunction<BOOL> IsDebuggerPresent;
#endif
	}
}
