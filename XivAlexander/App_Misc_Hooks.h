#pragma once

#include "App_Misc_Signatures.h"

namespace App::Misc::Hooks {

	using namespace Signatures;

	class Binder {
	public:
		static constexpr auto DummyAddress = 0xFF00FF00FF00FF00ULL;

	private:
		static HANDLE s_hHeap;

		void* m_pAddress = nullptr;
		Utils::CallOnDestruction::Multiple m_cleanup;

	public:
		Binder() = default;
		Binder(void* this_, void* templateMethod);
		~Binder();

		template<typename F = void*>
		[[nodiscard]] F GetBinder() const {
			return reinterpret_cast<F>(m_pAddress);
		}
	};

	template<typename R, typename ...Args>
	class Function : public Signature<R(*)(Args...)> {
	protected:
		typedef R(*FunctionType)(Args...);
		typedef R(*BinderType_)(FunctionType, Args...);

		FunctionType m_bridge = nullptr;
		std::function<std::remove_pointer_t<FunctionType>> m_detour = nullptr;

		static R DetouredGatewayTemplateFunction(Args...args) {
			const volatile auto target = reinterpret_cast<Function<R, Args...>*>(Binder::DummyAddress);
			return target->DetouredGateway(args...);
		}

		const Binder m_binder{ this, DetouredGatewayTemplateFunction };

		std::shared_ptr<bool> destructed = std::make_shared<bool>(false);

	public:
		using Signature<FunctionType>::Signature;
		~Function() override {
			if (m_detour)
				std::abort();
			
			*destructed = true;
		}

		/*
		R operator()(Args ...args) const {
			return reinterpret_cast<FunctionType>(this->m_pAddress)(std::forward<Args>(args)...);
		}//*/

		R bridge(Args ...args) const {
			return m_bridge(std::forward<Args>(args)...);
		}

		[[nodiscard]] virtual bool IsUnloadable() const {
			return !m_detour;
		}

		Utils::CallOnDestruction SetHook(std::function<std::remove_pointer_t<FunctionType>> pfnDetour) {
			if (!pfnDetour)
				throw std::invalid_argument("pfnDetour cannot be null");
			if (m_detour)
				throw std::runtime_error("Cannot add multiple hooks");
			m_detour = std::move(pfnDetour);
			HookEnable();

			return Utils::CallOnDestruction([this, destructed=destructed]() {
				if (destructed)
					return;
				
				HookDisable();
				m_detour = nullptr;
				});
		}

	protected:
		R DetouredGateway(Args...args) const {
			if (m_detour)
				return m_detour(std::forward<Args>(args)...);
			else
				return m_bridge(std::forward<Args>(args)...);
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
		using Function<R, Args...>::FunctionType;

	public:
		ImportedFunction(const char* szName, const char* szDllName, const char* szFunctionName, uint32_t hintOrOrdinal = 0)
			: Function<R, Args...>(szName, reinterpret_cast<FunctionType>(FindImportAddressTableItem(szDllName, szFunctionName, hintOrOrdinal))) {
			this->m_bridge = static_cast<FunctionType>(*reinterpret_cast<void**>(this->m_pAddress));
		}

	protected:
		void HookEnable() override {
			MEMORY_BASIC_INFORMATION mbi;
			VirtualQuery(this->m_pAddress, &mbi, sizeof(MEMORY_BASIC_INFORMATION));
			VirtualProtect(mbi.BaseAddress, mbi.RegionSize, PAGE_EXECUTE_READWRITE, &mbi.Protect);
			*reinterpret_cast<void**>(this->m_pAddress) = this->m_binder.GetBinder();
			VirtualProtect(mbi.BaseAddress, mbi.RegionSize, mbi.Protect, &mbi.Protect);
		}

		void HookDisable() override {
			MEMORY_BASIC_INFORMATION mbi;
			VirtualQuery(this->m_pAddress, &mbi, sizeof(MEMORY_BASIC_INFORMATION));
			VirtualProtect(mbi.BaseAddress, mbi.RegionSize, PAGE_EXECUTE_READWRITE, &mbi.Protect);
			*reinterpret_cast<void**>(this->m_pAddress) = this->m_binder.GetBinder();
			VirtualProtect(mbi.BaseAddress, mbi.RegionSize, mbi.Protect, &mbi.Protect);
		}
	};

	class WndProcFunction : public Function<LRESULT, HWND, UINT, WPARAM, LPARAM> {
		using Super = Function<LRESULT, HWND, UINT, WPARAM, LPARAM>;
		HWND const m_hWnd;

	public:
		WndProcFunction(const char* szName, HWND hWnd)
			: Super(szName, reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hWnd, GWLP_WNDPROC)))
			, m_hWnd(hWnd) {
			m_bridge = reinterpret_cast<decltype(m_bridge)>(SetWindowLongPtrW(m_hWnd, GWLP_WNDPROC, m_binder.GetBinder<LONG_PTR>()));
		}

		~WndProcFunction() override {
			SetWindowLongPtrW(m_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_bridge));
		}

		[[nodiscard]] bool IsUnloadable() const override {
			return GetWindowLongPtrW(m_hWnd, GWLP_WNDPROC) == m_binder.GetBinder<LONG_PTR>();
		}

	protected:
		void HookEnable() override {}
		void HookDisable() override {}
	};
}
