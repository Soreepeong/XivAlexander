#include "pch.h"
#include "include/Utils.h"

#include <stdexcept>

#include "include/CallOnDestruction.h"
#include "include/myzlib.h"
#include "include/Win32Handle.h"

std::wstring Utils::FromOem(const std::string& in) {
	const size_t length = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, in.c_str(), static_cast<int>(in.size()), nullptr, 0);
	std::wstring u16(length, 0);
	MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, in.c_str(), static_cast<int>(in.size()), const_cast<LPWSTR>(u16.c_str()), static_cast<int>(u16.size()));
	return u16;
}

std::wstring Utils::FromUtf8(const std::string& in) {
	const size_t length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in.c_str(), static_cast<int>(in.size()), nullptr, 0);
	std::wstring u16(length, 0);
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in.c_str(), static_cast<int>(in.size()), const_cast<LPWSTR>(u16.c_str()), static_cast<int>(u16.size()));
	return u16;
}

std::string Utils::ToUtf8(const std::wstring& u16) {
	const size_t length = WideCharToMultiByte(CP_UTF8, 0, u16.c_str(), static_cast<int>(u16.size()), nullptr, 0, nullptr, nullptr);
	std::string u8(length, 0);
	WideCharToMultiByte(CP_UTF8, 0, u16.c_str(), static_cast<int>(u16.size()), const_cast<LPSTR>(u8.c_str()), static_cast<int>(u8.size()), nullptr, nullptr);
	return u8;
}

uint64_t Utils::GetEpoch() {
	union {
		FILETIME ft;
		LARGE_INTEGER li;
	};
	GetSystemTimePreciseAsFileTime(&ft);
	return (li.QuadPart - 116444736000000000ULL) / 10 / 1000;
}

SYSTEMTIME Utils::EpochToLocalSystemTime(uint64_t epochMilliseconds) {
	union {
		FILETIME ft;
		LARGE_INTEGER li;
	};
	FILETIME lft;
	SYSTEMTIME st;

	li.QuadPart = epochMilliseconds * 10 * 1000ULL + 116444736000000000ULL;
	FileTimeToLocalFileTime(&ft, &lft);
	FileTimeToSystemTime(&lft, &st);
	return st;
}

uint64_t Utils::GetHighPerformanceCounter(int32_t multiplier) {
	LARGE_INTEGER time, freq;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&time);
	return time.QuadPart * multiplier / freq.QuadPart;
}

std::string Utils::ToUtf8(const std::string& in) {
	return ToUtf8(FromOem(in));
}

std::string Utils::FormatWindowsErrorMessage(unsigned int errorCode) {
	if (errorCode == -1)
		errorCode = GetLastError();
	std::string res;
	LPTSTR errorText = nullptr;
	FormatMessage(
		FORMAT_MESSAGE_FROM_SYSTEM
		| FORMAT_MESSAGE_ALLOCATE_BUFFER
		| FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr,
		errorCode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<LPTSTR>(&errorText), // output 
		0, // minimum size for output buffer
		nullptr); // arguments - see note 

	if (nullptr != errorText) {
		OutputDebugString(FormatString(TEXT("Windows Error: %s\n"), errorText).c_str());
		res = ToUtf8(errorText);
		LocalFree(errorText);
	}
	return res;
}

static void sockaddr_cmp_helper(int x, int y) {
	if (x < y)
		throw (-1);
	else if (x > y)
		throw 1;
}
static void sockaddr_cmp_helper(int x) {
	if (x)
		throw x;
}

int Utils::sockaddr_cmp(const void* x, const void* y) {
	const auto family1 = static_cast<const sockaddr*>(x)->sa_family;
	const auto family2 = static_cast<const sockaddr*>(y)->sa_family;
	try {
		sockaddr_cmp_helper(family1, family2);
		if (family1 == AF_INET) {
			const auto addr1 = static_cast<const sockaddr_in*>(x);
			const auto addr2 = static_cast<const sockaddr_in*>(y);
			sockaddr_cmp_helper(ntohl(addr1->sin_addr.s_addr), ntohl(addr2->sin_addr.s_addr));
			sockaddr_cmp_helper(ntohs(addr1->sin_port), ntohs(addr2->sin_port));
		} else if (family1 == AF_INET6) {
			const auto addr1 = static_cast<const sockaddr_in6*>(x);
			const auto addr2 = static_cast<const sockaddr_in6*>(y);
			sockaddr_cmp_helper(memcmp(addr1->sin6_addr.s6_addr, addr2->sin6_addr.s6_addr, sizeof addr1->sin6_addr.s6_addr));
			sockaddr_cmp_helper(ntohs(addr1->sin6_port), ntohs(addr2->sin6_port));
			sockaddr_cmp_helper(addr1->sin6_flowinfo, addr2->sin6_flowinfo);
			sockaddr_cmp_helper(addr1->sin6_scope_id, addr2->sin6_scope_id);
		}
	} catch (int r) {
		return r;
	}
	return 0;
}

std::string Utils::DescribeSockaddr(const sockaddr_in& sa) {
	if (sa.sin_family != AF_INET)
		return "invalid sockaddr_in";
	
	char s[INET_ADDRSTRLEN + 6] = { 0 };
	inet_ntop(AF_INET, &sa.sin_addr, s, sizeof s);
	return FormatString("%s:%d", s, ntohs(sa.sin_port));
}

std::string Utils::DescribeSockaddr(const sockaddr_in6& sa) {
	if (sa.sin6_family != AF_INET6)
		return "invalid sockaddr_in6";

	char s[INET6_ADDRSTRLEN + 6] = { 0 };
	inet_ntop(AF_INET6, &sa.sin6_addr, s, sizeof s);
	return FormatString("%s:%d", s, ntohs(sa.sin6_port));
}

std::string Utils::DescribeSockaddr(const sockaddr& sa) {
	if (sa.sa_family == AF_INET)
		return DescribeSockaddr(*reinterpret_cast<const sockaddr_in*>(&sa));
	if (sa.sa_family == AF_INET6)
		return DescribeSockaddr(*reinterpret_cast<const sockaddr_in6*>(&sa));
	return "unknown sa_family";
}

std::string Utils::DescribeSockaddr(const sockaddr_storage& sa) {
	return DescribeSockaddr(*reinterpret_cast<const sockaddr*>(&sa));
}

std::vector<std::string> Utils::StringSplit(const std::string& str, const std::string& delimiter) {
	std::vector<std::string> result;
	if (delimiter.empty()){
		for (size_t i = 0; i < str.size(); ++i)
			result.push_back(str.substr(i, 1));
	} else {
		size_t previousOffset = 0, offset;
		while ((offset = str.find(delimiter, previousOffset)) != std::string::npos) {
			result.push_back(str.substr(previousOffset, offset - previousOffset));
			previousOffset = offset + delimiter.length();
		}
		result.push_back(str.substr(previousOffset));
	}
	return result;
}

std::string Utils::StringTrim(const std::string& str, bool leftTrim, bool rightTrim) {
	size_t left = 0, right = str.length() - 1;
	if (leftTrim)
		while (left < str.length() && std::isspace(str[left]))
			left++;
	if (rightTrim)
		while (right != SIZE_MAX && std::isspace(str[right]))
			right--;
	return str.substr(left, right + 1 - left);
}

std::vector<uint8_t> Utils::ZlibDecompress(const uint8_t* src, size_t length) {
	z_stream stream;
	memset(&stream, 0, sizeof(stream));
	stream.next_in = src;
	stream.avail_in = static_cast<uInt>(length);
	if (inflateInit(&stream) != Z_OK)
		throw std::exception();

	try {
		std::vector<uint8_t> result;
		size_t pos = 0;
		while (true) {
			result.resize(result.size() + 8192);
			stream.avail_out = static_cast<uInt>(result.size() - pos);
			stream.next_out = &result[pos];
			const auto res = inflate(&stream, Z_FINISH);
			if (res != Z_OK && res != Z_BUF_ERROR && res != Z_STREAM_END)
				throw std::exception();
			pos = result.size() - stream.avail_out;
			if (res == Z_STREAM_END)
				break;
		}
		result.resize(pos);
		inflateEnd(&stream);
		return result;
	} catch (...) {
		inflateEnd(&stream);
		throw;
	}
}

std::vector<uint8_t> Utils::ZlibCompress(const uint8_t* src, size_t length) {
	z_stream stream;
	memset(&stream, 0, sizeof(stream));
	stream.next_in = src;
	stream.avail_in = static_cast<uInt>(length);
	if (deflateInit(&stream, Z_DEFAULT_COMPRESSION) != Z_OK)
		throw std::exception();

	try {
		std::vector<uint8_t> result;
		size_t pos = 0;
		while (true) {
			result.resize(result.size() + 8192);
			stream.avail_out = static_cast<uInt>(result.size() - pos);
			stream.next_out = &result[pos];
			const auto res = deflate(&stream, Z_FINISH);
			if (res != Z_OK && res != Z_BUF_ERROR && res != Z_STREAM_END)
				throw std::exception();
			pos = result.size() - stream.avail_out;
			if (res == Z_STREAM_END)
				break;
		}
		result.resize(pos);
		deflateEnd(&stream);
		return result;
	} catch (...) {
		deflateEnd(&stream);
		throw;
	}
}

void Utils::SetMenuState(HMENU hMenu, DWORD nMenuId, bool bChecked) {
	MENUITEMINFOW mii = { sizeof(MENUITEMINFOW) };
	mii.fMask = MIIM_STATE;

	GetMenuItemInfoW(hMenu, nMenuId, false, &mii);
	if (bChecked)
		mii.fState |= MFS_CHECKED;
	else
		mii.fState &= ~MFS_CHECKED;
	SetMenuItemInfoW(hMenu, nMenuId, false, &mii);
}

void Utils::SetMenuState(HWND hWnd, DWORD nMenuId, bool bChecked) {
	SetMenuState(GetMenu(hWnd), nMenuId, bChecked);
}

std::tuple<std::wstring, std::wstring> Utils::ResolveGameReleaseRegion() {
	std::wstring path(PATHCCH_MAX_CCH, L'\0');
	path.resize(GetModuleFileNameW(nullptr, &path[0], static_cast<DWORD>(path.size())));
	return ResolveGameReleaseRegion(path);
}

static std::wstring TestPublisher(const std::wstring &path) {
	// See: https://docs.microsoft.com/en-US/troubleshoot/windows/win32/get-information-authenticode-signed-executables

	constexpr auto ENCODING = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
	
	HCERTSTORE hStore = nullptr;
	HCRYPTMSG hMsg = nullptr;
	DWORD dwEncoding = 0, dwContentType = 0, dwFormatType = 0;
	std::vector<Utils::CallOnDestruction> cleanupList;
	if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE,
		&path[0],
		CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
		CERT_QUERY_FORMAT_FLAG_BINARY,
		0,
		&dwEncoding,
		&dwContentType,
		&dwFormatType,
		&hStore,
		&hMsg,
		nullptr))
		return L"";
	if (hMsg) cleanupList.emplace_back([hMsg]() { CryptMsgClose(hMsg); });
	if (hStore) cleanupList.emplace_back([hStore]() { CertCloseStore(hStore, 0); });

	DWORD cbData = 0;
	std::vector<uint8_t> signerInfoBuf;
	for (size_t i = 0; i < 2; ++i) {
		if (!CryptMsgGetParam(hMsg,
			CMSG_SIGNER_INFO_PARAM,
			0,
			signerInfoBuf.empty() ? nullptr : &signerInfoBuf[0],
			&cbData))
			return L"";
		signerInfoBuf.resize(cbData);
	}

	const auto& signerInfo = *reinterpret_cast<CMSG_SIGNER_INFO*>(&signerInfoBuf[0]);
	
	CERT_INFO certInfo{};
	certInfo.Issuer = signerInfo.Issuer;
	certInfo.SerialNumber = signerInfo.SerialNumber;
	const auto pCertContext = CertFindCertificateInStore(hStore,
		ENCODING,
		0,
		CERT_FIND_SUBJECT_CERT,
		&certInfo,
		nullptr);
	if (!pCertContext)
		return L"";
	if (pCertContext) cleanupList.emplace_back([pCertContext]() { CertFreeCertificateContext(pCertContext); });

	std::wstring country;
	country.resize(CertGetNameStringW(pCertContext, CERT_NAME_ATTR_TYPE, 0, const_cast<char*>(szOID_COUNTRY_NAME), nullptr, 0));
	country.resize(CertGetNameStringW(pCertContext, CERT_NAME_ATTR_TYPE, 0, const_cast<char*>(szOID_COUNTRY_NAME), &country[0], static_cast<DWORD>(country.size())) - 1);
	
	return country;
}

std::tuple<std::wstring, std::wstring> Utils::ResolveGameReleaseRegion(const std::wstring& path) {
	std::wstring bootPath = path;
	bootPath.resize(PATHCCH_MAX_CCH);

	// boot: C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\ffxiv_dx11.exe
	
	PathCchRemoveFileSpec(&bootPath[0], bootPath.size());
	// boot: C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game

	std::wstring gameVerPath = bootPath;
	gameVerPath.resize(PATHCCH_MAX_CCH);
	PathCchAppend(&gameVerPath[0], gameVerPath.size(), L"ffxivgame.ver");
	gameVerPath.resize(wcsnlen(&gameVerPath[0], gameVerPath.size()));
	// boot: C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\ffxivgame.ver
	
	PathCchRemoveFileSpec(&bootPath[0], bootPath.size());
	// boot: C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn
	
	PathCchAppend(&bootPath[0], bootPath.size(), L"boot");
	// boot: C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\boot

	bootPath.resize(wcsnlen(&bootPath[0], bootPath.size()));

	std::wstring gameVer;
	{
		const Win32Handle hGameVer(CreateFile(gameVerPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
		LARGE_INTEGER size{};
		GetFileSizeEx(hGameVer, &size);
		if (size.QuadPart > 64)
			throw std::runtime_error("Game version file size too big");
		std::string buf;
		buf.resize(size.QuadPart);
		DWORD read = 0;
		if (!ReadFile(hGameVer, &buf[0], size.LowPart, &read, nullptr) || read != size.LowPart)
			throw std::runtime_error("Failed to read game version");
		gameVer = FromUtf8(buf);
	}

	WIN32_FIND_DATAW data{};
	Win32Handle<HANDLE, FindClose> hFindFile(FindFirstFileW(FormatString(L"%s\\ffxiv*.exe", bootPath.c_str()).c_str(), &data));
	do {
		const auto path = FormatString(L"%s\\%s", bootPath.c_str(), data.cFileName);
		const auto publisherCountry = TestPublisher(path);
		if (!publisherCountry.empty())
			return std::make_tuple(
				publisherCountry,
				gameVer
			);
		
	} while (FindNextFileW(hFindFile, &data));

	CharLowerW(&bootPath[0]);
	uLong crc = crc32(crc32(0L, nullptr, 0), 
		reinterpret_cast<Bytef*>(&bootPath[0]), 
		static_cast<uInt>(bootPath.size() * sizeof bootPath[0]));
	
	return std::make_tuple(
		FormatString(L"unknown_%08x", crc),
		gameVer
	);
}
