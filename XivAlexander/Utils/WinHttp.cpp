#include "pch.h"
#include "Utils/WinHttp.h"

#include <XivAlexanderCommon/Utils/Win32.h>
#include <XivAlexanderCommon/Utils/Win32/Closeable.h>

using Internet = Utils::Win32::Closeable<HINTERNET, WinHttpCloseHandle>;

constexpr DWORD BufferSize = 8192;

Utils::Win32::WinHttp::Response Utils::Win32::WinHttp::Get(const std::string& url, const std::function<void(const void*, size_t)>& onData) {
	const auto wUrl = FromUtf8(url);

	URL_COMPONENTSW urlComp{
		.dwStructSize = sizeof(urlComp),
		.dwSchemeLength = static_cast<DWORD>(-1),
		.dwHostNameLength = static_cast<DWORD>(-1),
		.dwUrlPathLength = static_cast<DWORD>(-1),
		.dwExtraInfoLength = static_cast<DWORD>(-1),
	};
	if (!WinHttpCrackUrl(wUrl.c_str(), static_cast<DWORD>(wUrl.size()), 0, &urlComp))
		throw Error("WinHttpCrackUrl");

	const std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
	const auto urlPath = std::format(L"{}{}",
		std::wstring_view(urlComp.lpszUrlPath, urlComp.dwUrlPathLength),
		std::wstring_view(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength));

	// Read IE proxy config and determine proxy settings
	std::wstring proxyUrl;
	if (WINHTTP_CURRENT_USER_IE_PROXY_CONFIG proxyInfo{}; WinHttpGetIEProxyConfigForCurrentUser(&proxyInfo)) {
		if (proxyInfo.lpszProxy) {
			proxyUrl = proxyInfo.lpszProxy;
			GlobalFree(proxyInfo.lpszProxy);
		}
		if (proxyInfo.lpszProxyBypass) {
			for (const auto& v : Utils::StringSplit<std::wstring>(Utils::StringReplaceAll<std::wstring>(proxyInfo.lpszProxyBypass, L";", L" "), L" ")) {
				if (lstrcmpiW(v.c_str(), hostName.c_str()) == 0) {
					proxyUrl.clear();
					break;
				}
			}
			GlobalFree(proxyInfo.lpszProxyBypass);
		}
		if (proxyInfo.lpszAutoConfigUrl)
			GlobalFree(proxyInfo.lpszAutoConfigUrl);
	}

	const auto hSession = Internet(
		WinHttpOpen(
			L"Mozilla/5.0",
			proxyUrl.empty() ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_NAMED_PROXY,
			proxyUrl.empty() ? WINHTTP_NO_PROXY_NAME : proxyUrl.c_str(),
			WINHTTP_NO_PROXY_BYPASS,
			0),
		0,
		"WinHttpOpen failed");

	const auto hConnect = Internet(
		WinHttpConnect(hSession, hostName.c_str(), urlComp.nPort, 0),
		0,
		"WinHttpConnect failed");

	const auto hRequest = Internet(
		WinHttpOpenRequest(
			hConnect,
			L"GET",
			urlPath.c_str(),
			nullptr,
			WINHTTP_NO_REFERER,
			WINHTTP_DEFAULT_ACCEPT_TYPES,
			urlComp.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0),
		"WinHttpOpenRequest failed");

	// Enable automatic redirects
	if (DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
		!WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy)))
		throw Error("WinHttpSetOption(RedirectPolicy) failed");

	if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
		throw Error("WinHttpSendRequest failed");

	if (!WinHttpReceiveResponse(hRequest, nullptr))
		throw Error("WinHttpReceiveResponse failed");

	DWORD statusCode = 0;
	if (DWORD statusCodeSize = sizeof(statusCode);
		!WinHttpQueryHeaders(
			hRequest,
			WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX,
			&statusCode,
			&statusCodeSize,
			WINHTTP_NO_HEADER_INDEX)) {
		throw Error("WinHttpQueryHeaders(StatusCode)");
	}

	Response response{.StatusCode = static_cast<int>(statusCode)};

	std::vector<uint8_t> buf;
	buf.reserve(BufferSize);
	for (DWORD len = 0; WinHttpQueryDataAvailable(hRequest, &len) && len > 0;) {
		len = std::min<DWORD>(len, BufferSize);
		buf.resize(len);
		DWORD bytesRead = 0;
		if (!WinHttpReadData(hRequest, buf.data(), len, &bytesRead))
			throw Error("WinHttpReadData");
		if (bytesRead == 0)
			break;
		if (onData)
			onData(buf.data(), bytesRead);
		else
			response.Body.append(reinterpret_cast<const char*>(buf.data()), bytesRead);
	}

	return response;
}
