#include "pch.h"
#include "XivAlex.h"
#include "Utils_CallOnDestruction.h"
#include "Utils_Win32.h"
#include "Utils_Win32_Closeable.h"

std::tuple<std::wstring, std::wstring> XivAlex::ResolveGameReleaseRegion() {
	std::wstring path(PATHCCH_MAX_CCH, L'\0');
	path.resize(GetModuleFileNameW(nullptr, &path[0], static_cast<DWORD>(path.size())));
	return ResolveGameReleaseRegion(path);
}

static std::wstring TestPublisher(const std::filesystem::path& path) {
	// See: https://docs.microsoft.com/en-US/troubleshoot/windows/win32/get-information-authenticode-signed-executables

	constexpr auto ENCODING = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;

	HCERTSTORE hStore = nullptr;
	HCRYPTMSG hMsg = nullptr;
	DWORD dwEncoding = 0, dwContentType = 0, dwFormatType = 0;
	std::vector<Utils::CallOnDestruction> cleanupList;
	if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE,
		path.c_str(),
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
	const auto pvTypePara = const_cast<char*>(szOID_COUNTRY_NAME);
	country.resize(CertGetNameStringW(pCertContext, CERT_NAME_ATTR_TYPE, 0, pvTypePara, nullptr, 0));
	country.resize(CertGetNameStringW(pCertContext, CERT_NAME_ATTR_TYPE, 0, pvTypePara, &country[0], static_cast<DWORD>(country.size())) - 1);

	return country;
}

std::tuple<std::wstring, std::wstring> XivAlex::ResolveGameReleaseRegion(const std::filesystem::path& path) {
	const auto installationDir = path.parent_path().parent_path(); // remove "\game", "\ffxiv_dx11.exe"
	const auto gameDir = installationDir / L"game";
	const auto gameVerPath = gameDir / L"ffxivgame.ver";

	std::wstring gameVer;
	{
		const Utils::Win32::Closeable::Handle hGameVer(
			CreateFileW(gameVerPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr),
			INVALID_HANDLE_VALUE,
			L"ResolveGameReleaseRegion: Failed to open game version file(%s)",
			gameVerPath.c_str());
		LARGE_INTEGER size{};
		GetFileSizeEx(hGameVer, &size);
		if (size.QuadPart > 64)
			throw std::runtime_error("ResolveGameReleaseRegion: Game version file size too big.");
		std::string buf;
		buf.resize(size.QuadPart);
		DWORD read = 0;
		if (!ReadFile(hGameVer, &buf[0], size.LowPart, &read, nullptr))
			throw Utils::Win32::Error("ResolveGameReleaseRegion: Failed to read game version file");
		if (read != size.LowPart)
			throw std::runtime_error("ResolveGameReleaseRegion: Failed to read game version file in entirety.");
		gameVer = Utils::FromUtf8(buf);

		for (auto& chr : gameVer) {
			for (auto i : L"<>:\"/\\|?*") {
				if (chr == i || chr < 32)
					chr = L'_';
			}
		}
	}

	std::map<std::wstring, size_t> publisherCountries;
	for (const auto& possibleRegionSpecificFilesDir : {
		installationDir / L"boot" / L"ffxiv*.exe",
		installationDir / L"sdo" / L"sdologinentry.dll",
		}) {
		WIN32_FIND_DATAW data{};
		const auto hFindFile = Utils::Win32::Closeable::FindFile(
			FindFirstFileW(possibleRegionSpecificFilesDir.c_str(), &data),
			INVALID_HANDLE_VALUE);
		if (!hFindFile)
			continue;

		do {
			const auto path = possibleRegionSpecificFilesDir.parent_path() / data.cFileName;
			const auto publisherCountry = TestPublisher(path);
			if (!publisherCountry.empty())
				publisherCountries[publisherCountry]++;
		} while (FindNextFileW(hFindFile, &data));
	}

	if (!publisherCountries.empty()) {
		auto maxElem = std::max_element(publisherCountries.begin(), publisherCountries.end());
		return std::make_tuple(
			maxElem->first,
			gameVer
		);
	}

	auto buf = installationDir.wstring();
	CharLowerW(&buf[0]);
	uLong crc = crc32(crc32(0L, nullptr, 0),
		reinterpret_cast<Bytef*>(&buf[0]),
		static_cast<uInt>(buf.size() * sizeof buf[0]));

	return std::make_tuple(
		Utils::FormatString(L"unknown_%08x", crc),
		gameVer
	);
}

XivAlex::VersionInformation XivAlex::CheckUpdates() {
	std::ostringstream os;
	
	curlpp::Easy req;
	req.setOpt(curlpp::options::Url("https://api.github.com/repos/Soreepeong/XivAlexander/releases/latest"));
	req.setOpt(curlpp::options::UserAgent("Mozilla/5.0"));
	os << req;
	const auto parsed = nlohmann::json::parse(os.str());
	const auto item = parsed.at("assets")[0];

	std::istringstream in(parsed.at("published_at").get<std::string>());
	std::chrono::sys_seconds tp;
	from_stream(in, "%FT%TZ", tp);
	if (in.fail())
		throw std::format_error(Utils::FormatString("Failed to parse datetime string \"%s\"", in.str().c_str()));
	
	return {
		.Name = parsed.at("name").get<std::string>(),
		.Body = parsed.at("body").get<std::string>(),
		.PublishDate = std::chrono::zoned_time(std::chrono::current_zone(), tp),
		.DownloadLink = item.at("browser_download_url").get<std::string>(),
		.DownloadSize = item.at("size").get<size_t>(),
	};
}
