#pragma once

#include "App_Misc_Signatures.h"

namespace App::Misc::Hooks {

	using namespace Signatures;
	
	class Binder {
	public:
#if INTPTR_MAX == INT32_MAX

		static constexpr size_t DummyAddress = 0xFF00FF00U;

#elif INTPTR_MAX == INT64_MAX

		static constexpr size_t DummyAddress = 0xFF00FF00FF00FF00ULL;

#else
#error "Environment not x86 or x64."
#endif

	private:
		static HANDLE s_hHeap;
		static std::mutex s_hHeapMutex;

		void* m_pAddress = nullptr;
		Utils::CallOnDestruction::Multiple m_cleanup;

	public:
		Binder(void* this_, void* templateMethod);
		~Binder();

		template<typename F = void*>
		[[nodiscard]] F GetBinder() const {
			return reinterpret_cast<F>(m_pAddress);
		}
	};

	template<typename R, typename ...Args>
	class Function : public Signature<R(_stdcall *)(Args...)> {
	protected:
		typedef R(__stdcall *FunctionType)(Args...);

		FunctionType m_bridge = nullptr;
		std::function<std::remove_pointer_t<FunctionType>> m_detour = nullptr;

		static R __stdcall DetouredGatewayTemplateFunction(Args...args) {
			const volatile auto target = reinterpret_cast<Function<R, Args...>*>(Binder::DummyAddress);
			return target->DetouredGateway(args...);
		}

		const Binder m_binder{ this, DetouredGatewayTemplateFunction };

		std::shared_ptr<bool> m_destructed = std::make_shared<bool>(false);

	public:
		using Signature<FunctionType>::Signature;
		~Function() override {
			if (m_detour)
				std::abort();
			
			*m_destructed = true;
		}
		
		virtual R bridge(Args ...args) {
			return m_bridge(std::forward<Args>(args)...);
		}

		[[nodiscard]] virtual bool IsDisableable() const {
			return true;
		}

		Utils::CallOnDestruction SetHook(std::function<std::remove_pointer_t<FunctionType>> pfnDetour) {
			if (!pfnDetour)
				throw std::invalid_argument("pfnDetour cannot be null");
			if (m_detour)
				throw std::runtime_error("Cannot add multiple hooks");
			m_detour = std::move(pfnDetour);
			HookEnable();

			return Utils::CallOnDestruction([this, m_destructed=m_destructed]() {
				if (*m_destructed)
					return;
				
				HookDisable();
				m_detour = nullptr;
				});
		}

	protected:
		virtual R DetouredGateway(Args...args) {
			if (m_detour)
				return m_detour(std::forward<Args>(args)...);
			else
				return bridge(std::forward<Args>(args)...);
		}
		
		virtual void HookEnable() = 0;
		virtual void HookDisable() = 0;
	};

	template<typename R, typename ...Args>
	class PointerFunction : public Function<R, Args...> {
		using Function<R, Args...>::FunctionType;

	public:
		PointerFunction(const char* szName, FunctionType pAddress)
			: Function<R, Args...>(szName, pAddress) {
			void* bridge;
			const auto res = MH_CreateHook(this->m_pAddress, this->m_binder.GetBinder(), &bridge);
			if (res != MH_OK)
				throw std::runtime_error(std::format("SetupHook error for {}: {}", this->m_sName, static_cast<int>(res)));
			this->m_bridge = static_cast<FunctionType>(bridge);
		}

	protected:
		void HookEnable() final {
			MH_EnableHook(this->m_pAddress);
		}

		void HookDisable() final {
			MH_DisableHook(this->m_pAddress);
		}
	};

	template<typename R, typename ...Args>
	class ImportedFunction : public Function<R, Args...> {
		using Function<R, Args...>::FunctionType;

	public:
		ImportedFunction(const char* szName, const char* szDllName, const char* szFunctionName, uint32_t hintOrOrdinal = 0)
			: Function<R, Args...>(szName, reinterpret_cast<FunctionType>(Utils::Win32::Process::Current().FindImportedFunction(GetModuleHandleW(nullptr), szDllName, szFunctionName, hintOrOrdinal).first)) {
			this->m_bridge = static_cast<FunctionType>(*reinterpret_cast<void**>(this->m_pAddress));
		}

		[[nodiscard]] bool IsDisableable() const final {
			return *reinterpret_cast<void**>(this->m_pAddress) == this->m_binder.GetBinder()
				|| *reinterpret_cast<void**>(this->m_pAddress) == this->m_bridge;
		}

	protected:
		void HookEnable() final {
			MEMORY_BASIC_INFORMATION mbi;
			VirtualQuery(this->m_pAddress, &mbi, sizeof(MEMORY_BASIC_INFORMATION));
			VirtualProtect(mbi.BaseAddress, mbi.RegionSize, PAGE_EXECUTE_READWRITE, &mbi.Protect);
			*reinterpret_cast<void**>(this->m_pAddress) = this->m_binder.GetBinder();
			VirtualProtect(mbi.BaseAddress, mbi.RegionSize, mbi.Protect, &mbi.Protect);
		}

		void HookDisable() final {
			MEMORY_BASIC_INFORMATION mbi;
			VirtualQuery(this->m_pAddress, &mbi, sizeof(MEMORY_BASIC_INFORMATION));
			VirtualProtect(mbi.BaseAddress, mbi.RegionSize, PAGE_EXECUTE_READWRITE, &mbi.Protect);
			*reinterpret_cast<void**>(this->m_pAddress) = this->m_bridge;
			VirtualProtect(mbi.BaseAddress, mbi.RegionSize, mbi.Protect, &mbi.Protect);
		}
	};
	
	class WndProcFunction : public Function<LRESULT, HWND, UINT, WPARAM, LPARAM> {
		using Super = Function<LRESULT, HWND, UINT, WPARAM, LPARAM>;

		HWND const m_hWnd;
		bool m_windowDestroyed = false;

		LONG_PTR m_prevProc;

	public:
		WndProcFunction(const char* szName, HWND hWnd);
		~WndProcFunction() override;

		[[nodiscard]] bool IsDisableable() const final;
		
		LRESULT bridge(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) final;

	protected:
		void HookEnable() final;
		void HookDisable() final;
	};
}
