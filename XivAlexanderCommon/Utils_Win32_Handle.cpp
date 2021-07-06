#include "pch.h"
#include "Utils_Win32_Handle.h"

HANDLE Utils::Win32::Handle::DuplicateHandleNullable(HANDLE src) {
	if (!src)
		return nullptr;
	DWORD flags;
	if (!GetHandleInformation(src, &flags))
		throw Error("GetHandleInformation");
	const auto bInherit = (flags & HANDLE_FLAG_INHERIT) ? TRUE : FALSE;
	if (HANDLE dst; DuplicateHandle(GetCurrentProcess(), src, GetCurrentProcess(), &dst, 0, bInherit, DUPLICATE_SAME_ACCESS))
		return dst;
	throw Error("DuplicateHandle");
}

Utils::Win32::Handle::Handle(Handle&& r) noexcept
	: Closeable(std::move(r)) {
}

Utils::Win32::Handle::Handle(const Handle& r)
	: Closeable(r.m_bOwnership ? DuplicateHandleNullable(r.m_object) : r.m_object, r.m_bOwnership) {
}

Utils::Win32::Handle& Utils::Win32::Handle::operator=(Handle&& r) noexcept {
	if (&r == this)
		return *this;
	
	Clear();
	m_object = r.m_object;
	m_bOwnership = r.m_bOwnership;
	r.Detach();
	return *this;
}

Utils::Win32::Handle& Utils::Win32::Handle::operator=(const Handle& r) {
	if (&r == this)
		return *this;
	
	const auto newHandle = r.m_bOwnership ? DuplicateHandleNullable(r.m_object) : r.m_object;
	if (m_object)
		CloseHandle(m_object);
	m_object = newHandle;
	m_bOwnership = r.m_bOwnership;
	return *this;
}

Utils::Win32::Handle::~Handle() = default;

Utils::Win32::ActivationContext::ActivationContext(const ACTCTXW& actctx)
	: Closeable<HANDLE, ReleaseActCtx>(CreateActCtxW(&actctx), INVALID_HANDLE_VALUE, "CreateActCtxW") {
}

Utils::Win32::ActivationContext::~ActivationContext() = default;

Utils::CallOnDestruction Utils::Win32::ActivationContext::With() const {
	ULONG_PTR cookie;
	if (!ActivateActCtx(m_object, &cookie))
		throw Error("ActivateActCtx");
	return CallOnDestruction([cookie]() {
		DeactivateActCtx(DEACTIVATE_ACTCTX_FLAG_FORCE_EARLY_DEACTIVATION, cookie);
	});
}

Utils::Win32::Thread::Thread()
	: Handle() {
}

Utils::Win32::Thread::Thread(std::wstring name, std::function<DWORD()> body, LoadedModule hLibraryToFreeAfterExecution) {
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
	m_bOwnership = true;
	m_object = hThread;
}

Utils::Win32::Thread::Thread(std::wstring name, std::function<void()> body, LoadedModule hLibraryToFreeAfterExecution)
	: Thread(std::move(name), std::function([body = std::move(body)]() -> DWORD { body(); return 0; }), std::move(hLibraryToFreeAfterExecution)) {
}

Utils::Win32::Thread::Thread(Thread&& r) noexcept
	: Handle(r.m_object, r.m_bOwnership) {
	r.m_object = nullptr;
	r.m_bOwnership = false;
}

Utils::Win32::Thread::Thread(const Thread& r)
	: Handle(r) {
}

Utils::Win32::Thread& Utils::Win32::Thread::operator=(Thread&& r) noexcept {
	if (&r == this)
		return *this;

	m_object = r.m_object;
	m_bOwnership = r.m_bOwnership;
	r.Detach();
	return *this;
}

Utils::Win32::Thread& Utils::Win32::Thread::operator=(const Thread & r) {
	if (&r == this)
		return *this;

	Handle::operator=(r);
	return *this;
}

Utils::Win32::Thread::~Thread() = default;

DWORD Utils::Win32::Thread::GetId() const {
	return GetThreadId(m_object);
}

void Utils::Win32::Thread::Terminate(DWORD dwExitCode, bool errorIfAlreadyTerminated) const {
	if (TerminateThread(m_object, dwExitCode))
		return;
	const auto err = GetLastError();
	if (!errorIfAlreadyTerminated && Wait(0) != WAIT_TIMEOUT)
		return;
	throw Error(err, "TerminateThread");
}

Utils::Win32::Event::~Event() = default;

void Utils::Win32::Handle::Wait() const {
	WaitForSingleObject(m_object, INFINITE);
}

DWORD Utils::Win32::Handle::Wait(DWORD duration) const {
	return WaitForSingleObject(m_object, duration);
}

Utils::Win32::ActivationContext::ActivationContext(ActivationContext&& r) noexcept
	: Closeable(std::move(r)) {
}

Utils::Win32::ActivationContext& Utils::Win32::ActivationContext::operator=(ActivationContext&& r) noexcept {
	Closeable<HANDLE, ReleaseActCtx>::operator=(std::move(r));
	return *this;
}

Utils::Win32::Event Utils::Win32::Event::Create(
	_In_opt_ LPSECURITY_ATTRIBUTES lpEventAttributes,
	_In_ BOOL bManualReset,
	_In_ BOOL bInitialState,
	_In_opt_ LPCWSTR lpName
) {
	return Event(CreateEventW(lpEventAttributes, bManualReset, bInitialState, lpName), INVALID_HANDLE_VALUE, "CreateEventW");
}

void Utils::Win32::Event::Set() const {
	if (!SetEvent(m_object))
		throw Error("SetEvent");
}

void Utils::Win32::Event::Reset() const {
	if (!ResetEvent(m_object))
		throw Error("ResetEvent");
}
