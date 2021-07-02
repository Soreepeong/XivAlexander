#include "pch.h"
#include "Utils_Win32_Closeable.h"

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
