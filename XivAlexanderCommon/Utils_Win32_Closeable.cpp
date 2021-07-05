#include "pch.h"
#include "Utils_Win32_Closeable.h"

#include "Utils_Win32_Process.h"

HANDLE Utils::Win32::Closeable::Handle::DuplicateHandleNullable(HANDLE src) {
	if (!src)
		return nullptr;
	if (HANDLE dst; DuplicateHandle(GetCurrentProcess(), src, GetCurrentProcess(), &dst, 0, FALSE, DUPLICATE_SAME_ACCESS))
		return dst;
	throw Error("DuplicateHandle");
}

Utils::Win32::Closeable::Handle::Handle(const Handle& r)
	: Base(r.m_bOwnership ? DuplicateHandleNullable(r.m_object) : r.m_object, r.m_bOwnership) {
}

Utils::Win32::Closeable::Handle& Utils::Win32::Closeable::Handle::operator=(Handle&& r) noexcept {
	if (&r == this)
		return *this;
	
	Clear();
	m_object = r.m_object;
	m_bOwnership = r.m_bOwnership;
	r.Detach();
	return *this;
}

Utils::Win32::Closeable::Handle& Utils::Win32::Closeable::Handle::operator=(const Handle& r) {
	if (&r == this)
		return *this;
	
	const auto newHandle = r.m_bOwnership ? DuplicateHandleNullable(r.m_object) : r.m_object;
	if (m_object)
		CloseHandle(m_object);
	m_object = newHandle;
	m_bOwnership = r.m_bOwnership;
	return *this;
}

Utils::Win32::Closeable::Handle& Utils::Win32::Closeable::Handle::DuplicateFrom(HANDLE hProcess, HANDLE hSourceHandle, bool bInheritable) {
	HANDLE h;
	if (!DuplicateHandle(hProcess, hSourceHandle, GetCurrentProcess(), &h, 0, bInheritable ? TRUE : FALSE, DUPLICATE_SAME_ACCESS))
		throw Error("DuplicateHandle");
	Clear();
	m_object = h;
	m_bOwnership = true;
	return *this;
}

Utils::Win32::Closeable::Handle& Utils::Win32::Closeable::Handle::DuplicateFrom(HANDLE hSourceHandle, bool bInheritable) {
	return DuplicateFrom(GetCurrentProcess(), hSourceHandle, bInheritable);
}

Utils::Win32::Closeable::ActivationContext::ActivationContext(const ACTCTXW& actctx)
	: Base<HANDLE, ReleaseActCtx>(CreateActCtxW(&actctx), INVALID_HANDLE_VALUE, "CreateActCtxW") {
}

Utils::CallOnDestruction Utils::Win32::Closeable::ActivationContext::With() const {
	ULONG_PTR cookie;
	if (!ActivateActCtx(m_object, &cookie))
		throw Error("ActivateActCtx");
	return CallOnDestruction([cookie]() {
		DeactivateActCtx(DEACTIVATE_ACTCTX_FLAG_FORCE_EARLY_DEACTIVATION, cookie);
	});
}

Utils::Win32::Closeable::LoadedModule::LoadedModule(const wchar_t* pwszFileName, DWORD dwFlags, bool bRequire)
	: Base<HMODULE, FreeLibrary>(LoadLibraryExW(pwszFileName, nullptr, dwFlags), Null) {
	if (!m_object && bRequire)
		throw Error("LoadLibraryExW");
}

Utils::Win32::Closeable::LoadedModule::LoadedModule(LoadedModule&& r) noexcept
	: Base(std::move(r)) {
}

Utils::Win32::Closeable::LoadedModule::LoadedModule(const LoadedModule& r)
	: Base(r.m_bOwnership && r.m_object ? LoadLibraryW(Process::Current().PathOf(r.m_object).c_str()) : r.m_object,
		r.m_bOwnership) {
	if (r.m_object && !m_object)
		throw Error("LoadLibraryW");
}

Utils::Win32::Closeable::LoadedModule& Utils::Win32::Closeable::LoadedModule::operator=(LoadedModule&& r) noexcept {
	if (this == &r)
		return *this;

	Clear();
	m_object = r.m_object;
	m_bOwnership = r.m_bOwnership;
	r.Detach();
	return *this;
}

Utils::Win32::Closeable::LoadedModule& Utils::Win32::Closeable::LoadedModule::operator=(const LoadedModule& r) {
	if (!r.m_bOwnership || !r.m_object) {
		Clear();
		m_object = r.m_object;
		m_bOwnership = r.m_bOwnership;
	} else {
		*this = From(r.m_object);
	}
	return *this;
}

Utils::Win32::Closeable::LoadedModule& Utils::Win32::Closeable::LoadedModule::operator=(std::nullptr_t) {
	Clear();
	return *this;
}

Utils::Win32::Closeable::LoadedModule Utils::Win32::Closeable::LoadedModule::From(HINSTANCE hInstance) {
	return LoadedModule(LoadLibraryW(Process::Current().PathOf(hInstance).c_str()), Null, "LoadLibraryW");
}

Utils::Win32::Closeable::Thread::Thread(std::wstring name, std::function<DWORD()> body, LoadedModule hLibraryToFreeAfterExecution) {
	typedef std::function<DWORD(void*)> InnerBodyFunctionType;
	const auto pInnerBodyFunction = new InnerBodyFunctionType(
		[
			name = std::move(name),
			body = std::move(body),
			hLibraryToFreeAfterExecution = std::move(hLibraryToFreeAfterExecution)
		](void* pThisFunction) mutable {
			SetThreadDescription(GetCurrentThread(), name);
			const auto res = body();
			if (hLibraryToFreeAfterExecution.HasOwnership()) {
				const auto hModule = hLibraryToFreeAfterExecution.Detach();
				delete static_cast<InnerBodyFunctionType*>(pThisFunction);
				FreeLibraryAndExitThread(hModule, res);
			}
			
			delete static_cast<InnerBodyFunctionType*>(pThisFunction);
			return res;
		});
	const auto hThread = CreateThread(nullptr, 0, [](void* lpParameter) -> DWORD {
		return (*static_cast<decltype(pInnerBodyFunction)>(lpParameter))(lpParameter);
	}, pInnerBodyFunction, 0, nullptr);
	if (!hThread) {
		const auto err = GetLastError();
		delete pInnerBodyFunction;
		throw Error(err, "CreateThread");
	}
}

Utils::Win32::Closeable::Thread::Thread(std::wstring name, std::function<void()> body, LoadedModule hLibraryToFreeAfterExecution)
	: Thread(std::move(name), std::function([body = std::move(body)]() -> DWORD { body(); return 0; }), std::move(hLibraryToFreeAfterExecution)) {
}

Utils::Win32::Closeable::Thread::~Thread() = default;

Utils::Win32::Closeable::Thread Utils::Win32::Closeable::Thread::WithReference(std::wstring name, std::function<DWORD()> body, HINSTANCE hModule) {
	return Thread(std::move(name), std::move(body), LoadedModule::From(hModule));
}

Utils::Win32::Closeable::Thread Utils::Win32::Closeable::Thread::WithReference(std::wstring name, std::function<void()> body, HINSTANCE hModule) {
	return Thread(std::move(name), std::move(body), LoadedModule::From(hModule));
}

DWORD Utils::Win32::Closeable::Thread::GetId() const {
	return GetThreadId(m_object);
}

void Utils::Win32::Closeable::Thread::Wait() const {
	WaitForSingleObject(m_object, INFINITE);
}

DWORD Utils::Win32::Closeable::Thread::Wait(DWORD duration) const {
	return WaitForSingleObject(m_object, duration);
}
