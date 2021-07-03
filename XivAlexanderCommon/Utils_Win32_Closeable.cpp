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
	Attach(r.m_object, r.m_bOwnership);
	r.Detach();
	return *this;
}

Utils::Win32::Closeable::Handle& Utils::Win32::Closeable::Handle::operator=(const Handle& r) {
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
	Attach(h, true);
	return *this;
}

Utils::Win32::Closeable::Handle& Utils::Win32::Closeable::Handle::DuplicateFrom(HANDLE hSourceHandle, bool bInheritable) {
	return DuplicateFrom(GetCurrentProcess(), hSourceHandle, bInheritable);
}

Utils::Win32::Closeable::LoadedModule::LoadedModule(const wchar_t* pwszFileName, DWORD dwFlags, bool bRequire)
	: Base<HMODULE, FreeLibrary>(LoadLibraryExW(pwszFileName, nullptr, dwFlags), Null) {
	if (!m_object && bRequire)
		throw Error("LoadLibraryExW");
}

Utils::Win32::Closeable::LoadedModule Utils::Win32::Closeable::LoadedModule::From(HINSTANCE hInstance) {
	return LoadedModule(LoadLibraryW(Process::Current().PathOf(hInstance).c_str()), Null, "LoadLibraryW");
}

void Utils::Win32::Closeable::LoadedModule::FreeAfterRunInNewThread(std::function<DWORD()> f) {
	if (!m_object)
		throw std::runtime_error("Tried to run when no module is selected");

	struct TemporaryObject {
		std::function<DWORD()> f;
		LoadedModule m;
	};

	const auto k = new TemporaryObject();

	try {
		const auto hThread = Handle(CreateThread(nullptr, 0, [](void* pTemp) -> DWORD {
			TemporaryObject pTempObj = std::move(*static_cast<TemporaryObject*>(pTemp));
			delete static_cast<TemporaryObject*>(pTemp);

			const auto exitCode = pTempObj.f();
			pTempObj.f = nullptr;
			if (pTempObj.m.m_bOwnership){
				FreeLibraryAndExitThread(pTempObj.m.m_object, exitCode);
			}
			return exitCode;
			}, k, CREATE_SUSPENDED, nullptr),
			Handle::Null, "CreateThread"
		);
		k->f = std::move(f);
		k->m = std::move(*this);
		ResumeThread(hThread);
	} catch(...) {
		delete k;
		throw;
	}
}
