#pragma once

#include "App_Signatures.h"

namespace App::Misc::Hooks {

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

		Utils::CallOnDestruction SetHook(std::function<std::remove_pointer_t<type>> pfnDetour) {
			if (!pfnDetour)
				throw std::invalid_argument("pfnDetour cannot be null");
			if (m_pfnDetour)
				throw std::runtime_error("Cannot add multiple hooks");
			m_pfnDetour = std::move(pfnDetour);
			HookEnable();
			
			return Utils::CallOnDestruction([this]() {
				HookDisable();
				m_pfnDetour = nullptr;
			});
		}

	protected:
		virtual void HookEnable() = 0;
		virtual void HookDisable() = 0;
	};

	template<typename R, typename ...Args>
	class PointerFunction : public Function<R, Args...> {
		typedef R(*type)(Args...);

	public:
		using Function<R, Args...>::Function;

		void Setup() override {
			Function<R, Args...>::Setup();
			void* bridge;
			const auto res = MH_CreateHook(this->m_pAddress, static_cast<void*>(this->m_pfnBinder), &bridge);
			if (res != MH_OK)
				throw std::runtime_error(std::format("SetupHook error for {}: {}", this->m_sName, static_cast<int>(res)));
			this->m_pfnBridge = static_cast<type>(bridge);
		}

		void HookEnable() override {
			MH_EnableHook(this->m_pAddress);
		}

		void HookDisable() override {
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

		void Setup() override {
			Function<R, Args...>::Setup();
			this->m_pfnBridge = reinterpret_cast<type>(*(reinterpret_cast<void**>(this->m_pAddress)));
		}

		void HookEnable() override {
			MEMORY_BASIC_INFORMATION mbi;
			VirtualQuery(this->m_pAddress, &mbi, sizeof(MEMORY_BASIC_INFORMATION));
			VirtualProtect(mbi.BaseAddress, mbi.RegionSize, PAGE_EXECUTE_READWRITE, &mbi.Protect);
			*(reinterpret_cast<void**>(this->m_pAddress)) = this->m_pfnBinder;
			VirtualProtect(mbi.BaseAddress, mbi.RegionSize, mbi.Protect, &mbi.Protect);
		}

		void HookDisable() override {
			MEMORY_BASIC_INFORMATION mbi;
			VirtualQuery(this->m_pAddress, &mbi, sizeof(MEMORY_BASIC_INFORMATION));
			VirtualProtect(mbi.BaseAddress, mbi.RegionSize, PAGE_EXECUTE_READWRITE, &mbi.Protect);
			*(reinterpret_cast<void**>(this->m_pAddress)) = this->m_pfnBridge;
			VirtualProtect(mbi.BaseAddress, mbi.RegionSize, mbi.Protect, &mbi.Protect);
		}
	};
	
	namespace WinApi {
#ifdef _DEBUG
		extern PointerFunction<BOOL> IsDebuggerPresent;
#endif
	}
}
