#include "Base.h"

#include "resource.h"

// Small Win32 helpers.

namespace Loader {
	HMODULE g_module;
	const wchar_t* g_modulePath;
	const wchar_t* g_moduleDirectory;
	const wchar_t* g_originalCommandLine;
}

void* Loader::Alloc(size_t size) {
	const auto p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
	if (!p)
		__fastfail(FAST_FAIL_FATAL_APP_EXIT);
	return p;
}

void Loader::Free(void* p) {
	if (p)
		HeapFree(GetProcessHeap(), 0, p);
}

size_t Loader::Length(const wchar_t* s) {
	size_t n = 0;
	if (s)
		while (s[n])
			++n;
	return n;
}

size_t Loader::Length(const char* s) {
	size_t n = 0;
	if (s)
		while (s[n])
			++n;
	return n;
}

Loader::Str::Str(const wchar_t* s) {
	Append(s);
}

Loader::Str::Str(const wchar_t* s, size_t n) {
	Append(s, n);
}

Loader::Str::Str(const Str& r) {
	Append(r);
}

Loader::Str::Str(Str&& r) noexcept
	: m_p(r.m_p)
	, m_len(r.m_len)
	, m_cap(r.m_cap) {
	r.m_p = nullptr;
	r.m_len = r.m_cap = 0;
}

Loader::Str::~Str() {
	Free(m_p);
}

Loader::Str& Loader::Str::operator=(const Str& r) {
	if (this != &r) {
		Clear();
		Append(r);
	}
	return *this;
}

Loader::Str& Loader::Str::operator=(Str&& r) noexcept {
	if (this != &r) {
		Free(m_p);
		m_p = r.m_p;
		m_len = r.m_len;
		m_cap = r.m_cap;
		r.m_p = nullptr;
		r.m_len = r.m_cap = 0;
	}
	return *this;
}

wchar_t* Loader::Str::Buffer(size_t n) {
	if (n + 1 > m_cap) {
		auto cap = m_cap ? m_cap : 64;
		while (cap < n + 1)
			cap *= 2;
		const auto p = static_cast<wchar_t*>(Alloc(cap * sizeof(wchar_t)));
		if (m_p)
			memcpy(p, m_p, (m_len + 1) * sizeof(wchar_t));
		Free(m_p);
		m_p = p;
		m_cap = cap;
	}
	return m_p;
}

void Loader::Str::Resize(size_t n) {
	Buffer(n)[n] = 0;
	m_len = n;
}

Loader::Str& Loader::Str::Append(const wchar_t* s, size_t n) {
	if (!n && m_p)
		return *this;
	const auto p = Buffer(m_len + n);
	if (n)
		memmove(p + m_len, s, n * sizeof(wchar_t));
	m_len += n;
	p[m_len] = 0;
	return *this;
}

Loader::Str& Loader::Str::AppendDec(uint32_t n) {
	wchar_t buf[16];
	size_t i = sizeof buf / sizeof buf[0];
	do {
		buf[--i] = static_cast<wchar_t>(L'0' + n % 10);
		n /= 10;
	} while (n);
	return Append(buf + i, sizeof buf / sizeof buf[0] - i);
}

Loader::Str& Loader::Str::AppendHex(uintptr_t n) {
	wchar_t buf[2 + sizeof n * 2];
	size_t i = sizeof buf / sizeof buf[0];
	do {
		buf[--i] = L"0123456789abcdef"[n & 15];
		n >>= 4;
	} while (n);
	buf[--i] = L'x';
	buf[--i] = L'0';
	return Append(buf + i, sizeof buf / sizeof buf[0] - i);
}

Loader::Str Loader::FormatArgs(const wchar_t* fmt, const wchar_t* const* args, size_t count) {
	Str res;
	size_t next = 0;
	for (auto p = fmt; *p; ++p) {
		if (p[0] == L'{' && p[1] == L'}') {
			if (next < count)
				res.Append(args[next++]);
			++p;
		} else {
			res.Append(*p);
		}
	}
	return res;
}

void Loader::DebugPrintText(const Str& s) {
	auto line = Str(L"XivAlexanderLoader: ");
	line.Append(s).Append(L'\n');
	OutputDebugStringW(line.c_str());
}

Loader::Str Loader::ErrorText(const wchar_t* context, DWORD code) {
	auto res = Str(context);
	res.Append(L": ").AppendDec(code);
	wchar_t* message = nullptr;
	if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, code, 0, reinterpret_cast<LPWSTR>(&message), 0, nullptr) && message) {
		auto len = Length(message);
		while (len && (message[len - 1] == L'\r' || message[len - 1] == L'\n' || message[len - 1] == L' '))
			--len;
		res.Append(L" (").Append(message, len).Append(L')');
		LocalFree(message);
	}
	return res;
}

Loader::Str Loader::LastErrorText(const wchar_t* context) {
	return ErrorText(context, GetLastError());
}

bool Loader::IEquals(const wchar_t* a, const wchar_t* b) {
	return CompareStringOrdinal(a, -1, b, -1, TRUE) == CSTR_EQUAL;
}

const wchar_t* Loader::FileNameOf(const wchar_t* path) {
	auto name = path;
	for (auto p = path; *p; ++p) {
		if (*p == L'\\' || *p == L'/')
			name = p + 1;
	}
	return name;
}

Loader::Str Loader::DirectoryOf(const wchar_t* path) {
	const auto name = FileNameOf(path);
	auto len = static_cast<size_t>(name - path);
	// Keeps "C:\" and "\\" whole, as std::filesystem::path::parent_path does.
	if (len > 1 && !(len == 3 && path[1] == L':'))
		--len;
	return {path, len};
}

Loader::Str Loader::Join(const Str& dir, const wchar_t* name) {
	auto res = dir;
	if (!res.empty() && res[res.size() - 1] != L'\\' && res[res.size() - 1] != L'/')
		res.Append(L'\\');
	return res.Append(name);
}

bool Loader::Exists(const wchar_t* path) {
	return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

bool Loader::SameFile(const wchar_t* a, const wchar_t* b) {
	BY_HANDLE_FILE_INFORMATION info[2]{};
	const wchar_t* paths[2]{a, b};
	for (int i = 0; i < 2; ++i) {
		const auto h = CreateFileW(paths[i], 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (h == INVALID_HANDLE_VALUE)
			return false;
		const auto ok = GetFileInformationByHandle(h, &info[i]);
		CloseHandle(h);
		if (!ok)
			return false;
	}
	return info[0].dwVolumeSerialNumber == info[1].dwVolumeSerialNumber
		&& info[0].nFileIndexHigh == info[1].nFileIndexHigh
		&& info[0].nFileIndexLow == info[1].nFileIndexLow;
}

bool Loader::IsAbsolutePath(const wchar_t* path) {
	const auto sep = [](wchar_t c) { return c == L'\\' || c == L'/'; };
	if (sep(path[0]) && sep(path[1]))
		return true;
	return ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) && path[1] == L':' && sep(path[2]);
}

bool Loader::GetFullPath(const wchar_t* path, Str& out) {
	for (DWORD size = MAX_PATH;;) {
		const auto len = GetFullPathNameW(path, size, out.Buffer(size), nullptr);
		if (!len) {
			out.Clear();
			return false;
		}
		if (len < size) {
			out.Resize(len);
			return true;
		}
		size = len + 1;
	}
}

Loader::Str Loader::TranslatePath(const wchar_t* path, const wchar_t* relativeTo) {
	if (!*path)
		return {};
	Str expanded;
	for (DWORD size = MAX_PATH;;) {
		const auto len = ExpandEnvironmentStringsW(path, expanded.Buffer(size), size);
		if (!len) {
			expanded = path;
			break;
		}
		if (len <= size) {
			expanded.Resize(len - 1);
			break;
		}
		size = len;
	}
	auto combined = IsAbsolutePath(expanded.c_str()) ? expanded : Join(relativeTo, expanded.c_str());
	Str full;
	return GetFullPath(combined.c_str(), full) ? full : combined;
}

Loader::Str Loader::FromUtf8(const char* s, size_t n) {
	Str res;
	if (!n)
		return res;
	const auto len = MultiByteToWideChar(CP_UTF8, 0, s, static_cast<int>(n), nullptr, 0);
	if (len > 0) {
		MultiByteToWideChar(CP_UTF8, 0, s, static_cast<int>(n), res.Buffer(len), len);
		res.Resize(len);
	}
	return res;
}

Loader::Str Loader::ModulePath(HMODULE hModule) {
	Str res;
	for (DWORD size = MAX_PATH; size < 65536; size *= 2) {
		const auto len = GetModuleFileNameW(hModule, res.Buffer(size), size);
		if (len && len < size) {
			res.Resize(len);
			return res;
		}
		if (!len)
			break;
	}
	res.Clear();
	return res;
}

Loader::Str Loader::ProcessImagePath(HANDLE hProcess, DWORD flags) {
	Str res;
	for (DWORD size = MAX_PATH; size < 65536; size *= 2) {
		auto len = size;
		if (QueryFullProcessImageNameW(hProcess, flags, res.Buffer(size), &len)) {
			res.Resize(len);
			return res;
		}
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
			break;
	}
	res.Clear();
	return res;
}

Loader::Str Loader::SystemDirectory() {
	Str res;
	const auto len = GetSystemDirectoryW(res.Buffer(MAX_PATH), MAX_PATH);
	res.Resize(len < MAX_PATH ? len : 0);
	return res;
}

Loader::Str Loader::KnownFolder(const GUID& id) {
	using SHGetKnownFolderPathFn = HRESULT(WINAPI*)(const GUID&, DWORD, HANDLE, PWSTR*);
	using CoTaskMemFreeFn = void(WINAPI*)(void*);
	Str res;
	const auto hShell32 = LoadLibraryW(L"shell32.dll");
	const auto hOle32 = LoadLibraryW(L"ole32.dll");
	const auto pGet = hShell32 ? reinterpret_cast<SHGetKnownFolderPathFn>(GetProcAddress(hShell32, "SHGetKnownFolderPath")) : nullptr;
	const auto pFree = hOle32 ? reinterpret_cast<CoTaskMemFreeFn>(GetProcAddress(hOle32, "CoTaskMemFree")) : nullptr;
	PWSTR psz = nullptr;
	if (pGet && pGet(id, 0x00008000 /* KF_FLAG_CREATE */ | 0x00000800 /* KF_FLAG_INIT */, nullptr, &psz) == S_OK && psz)
		res = psz;
	if (psz && pFree)
		pFree(psz);
	return res;
}

bool Loader::ReadWholeFile(const wchar_t* path, Bytes& out, size_t maxSize) {
	const auto h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	auto ok = false;
	LARGE_INTEGER size{};
	if (GetFileSizeEx(h, &size) && size.HighPart == 0 && size.LowPart <= maxSize) {
		out.Data = static_cast<char*>(Alloc(size.LowPart + 1));
		out.Size = size.LowPart;
		DWORD read = 0;
		ok = ReadFile(h, out.Data, size.LowPart, &read, nullptr) && read == size.LowPart;
	}
	CloseHandle(h);
	return ok;
}

bool Loader::IsProcess64(HANDLE hProcess) {
	BOOL wow64 = FALSE;
	if (IsWow64Process(hProcess, &wow64) && wow64)
		return false;
	SYSTEM_INFO si;
	GetNativeSystemInfo(&si);
	return si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64;
}

bool Loader::IsEnvironmentVariableSet(const wchar_t* name) {
	SetLastError(ERROR_SUCCESS);
	GetEnvironmentVariableW(name, nullptr, 0);
	return GetLastError() != ERROR_ENVVAR_NOT_FOUND;
}

Loader::Str Loader::QuoteArgument(const wchar_t* arg) {
	auto needsQuotes = !*arg;
	for (auto p = arg; *p && !needsQuotes; ++p)
		needsQuotes = *p == L' ' || *p == L'\t' || *p == L'\n' || *p == L'\v' || *p == L'"';
	if (!needsQuotes)
		return arg;

	auto res = Str(L"\"");
	for (auto p = arg;; ++p) {
		size_t backslashes = 0;
		while (*p == L'\\') {
			++p;
			++backslashes;
		}
		if (!*p) {
			for (size_t i = 0; i < backslashes * 2; ++i)
				res.Append(L'\\');
			break;
		}
		const auto count = *p == L'"' ? backslashes * 2 + 1 : backslashes;
		for (size_t i = 0; i < count; ++i)
			res.Append(L'\\');
		res.Append(*p);
	}
	return res.Append(L'"');
}

bool Loader::IsXivAlexanderDll(const wchar_t* path) {
	DWORD handle = 0;
	const auto size = GetFileVersionInfoSizeW(path, &handle);
	if (!size)
		return false;
	const auto block = Alloc(size);
	auto result = false;
	struct LangAndCodePage {
		WORD Language;
		WORD CodePage;
	}* translations = nullptr;
	UINT cbTranslations = 0;
	if (GetFileVersionInfoW(path, 0, size, block)
		&& VerQueryValueW(block, L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &cbTranslations)) {
		for (UINT i = 0; !result && i < cbTranslations / sizeof *translations; ++i) {
			auto query = Str(L"\\StringFileInfo\\");
			const uint32_t langCp = static_cast<uint32_t>(translations[i].Language) << 16 | translations[i].CodePage;
			for (int shift = 28; shift >= 0; shift -= 4)
				query.Append(L"0123456789abcdef"[langCp >> shift & 15]);
			query.Append(L"\\FileDescription");
			wchar_t* value = nullptr;
			UINT cch = 0;
			if (!VerQueryValueW(block, query.c_str(), reinterpret_cast<void**>(&value), &cch) || !value)
				continue;
			const auto text = Str(value, Length(value));
			result = text.size() && (lstrcmpW(text.c_str(), L"XivAlexander Loader DLL") == 0 || lstrcmpW(text.c_str(), L"XivAlexander Main DLL") == 0);
		}
	}
	Free(block);
	return result;
}

void Loader::PinModule(HMODULE hModule) {
	HMODULE h{};
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(hModule), &h);
}

HWND Loader::FindGameMainWindow() {
	HWND hwnd = nullptr;
	while ((hwnd = FindWindowExW(nullptr, hwnd, L"FFXIVGAME", nullptr))) {
		DWORD pid;
		GetWindowThreadProcessId(hwnd, &pid);
		if (pid == GetCurrentProcessId())
			break;
	}
	return hwnd;
}

const wchar_t* Loader::LoadStr(UINT id) {
	const wchar_t* p = nullptr;
	// Loader.rc is compiled with null-terminated strings.
	if (LoadStringW(g_module, id, reinterpret_cast<LPWSTR>(&p), 0) > 0 && p)
		return p;
	return L"";
}

Loader::Str Loader::ButtonText(int id) {
	using MB_GetStringFn = LPCWSTR(WINAPI*)(int);
	const auto hUser32 = GetModuleHandleW(L"user32.dll");
	const auto pfn = hUser32 ? reinterpret_cast<MB_GetStringFn>(GetProcAddress(hUser32, "MB_GetString")) : nullptr;
	Str res;
	if (const auto text = pfn ? pfn(id - 1) : nullptr) {
		for (auto p = text; *p; ++p) {
			if (*p != L'&')
				res.Append(*p);
		}
	}
	return res;
}

int Loader::ShowMessageBox(HWND hwnd, const wchar_t* text, UINT type) {
	auto title = Str(LoadStr(IDS_APP_NAME));
	if (const auto hRes = FindResourceW(g_module, MAKEINTRESOURCEW(VS_VERSION_INFO), RT_VERSION)) {
		const auto p = static_cast<const uint8_t*>(LockResource(LoadResource(g_module, hRes)));
		const auto size = SizeofResource(g_module, hRes);
		for (DWORD i = 0; p && i + sizeof(VS_FIXEDFILEINFO) <= size; i += 4) {
			const auto& info = *reinterpret_cast<const VS_FIXEDFILEINFO*>(p + i);
			if (info.dwSignature != 0xFEEF04BD)
				continue;
			title.Append(L' ').AppendDec(HIWORD(info.dwProductVersionMS)).Append(L'.').AppendDec(LOWORD(info.dwProductVersionMS))
				.Append(L'.').AppendDec(HIWORD(info.dwProductVersionLS)).Append(L'.').AppendDec(LOWORD(info.dwProductVersionLS));
			break;
		}
	}

	ACTCTXW actCtx{
		.cbSize = static_cast<ULONG>(sizeof actCtx),
		.dwFlags = ACTCTX_FLAG_HMODULE_VALID | ACTCTX_FLAG_RESOURCE_NAME_VALID,
		.lpResourceName = MAKEINTRESOURCEW(IDR_RT_MANIFEST_LATE_ACTIVATION),
		.hModule = g_module,
	};
	const auto hActCtx = CreateActCtxW(&actCtx);
	ULONG_PTR cookie = 0;
	const auto activated = hActCtx != INVALID_HANDLE_VALUE && ActivateActCtx(hActCtx, &cookie);
	const auto result = MessageBoxW(hwnd, text, title.c_str(), type);
	if (activated)
		DeactivateActCtx(0, cookie);
	if (hActCtx != INVALID_HANDLE_VALUE)
		ReleaseActCtx(hActCtx);
	return result;
}

void Loader::OpenUrl(const wchar_t* url) {
	using ShellExecuteWFn = HINSTANCE(WINAPI*)(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, INT);
	const auto hShell32 = LoadLibraryW(L"shell32.dll");
	const auto pfn = hShell32 ? reinterpret_cast<ShellExecuteWFn>(GetProcAddress(hShell32, "ShellExecuteW")) : nullptr;
	if (!pfn || reinterpret_cast<INT_PTR>(pfn(nullptr, nullptr, url, nullptr, nullptr, SW_SHOW)) <= 32)
		ShowMessageBox(nullptr, Format(LoadStr(IDS_ERROR_UNEXPECTED), LastErrorText(L"ShellExecuteW")).c_str(), MB_OK | MB_ICONERROR);
}
