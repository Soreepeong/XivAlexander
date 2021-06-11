#include "pch.h"
#include "include/Misc.h"
#include "include/WinPath.h"

#include <system_error>

Utils::WinPath::WinPath(const std::string& path)
	: m_empty(false)
	, m_wbuf(FromUtf8(path))
	, m_sbuf(std::make_unique<std::string>(path)) {
}

Utils::WinPath::WinPath(std::wstring path)
	: m_empty(false)
	, m_wbuf(std::move(path))
	, m_sbuf(nullptr) {
}

Utils::WinPath::WinPath(const WinPath& r)
	: WinPath(r.m_wbuf) {
}

Utils::WinPath::WinPath(WinPath&& r) noexcept
	: m_empty(r.m_empty)
	, m_wbuf(std::move(r.m_wbuf))
	, m_sbuf(std::move(r.m_sbuf)) {
	r.m_empty = true;
}

Utils::WinPath& Utils::WinPath::operator=(const WinPath& r) {
	m_empty = r.m_empty;
	m_wbuf = r.m_wbuf;
	m_sbuf = nullptr;
	return *this;
}

Utils::WinPath& Utils::WinPath::operator=(WinPath&& r) noexcept {
	m_empty = r.m_empty;
	m_wbuf = std::move(r.m_wbuf);
	m_sbuf = std::move(r.m_sbuf);
	r.m_empty = true;
	return *this;
}

Utils::WinPath::WinPath(HMODULE hModule, HANDLE hProcess)
	: m_empty(false) {
	
	if (hProcess == INVALID_HANDLE_VALUE)
		hProcess = GetCurrentProcess();

	m_wbuf.resize(PATHCCH_MAX_CCH);

	DWORD length;
	if (hProcess == GetCurrentProcess())
		length = GetModuleFileNameW(hModule, &m_wbuf[0], static_cast<DWORD>(m_wbuf.size()));
	else if (!hModule) {
		length = static_cast<DWORD>(m_wbuf.size());
		if (!QueryFullProcessImageNameW(hProcess, 0, &m_wbuf[0], &length))
			length = 0;
	} else
		length = GetModuleFileNameExW(hProcess, hModule, &m_wbuf[0], static_cast<DWORD>(m_wbuf.size()));
	if (!length)
		throw WindowsError("Failed to get module name.");
	m_wbuf.resize(length);
}

Utils::WinPath::~WinPath() = default;

Utils::WinPath& Utils::WinPath::RemoveComponentInplace(size_t numComponents) {
	m_sbuf = nullptr;
	while (numComponents--) {
		const auto result = PathCchRemoveFileSpec(&m_wbuf[0], m_wbuf.size());
		if (FAILED(result))
			throw WindowsError(result, "RemoveComponentInPlace");
	}
	m_wbuf.resize(wcsnlen(&m_wbuf[0], m_wbuf.size()));
	return *this;
}

Utils::WinPath& Utils::WinPath::AddComponentInplace(const char* component) {
	return AddComponentInplace(FromUtf8(component).c_str());
}

Utils::WinPath& Utils::WinPath::AddComponentInplace(const wchar_t* component) {
	m_sbuf = nullptr;
	m_wbuf.resize(PATHCCH_MAX_CCH);
	const auto result = PathCchAppendEx(&m_wbuf[0], m_wbuf.size(), component, PATHCCH_ALLOW_LONG_PATHS);
	if (FAILED(result))
		throw WindowsError(result, "AddComponentInPlace");
	m_wbuf.resize(wcsnlen(&m_wbuf[0], m_wbuf.size()));
	return *this;
}

bool Utils::WinPath::operator==(const WinPath& rhs) const {
	return m_wbuf == rhs.m_wbuf;
}

bool Utils::WinPath::Exists() const {
	return PathFileExistsW(m_wbuf.c_str());
}

bool Utils::WinPath::IsDirectory() const {
	return PathIsDirectoryW(m_wbuf.c_str());
}

const std::wstring& Utils::WinPath::wstr() const {
	return m_wbuf;
}

const std::string& Utils::WinPath::str() const {
	if (!m_sbuf)
		m_sbuf = std::make_unique<std::string>(ToUtf8(m_wbuf));
	return *m_sbuf;
}

const wchar_t* Utils::WinPath::wbuf() const {
	return &wstr()[0];
}

const char* Utils::WinPath::buf() const {
	return &str()[0];
}

Utils::WinPath::operator const std::wstring& () const {
	return wstr();
}

Utils::WinPath::operator const std::string& () const {
	return str();
}

Utils::WinPath::operator const wchar_t* () const {
	return static_cast<const std::wstring&>(*this).c_str();
}

Utils::WinPath::operator const char* () const {
	return static_cast<const std::string&>(*this).c_str();
}
