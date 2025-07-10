#include "pch.h"
#include "Config.h"

#include <XivAlexanderCommon/Utils/Win32/Process.h>
#include <XivAlexanderCommon/Utils/Win32/Resource.h>

#include "Misc/GameInstallationDetector.h"
#include "Misc/Logger.h"
#include "resource.h"
#include "XivAlexander.h"

std::weak_ptr<XivAlexander::Config> XivAlexander::Config::s_instance;
static const std::map<XivAlexander::Language, WORD> LanguageIdMap{
	{XivAlexander::Language::SystemDefault, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL)},
	{XivAlexander::Language::Japanese, MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN)},
	{XivAlexander::Language::English, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)},
	{XivAlexander::Language::Korean, MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN)},
};
static const std::map<Sqex::Language, WORD> GameLanguageIdMap{
	{Sqex::Language::Unspecified, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL)},
	{Sqex::Language::Japanese, MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN)},
	{Sqex::Language::English, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)},
	{Sqex::Language::German, MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN)},
	{Sqex::Language::French, MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH)},
	{Sqex::Language::ChineseSimplified, MAKELANGID(LANG_CHINESE_SIMPLIFIED, SUBLANG_CHINESE_SIMPLIFIED)},
	{Sqex::Language::ChineseTraditional, MAKELANGID(LANG_CHINESE_TRADITIONAL, SUBLANG_CHINESE_TRADITIONAL)},
	{Sqex::Language::Korean, MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN)},
};
static const std::map<WORD, int> LanguageIdNameResourceIdMap{
	{MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), IDS_LANGUAGE_NAME_UNSPECIFIED},
	{MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN), IDS_LANGUAGE_NAME_JAPANESE},
	{MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), IDS_LANGUAGE_NAME_ENGLISH},
	{MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN), IDS_LANGUAGE_NAME_GERMAN},
	{MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH), IDS_LANGUAGE_NAME_FRENCH},
	{MAKELANGID(LANG_CHINESE_SIMPLIFIED, SUBLANG_CHINESE_SIMPLIFIED), IDS_LANGUAGE_NAME_CHINESE_SIMPLIFIED},
	{MAKELANGID(LANG_CHINESE_TRADITIONAL, SUBLANG_CHINESE_TRADITIONAL), IDS_LANGUAGE_NAME_CHINESE_TRADITIONAL},
	{MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN), IDS_LANGUAGE_NAME_KOREAN},
};
static const std::map<Sqex::Region, int> RegionResourceIdMap{
	{Sqex::Region::Unspecified, IDS_REGION_NAME_UNSPECIFIED},
	{Sqex::Region::Japan, IDS_REGION_NAME_JAPAN},
	{Sqex::Region::NorthAmerica, IDS_REGION_NAME_NORTH_AMERICA},
	{Sqex::Region::Europe, IDS_REGION_NAME_EUROPE},
	{Sqex::Region::China, IDS_REGION_NAME_CHINA},
	{Sqex::Region::Korea, IDS_REGION_NAME_KOREA},
};

XivAlexander::Config::BaseRepository::BaseRepository(__in_opt const Config* pConfig, std::filesystem::path path, std::string parentKey)
	: m_pConfig(pConfig)
	, m_sConfigPath(std::move(path))
	, m_parentKey(std::move(parentKey))
	, m_logger(Misc::Logger::Acquire()) {
}

XivAlexander::Config::BaseRepository::~BaseRepository() = default;

XivAlexander::Config::ItemBase::ItemBase(BaseRepository * pRepository, const char* pszName)
	: Name(pszName)
	, m_pBaseRepository(pRepository) {
	pRepository->m_allItems.push_back(this);
}

void XivAlexander::Config::ItemBase::TriggerOnChange() {
	OnChange();
}

Utils::CallOnDestruction XivAlexander::Config::ItemBase::AddAndCallOnChange(std::function<void()> cb, std::function<void()> onUnbind) {
	auto r = OnChange(cb, std::move(onUnbind));
	cb();
	return r;
}

void XivAlexander::Config::BaseRepository::Reload(const std::filesystem::path & from) {
	m_loaded = true;

	nlohmann::json totalConfig;
	if (exists(from.empty() ? m_sConfigPath : from)) {
		try {
			totalConfig = Utils::ParseJsonFromFile(from.empty() ? m_sConfigPath : from);
			if (totalConfig.type() != nlohmann::detail::value_t::object)
				throw std::runtime_error("Root must be an object.");  // TODO: string resource
		} catch (const std::exception& e) {
			totalConfig = nlohmann::json::object();
			m_logger->FormatDefaultLanguage<LogLevel::Warning>(LogCategory::General,
				IDS_ERROR_CONFIGURATION_LOAD,
				e.what());
		}
	} else {
		totalConfig = nlohmann::json::object();
		m_logger->FormatDefaultLanguage(LogCategory::General, IDS_LOG_NEW_CONFIG, Utils::ToUtf8((from.empty() ? m_sConfigPath : from).wstring()));
	}

	const auto& currentConfig = m_parentKey.empty() ? totalConfig : totalConfig[m_parentKey];

	const auto suppressSave = WithSuppressSave();
	for (const auto& item : m_allItems)
		item->LoadFrom(currentConfig);
}

class XivAlexander::Config::ConfigCreator : public Config {
public:
	ConfigCreator(std::filesystem::path initializationConfigPath)
		: Config(std::move(initializationConfigPath)) {
	}

	~ConfigCreator() override = default;
};

std::shared_ptr<XivAlexander::Config> XivAlexander::Config::Acquire() {
	auto r = s_instance.lock();
	if (!r) {
		static std::mutex mtx;
		std::lock_guard lock(mtx);

		r = s_instance.lock();
		if (!r) {
			const auto dllDir = Dll::Module().PathOf().parent_path();
			s_instance = r = std::make_shared<ConfigCreator>(dllDir / "config.xivalexinit.json");
		}
	}
	return r;
}

XivAlexander::Config::RuntimeRepository::RuntimeRepository(__in_opt const Config * pConfig, std::filesystem::path path, std::string parentKey)
	: BaseRepository(pConfig, std::move(path), std::move(parentKey)) {

	m_cleanup += Language.AddAndCallOnChange([&]() {
		Utils::Win32::Error::SetDefaultLanguageId(GetLangId());
		});

	m_cleanup += SynchronizeProcessing.AddAndCallOnChange([&]() { UseMainThreadTimingHandler = SynchronizeProcessing || LockFramerateAutomatic || LockFramerateInterval; });
	m_cleanup += LockFramerateAutomatic.AddAndCallOnChange([&]() { UseMainThreadTimingHandler = SynchronizeProcessing || LockFramerateAutomatic || LockFramerateInterval; });
	m_cleanup += LockFramerateInterval.AddAndCallOnChange([&]() { UseMainThreadTimingHandler = SynchronizeProcessing || LockFramerateAutomatic || LockFramerateInterval; });
}

XivAlexander::Config::RuntimeRepository::~RuntimeRepository() {
	m_cleanup.Clear();
}

void XivAlexander::Config::RuntimeRepository::Reload(const std::filesystem::path & from) {
	BaseRepository::Reload(from);
	Utils::Win32::Error::SetDefaultLanguageId(GetLangId());
}

WORD XivAlexander::Config::RuntimeRepository::GetLangId() const {
	if (const auto i = LanguageIdMap.find(Language); i != LanguageIdMap.end())
		return i->second;

	return MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
}

LPCWSTR XivAlexander::Config::RuntimeRepository::GetStringRes(UINT uId) const {
	return FindStringResourceEx(Dll::Module(), uId, GetLangId()) + 1;
}

std::wstring XivAlexander::Config::RuntimeRepository::GetLanguageNameLocalized(Sqex::Language gameLanguage) const {
	const auto langNameInUserLang = std::wstring(GetStringRes(LanguageIdNameResourceIdMap.at(GameLanguageIdMap.at(gameLanguage))));
	auto langNameInGameLang = std::wstring(FindStringResourceEx(Dll::Module(), LanguageIdNameResourceIdMap.at(GameLanguageIdMap.at(gameLanguage)), GameLanguageIdMap.at(gameLanguage)) + 1);
	if (langNameInUserLang == langNameInGameLang)
		return langNameInGameLang;
	else
		return std::format(L"{} ({})", langNameInUserLang, langNameInGameLang);
}

std::wstring XivAlexander::Config::RuntimeRepository::GetRegionNameLocalized(Sqex::Region gameRegion) const {
	return GetStringRes(RegionResourceIdMap.at(gameRegion));
}

std::vector<std::pair<WORD, std::string>> XivAlexander::Config::RuntimeRepository::GetDisplayLanguagePriorities() const {
	std::vector<std::pair<WORD, std::string>> res;
	if (Language != Language::SystemDefault) {
		wchar_t buf[64];
		LCIDToLocaleName(LanguageIdMap.at(Language), &buf[0], 64, 0);
		res.emplace_back(LanguageIdMap.at(Language), Utils::ToUtf8(buf));
	}
	try {
		ULONG num = 0, bufSize = 0;
		if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num, nullptr, &bufSize))
			throw Utils::Win32::Error("GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num, nullptr, &bufSize)");
		std::wstring buf(bufSize, L'\0');
		if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num, &buf[0], &bufSize))
			throw Utils::Win32::Error("GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num, &buf[0], &bufSize)");
		buf.resize(bufSize);
		auto ptr = &buf[0];
		while (*ptr) {
			const auto len = wcslen(ptr);
			res.emplace_back(LANGIDFROMLCID(LocaleNameToLCID(ptr, 0)), Utils::ToUtf8(ptr));
			ptr += len + 1;
		}
	} catch (...) {
		// pass
	}
	for (const auto& [language, languageId] : LanguageIdMap) {
		if (language == Language::SystemDefault || language == Language)
			continue;
		wchar_t buf[64];
		LCIDToLocaleName(languageId, &buf[0], 64, 0);
		res.emplace_back(languageId, Utils::ToUtf8(buf));
	}
	return res;
}

uint64_t XivAlexander::Config::RuntimeRepository::CalculateLockFramerateIntervalUs(double fromFps, double toFps, uint64_t gcdUs, uint64_t renderIntervalDeviation) {
	static double prevFromFps{}, prevToFps{};
	static uint64_t prevGcdUs{}, prevRenderIntervalDeviation{};
	static uint64_t prevResult{};
	if (prevFromFps == fromFps && prevToFps == toFps && prevGcdUs == gcdUs && prevRenderIntervalDeviation == renderIntervalDeviation) {
		return prevResult;
	}

	fromFps = std::min(1000000., std::max(1., fromFps));
	toFps = std::min(1000000., std::max(1., toFps));
	auto minInterval = static_cast<uint64_t>(1000000. / toFps);
	for (auto i = minInterval + 1, i_ = static_cast<uint64_t>(1000000. / fromFps); i <= i_; ++i) {
		if (i - gcdUs % i < minInterval - gcdUs % minInterval && i - gcdUs % i >= renderIntervalDeviation) {
			minInterval = i;
		}
	}
	prevFromFps = fromFps;
	prevToFps = toFps;
	prevGcdUs = gcdUs;
	prevRenderIntervalDeviation = renderIntervalDeviation;
	prevResult = minInterval;
	return minInterval;
}

[[nodiscard]] const std::map<std::string, std::string>& XivAlexander::Config::RuntimeRepository::GetMusicDirectoryPurchaseWebsites(std::string name) const {
	static std::map<std::string, std::string> empty;
	const auto it = m_musicDirectoryPurchaseWebsites.find(name);
	if (it == m_musicDirectoryPurchaseWebsites.end())
		return empty;
	return it->second;
}

std::filesystem::path XivAlexander::Config::InitRepository::ResolveConfigStorageDirectoryPath() {
	if (!Loaded())
		Reload();

	if (!FixedConfigurationFolderPath.Value().empty())
		return Utils::Win32::EnsureDirectory(TranslatePath(FixedConfigurationFolderPath.Value()));
	else
		return Utils::Win32::EnsureDirectory(Utils::Win32::EnsureKnownFolderPath(FOLDERID_RoamingAppData) / L"XivAlexander");
}

std::filesystem::path XivAlexander::Config::InitRepository::ResolveXivAlexInstallationPath() {
	if (!Loaded())
		Reload();

	if (!XivAlexFolderPath.Value().empty())
		return Utils::Win32::EnsureDirectory(TranslatePath(XivAlexFolderPath.Value()));
	else
		return Utils::Win32::EnsureDirectory(Utils::Win32::EnsureKnownFolderPath(FOLDERID_LocalAppData) / L"XivAlexander");
}

std::filesystem::path XivAlexander::Config::InitRepository::ResolveRuntimeConfigPath() {
	return ResolveConfigStorageDirectoryPath() / "config.runtime.json";
}

std::filesystem::path XivAlexander::Config::InitRepository::ResolveGameOpcodeConfigPath() {
	const auto gameReleaseInfo = Misc::GameInstallationDetector::GetGameReleaseInfo();
	return ResolveConfigStorageDirectoryPath() / std::format(L"game.{}.{}.json", gameReleaseInfo.CountryCode, gameReleaseInfo.PathSafeGameVersion);
}

std::filesystem::path XivAlexander::Config::TranslatePath(const std::filesystem::path & path, const std::filesystem::path & relativeTo) {
	return Utils::Win32::TranslatePath(path, relativeTo.empty() ? Dll::Module().PathOf().parent_path() : relativeTo);
}

XivAlexander::Config::Config(std::filesystem::path initializationConfigPath)
	: Init(this, std::move(initializationConfigPath), "")
	, Runtime(this, Init.ResolveRuntimeConfigPath(), Utils::ToUtf8(Utils::Win32::Process::Current().PathOf().wstring()))
	, Game(this, Init.ResolveGameOpcodeConfigPath(), "") {
	Runtime.Reload();
	Game.Reload();
}

XivAlexander::Config::~Config() = default;


void XivAlexander::Config::Reload() {
	Init.Reload();
	Runtime.Reload();
	Game.Reload();
}

Utils::CallOnDestruction XivAlexander::Config::BaseRepository::WithSuppressSave() {
	const auto _ = std::lock_guard(m_suppressSave.Mtx);
	m_suppressSave.SupressionCounter += 1;

	return { [this]() {
		{
			const auto _ = std::lock_guard(m_suppressSave.Mtx);
			m_suppressSave.SupressionCounter -= 1;
			if (m_suppressSave.SupressionCounter || !m_suppressSave.PendingSave)
				return;
		}

		Save();
	} };
}

void XivAlexander::Config::BaseRepository::Save(const std::filesystem::path & to) {
	if (m_suppressSave.SupressionCounter) {
		m_suppressSave.PendingSave = true;
		return;
	}

	const auto& targetPath = to.empty() ? m_sConfigPath : to;
	if (targetPath.empty())
		return;

	nlohmann::json totalConfig;
	try {
		totalConfig = Utils::ParseJsonFromFile(targetPath);
		if (totalConfig.type() != nlohmann::detail::value_t::object)
			throw std::runtime_error("Root must be an object.");  // TODO: string resource
	} catch (const std::exception&) {
		totalConfig = nlohmann::json::object();
	}

	nlohmann::json& currentConfig = m_parentKey.empty() ? totalConfig : totalConfig[m_parentKey];
	for (const auto& item : m_allItems)
		item->SaveTo(currentConfig);

	try {
		Utils::SaveJsonToFile(targetPath, totalConfig);
	} catch (const std::exception& e) {
		m_logger->FormatDefaultLanguage<LogLevel::Error>(LogCategory::General, IDS_ERROR_CONFIGURATION_SAVE, e.what());
	}
}

bool XivAlexander::Config::Item<uint16_t>::LoadFrom(const nlohmann::json & data) {
	if (const auto it = data.find(Name); it != data.end()) {
		uint16_t newValue;
		std::string strVal;
		try {
			strVal = it->get<std::string>();
			if (it->is_string())
				newValue = static_cast<uint16_t>(std::stoi(it->get<std::string>(), nullptr, 0));
			else if (it->is_number_integer())
				newValue = it->get<uint16_t>();
			else
				return false;
		} catch (const std::exception& e) {
			m_pBaseRepository->m_logger->FormatDefaultLanguage(LogCategory::General, IDS_ERROR_CONFIGURATION_PARSE_VALUE, strVal, e.what());
		}

		*this = newValue;
	}
	return false;
}

void XivAlexander::Config::Item<uint16_t>::SaveTo(nlohmann::json & data) const {
	data[Name] = std::format("0x{:04x}", m_value);
}

bool XivAlexander::PatchInstruction::CreateNewHmacKeyIfInvalid() {
	try {
		CryptoPP::Base64Decoder b64d;
		b64d.Put(reinterpret_cast<const byte*>(HmacKey.data()), HmacKey.size());
		b64d.MessageEnd();
		if (b64d.MaxRetrievable() == HmacKeySize)
			return false;
	} catch (...) {
		// pass
	}

	byte hmacKey[HmacKeySize];
	CryptoPP::OS_GenerateRandomBlock(false, hmacKey, HmacKeySize);
	
	CryptoPP::Base64Encoder b64e(nullptr, false);
	b64e.Put(hmacKey, sizeof hmacKey);
	b64e.MessageEnd();

	HmacKey.resize(static_cast<size_t>(b64e.MaxRetrievable()), 0);
	b64e.Get(reinterpret_cast<byte*>(HmacKey.data()), HmacKey.size());
	return true;
}

std::string XivAlexander::PatchInstruction::Digest() const {
	byte hmacResult[HmacKeySize + CryptoPP::HMAC<CryptoPP::SHA512>::DIGESTSIZE];
	
	CryptoPP::Base64Decoder b64d;
	b64d.Put(reinterpret_cast<const byte*>(HmacKey.data()), HmacKey.size());
	b64d.MessageEnd();
	if (b64d.MaxRetrievable() != HmacKeySize)
		return {};
	b64d.Get(hmacResult, HmacKeySize);
	
	CryptoPP::HMAC<CryptoPP::SHA512> hmac(hmacResult, HmacKeySize);

	auto nbuf = Name.size();
	hmac.Update(reinterpret_cast<const byte*>(&nbuf), sizeof nbuf);
	hmac.Update(reinterpret_cast<const byte*>(Name.data()), Name.size());

	for (const auto& bitness : {X64, X86}) {
		nbuf = bitness.size();
		hmac.Update(reinterpret_cast<const byte*>(&nbuf), sizeof nbuf);
		for (const auto& b1 : bitness) {
			nbuf = b1.size();
			hmac.Update(reinterpret_cast<const byte*>(&nbuf), sizeof nbuf);
			for (const auto& b2 : b1) {
				nbuf = b2.size();
				hmac.Update(reinterpret_cast<const byte*>(&nbuf), sizeof nbuf);
				hmac.Update(reinterpret_cast<const byte*>(b2.data()), b2.size());
			}
		}
	}

	hmac.Final(&hmacResult[HmacKeySize]);
	
	CryptoPP::Base64Encoder b64e(nullptr, false);
	b64e.Put(hmacResult, sizeof hmacResult);
	b64e.MessageEnd();

	std::string buf(static_cast<size_t>(b64e.MaxRetrievable()), 0);
	b64e.Get(reinterpret_cast<byte*>(buf.data()), buf.size());
	return buf;
}

void XivAlexander::to_json(nlohmann::json & j, const Language & value) {
	switch (value) {
		case Language::English:
			j = "English";
			break;

		case Language::Korean:
			j = "Korean";
			break;

		case Language::Japanese:
			j = "Japanese";
			break;

		case Language::SystemDefault:
		default:
			j = "SystemDefault";
	}
}

void XivAlexander::from_json(const nlohmann::json & it, Language & value) {
	auto newValueString = Utils::FromUtf8(it.get<std::string>());
	CharLowerW(&newValueString[0]);

	value = Language::SystemDefault;
	if (newValueString.empty())
		return;

	if (newValueString.substr(0, std::min<size_t>(7, newValueString.size())) == L"english")
		value = Language::English;
	else if (newValueString.substr(0, std::min<size_t>(6, newValueString.size())) == L"korean")
		value = Language::Korean;
	else if (newValueString.substr(0, std::min<size_t>(8, newValueString.size())) == L"japanese")
		value = Language::Japanese;
}

void XivAlexander::to_json(nlohmann::json & j, const HighLatencyMitigationMode & value) {
	switch (value) {
		case HighLatencyMitigationMode::SubtractLatency:
			j = "SubtractLatency";
			break;

		case HighLatencyMitigationMode::SimulateRtt:
			j = "SimulateRtt";
			break;
		
		case HighLatencyMitigationMode::SimulateNormalizedRttAndLatency:
		default:
			j = "SimulateNormalizedRttAndLatency";
	}
}

void XivAlexander::from_json(const nlohmann::json & it, HighLatencyMitigationMode & value) {
	auto newValueString = Utils::FromUtf8(it.get<std::string>());
	CharLowerW(&newValueString[0]);

	value = HighLatencyMitigationMode::SimulateNormalizedRttAndLatency;
	if (newValueString.empty())
		return;

	if (newValueString.substr(0, std::min<size_t>(31, newValueString.size())) == L"subtractnormalizedrttandlatency")
		value = HighLatencyMitigationMode::SimulateNormalizedRttAndLatency;
	else if (newValueString.substr(0, std::min<size_t>(11, newValueString.size())) == L"simulatertt")
		value = HighLatencyMitigationMode::SimulateRtt;
	else if (newValueString.substr(0, std::min<size_t>(16, newValueString.size())) == L"subtractlatency")
		value = HighLatencyMitigationMode::SubtractLatency;
}

void XivAlexander::to_json(nlohmann::json & j, const PatchInstruction & value) {
	j = nlohmann::json::object({
		{"Name", value.Name},
		{"HmacKey", value.HmacKey},
		{"x64", value.X64},
		{"x86", value.X86},
	});
}

void XivAlexander::from_json(const nlohmann::json & it, PatchInstruction & value) {
	value = {
		.Name = it.value("Name", "(unnamed)"),
		.HmacKey = it.value("HmacKey", ""),
		.X64 = it.value<std::vector<std::vector<std::string>>>("x64", {}),
		.X86 = it.value<std::vector<std::vector<std::string>>>("x86", {}),
	};
	value.CreateNewHmacKeyIfInvalid();
}

template<typename T>
bool XivAlexander::Config::Item<T>::LoadFrom(const nlohmann::json & data) {
	if (const auto it = data.find(Name); it != data.end()) {
		T newValue;
		try {
			newValue = it->template get<T>();
		} catch (...) {
			// do nothing for now
			// TODO: show how the value is invalid
#ifdef _DEBUG
			throw;
#endif
		}
		*this = newValue;
	}
	return false;
}

template<typename T>
void XivAlexander::Config::Item<T>::SaveTo(nlohmann::json & data) const {
	data[Name] = m_value;
}
