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
		
		virtual R bridge(Args ...args) const {
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

			return Utils::CallOnDestruction([this, destructed=destructed]() {
				if (*destructed)
					return;
				
				OutputDebugStringA(std::format("{}\n", this->m_sName).c_str());
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

	const void* FindImportAddressTableItem(const char* szDllName, const char* szFunctionName, uint32_t hintOrOrdinal);

	template<typename R, typename ...Args>
	class ImportedFunction : public Function<R, Args...> {
		using Function<R, Args...>::FunctionType;

	public:
		ImportedFunction(const char* szName, const char* szDllName, const char* szFunctionName, uint32_t hintOrOrdinal = 0)
			: Function<R, Args...>(szName, reinterpret_cast<FunctionType>(FindImportAddressTableItem(szDllName, szFunctionName, hintOrOrdinal))) {
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
		std::shared_ptr<bool> destructed = std::make_shared<bool>(false);

		LONG_PTR m_prevProc;

	public:
		WndProcFunction(const char* szName, HWND hWnd);
		~WndProcFunction() override;

		[[nodiscard]] bool IsDisableable() const final;
		
		LRESULT bridge(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) const final;

	protected:
		void HookEnable() final;
		void HookDisable() final;
	};
}
