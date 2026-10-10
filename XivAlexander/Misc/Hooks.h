#pragma once

#include <xivres/util.on_dtor.h>
#include "Utils/Win32/HeapAllocator.h"
#include "Utils/Win32/Process.h"
#include "Game/Signatures.h"

namespace XivAlexander::Misc::Hooks {

	using namespace XivAlexander::Game::Signatures;

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
		static std::vector<char, Utils::Win32::HeapAllocator<char>> CreateThunkBody(void* this_, void* templateMethod);

		const std::vector<char, Utils::Win32::HeapAllocator<char>> m_impl;

	public:
		Binder(void* this_, void* templateMethod);
		~Binder();

		template<typename F = void*>
		[[nodiscard]] F GetBinder() const {
			return reinterpret_cast<F>(const_cast<void*>(reinterpret_cast<const void*>(&m_impl[0])));
		}
	};

	template<typename R, typename ...Args>
	class Function : public Signature<R(*)(Args...)> {
	protected:
		typedef R(*FunctionType)(Args...);

		FunctionType m_bridge = nullptr;
		std::function<std::remove_pointer_t<FunctionType>> m_detour = nullptr;

		static R DetouredGatewayTemplateFunction(Args...args) {
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

			// If something is still using the bridge, wait for that to finish
			while (m_hookCounter)
				Sleep(1);

			*m_destructed = true;
		}

		virtual R bridge(Args ...args) {
			return m_bridge(std::forward<Args&>(args)...);
		}

		virtual R operator()(Args...args) const = 0;

		[[nodiscard]] virtual bool IsDisableable() const {
			return true;
		}

		xivres::util::on_dtor SetHook(std::function<std::remove_pointer_t<FunctionType>> pfnDetour) {
			if (!pfnDetour)
				throw std::invalid_argument("pfnDetour cannot be null");
			if (m_detour)
				throw std::runtime_error("Cannot add multiple hooks");
			m_detour = std::move(pfnDetour);
			HookEnable();

			return xivres::util::on_dtor([this, m_destructed = m_destructed] {
				if (*m_destructed)
					return;

				HookDisable();
				m_detour = nullptr;
			});
		}

	protected:
		std::atomic_size_t m_hookCounter{};

		xivres::util::on_dtor AcquireHookCounter() {
			m_hookCounter++;
			return { [this] {
				m_hookCounter--;
			} };
		}

		// Keep it virtual; need to deref from template function accepting instance of this class which is a template class
		virtual R DetouredGateway(Args...args) {
			const auto _hookCounterRelease = AcquireHookCounter();
			if (m_detour)
				return m_detour(std::forward<Args&>(args)...);
			else
				return bridge(std::forward<Args&>(args)...);
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

		~PointerFunction() override {
			// Remove the hook so that the function can be hooked again; the trampoline goes with it, so wait for the
			// calls still in it first.
			MH_DisableHook(this->m_pAddress);
			while (this->m_hookCounter)
				Sleep(1);
			MH_RemoveHook(this->m_pAddress);
		}

		R operator()(Args...args) const override {
			return this->m_pAddress(std::forward<Args&>(args)...);
		}

	protected:
		void HookEnable() final {
			MH_EnableHook(this->m_pAddress);
		}

		void HookDisable() final {
			MH_DisableHook(this->m_pAddress);
		}
	};

	template<typename TFunctionPointer>
	struct PointerFunctionOfImpl;

	template<typename R, typename ...Args>
	struct PointerFunctionOfImpl<R(*)(Args...)> {
		using Type = PointerFunction<R, Args...>;
	};

	/// The hook for a function pointer type, so that its signature need not be spelled out again;
	/// e.g. PointerFunctionOf<decltype(Game::SoundVoiceFunctions::Submit)>.
	template<typename TFunctionPointer>
	using PointerFunctionOf = typename PointerFunctionOfImpl<std::remove_cvref_t<TFunctionPointer>>::Type;

	/// Points one call rel32 instruction elsewhere, through a jump stub allocated within its reach. The displacement is
	/// swapped with one locked store, so a thread running the code calls either the old or the new target.
	class CallSitePatch {
		uint8_t* const m_call;
		const int32_t m_original;
		int32_t m_redirected{};
		uint8_t* m_stub{};  // jmp [rip+2]; int3 x2; the destination, 8 byte aligned

	public:
		/// Gets what a call rel32 instruction calls; throws if it isn't one.
		[[nodiscard]] static void* TargetOf(const void* callInstruction);

		CallSitePatch(void* callInstruction, void* destination);
		CallSitePatch(const CallSitePatch&) = delete;
		CallSitePatch& operator=(const CallSitePatch&) = delete;
		~CallSitePatch();

		/// Whether the call goes to the destination now.
		[[nodiscard]] bool IsRedirected() const;

		/// Whether the call goes to the destination or the original target, i.e. nobody patched it over this.
		[[nodiscard]] bool IsRestorable() const;

		/// Redirects the call, unless something else changed it since it was looked at; returns whether it did.
		bool Redirect();

		/// Restores the call. If something else redirected it over this, it may chain to the stub, which is then pointed at
		/// the original target and kept.
		void Restore();

	private:
		[[nodiscard]] int32_t Current() const;
		bool Exchange(int32_t from, int32_t to);
		void SetStubDestination(const void* destination);
	};

	/// A hook of one call site: only the call rel32 at callInstruction goes to the detour, and the bridge calls whatever it
	/// called before. Other callers of the function, and other hooks of the function itself (which other tools may patch
	/// inline), are left alone.
	template<typename R, typename ...Args>
	class CallSiteFunction : public Function<R, Args...> {
		using Function<R, Args...>::FunctionType;

		CallSitePatch m_patch;

	public:
		CallSiteFunction(const char* szName, void* callInstruction)
			: Function<R, Args...>(szName, reinterpret_cast<FunctionType>(CallSitePatch::TargetOf(callInstruction)))
			, m_patch(callInstruction, this->m_binder.GetBinder()) {
			this->m_bridge = this->m_pAddress;
		}

		R operator()(Args...args) const override {
			return this->m_pAddress(std::forward<Args&>(args)...);
		}

		[[nodiscard]] bool IsDisableable() const final {
			return m_patch.IsRestorable();
		}

		/// Whether the call goes to the detour; after SetHook, false if something else changed the call in between.
		[[nodiscard]] bool IsEnabled() const {
			return m_patch.IsRedirected();
		}

	protected:
		void HookEnable() final {
			m_patch.Redirect();
		}

		void HookDisable() final {
			m_patch.Restore();
		}
	};

	template<typename R, typename ...Args>
	class ImportedFunction : public Function<R, Args...> {
		using Function<R, Args...>::FunctionType;

	public:
		ImportedFunction(const char* szName, const char* szDllName, const char* szFunctionName, uint32_t hintOrOrdinal = 0, HMODULE hModule = nullptr)
			: Function<R, Args...>(szName, reinterpret_cast<FunctionType>(Utils::Win32::Process::Current().FindImportedFunction(hModule ? hModule : GetModuleHandleW(nullptr), szDllName, szFunctionName, hintOrOrdinal).first)) {
			if (this->m_pAddress)
				this->m_bridge = static_cast<FunctionType>(*reinterpret_cast<void**>(this->m_pAddress));
		}

		[[nodiscard]] bool IsDisableable() const final {
			return *reinterpret_cast<void**>(this->m_pAddress) == this->m_binder.GetBinder()
				|| *reinterpret_cast<void**>(this->m_pAddress) == this->m_bridge;
		}

		R operator()(Args...args) const override {
			return (*reinterpret_cast<FunctionType*>(this->m_pAddress))(std::forward<Args&>(args)...);
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

		LRESULT operator()(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) const final {
			return SendMessageW(hwnd, msg, wParam, lParam);
		}

	protected:
		void HookEnable() final;
		void HookDisable() final;
	};
}
