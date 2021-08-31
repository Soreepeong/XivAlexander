#include "pch.h"
#include "Utils_Win32_LoadedModule.h"
#include "Utils_Win32_Process.h"

Utils::Win32::LoadedModule::LoadedModule(const wchar_t* pwszFileName, DWORD dwFlags, bool bRequire)
	: Closeable<HMODULE, FreeLibrary>(LoadLibraryExW(pwszFileName, nullptr, dwFlags), Null) {
	if (!m_object && bRequire)
		throw Error("LoadLibraryExW");
}

Utils::Win32::LoadedModule::LoadedModule(LoadedModule&& r) noexcept
	: Closeable(std::move(r)) {
}

Utils::Win32::LoadedModule::LoadedModule(const LoadedModule& r)
	: Closeable(r.m_bOwnership&& r.m_object ? LoadLibraryW(Process::Current().PathOf(r.m_object).c_str()) : r.m_object,
		r.m_bOwnership) {
	if (r.m_object && !m_object)
		throw Error("LoadLibraryW");
}

Utils::Win32::LoadedModule& Utils::Win32::LoadedModule::operator=(LoadedModule&& r) noexcept {
	if (this == &r)
		return *this;

	Clear();
	m_object = r.m_object;
	m_bOwnership = r.m_bOwnership;
	r.Detach();
	return *this;
}

Utils::Win32::LoadedModule& Utils::Win32::LoadedModule::operator=(const LoadedModule& r) {
	if (!r.m_bOwnership || !r.m_object) {
		Clear();
		m_object = r.m_object;
		m_bOwnership = r.m_bOwnership;
	} else {
		*this = LoadMore(r);
	}
	return *this;
}

Utils::Win32::LoadedModule& Utils::Win32::LoadedModule::operator=(std::nullptr_t) {
	Clear();
	return *this;
}

Utils::Win32::LoadedModule::~LoadedModule() = default;

Utils::Win32::LoadedModule Utils::Win32::LoadedModule::LoadMore(const LoadedModule & module) {
	return LoadedModule(module.PathOf().c_str(), 0, true);
}

std::filesystem::path Utils::Win32::LoadedModule::PathOf() const {
	return Process::Current().PathOf(*this);
}

void Utils::Win32::LoadedModule::Pin() const {
	const auto pModuleHandleAsPsz = reinterpret_cast<LPCWSTR>(m_object);
	HMODULE dummy;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
		pModuleHandleAsPsz, &dummy))
		throw Error(
			"GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, 0x{:x})",
			reinterpret_cast<size_t>(pModuleHandleAsPsz));
}
