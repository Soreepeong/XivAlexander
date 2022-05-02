#pragma once

#include <XivAlexanderCommon/Sqex.h>
#include <XivAlexanderCommon/Utils/ListenerManager.h>

namespace XivAlexander {
	namespace Misc {
		class Logger;
	}

	enum class Language {
		SystemDefault,
		English,
		Korean,
		Japanese,
	};

	enum class HighLatencyMitigationMode {
		SubtractLatency,
		SimulateRtt,
		SimulateNormalizedRttAndLatency,
		StandardGcdDivision,
	};

	void to_json(nlohmann::json&, const Language&);
	void from_json(const nlohmann::json&, Language&);
	void to_json(nlohmann::json&, const HighLatencyMitigationMode&);
	void from_json(const nlohmann::json&, HighLatencyMitigationMode&);

	class Config {
	public:
		class BaseRepository;
		template<typename T>
		class Item;

		class ConfigCreator;

		class ItemBase {
			friend class BaseRepository;
			template<typename T>
			friend class Item;

			BaseRepository* const m_pBaseRepository;

		protected:
			ItemBase(BaseRepository* pRepository, const char* pszName);

			virtual bool LoadFrom(const nlohmann::json&) = 0;
			virtual void SaveTo(nlohmann::json&) const = 0;

			void TriggerOnChange();

		public:
			virtual ~ItemBase() = default;

			const char* const Name;
			Utils::ListenerManager<ItemBase, void> OnChange;
			[[nodiscard]] Utils::CallOnDestruction AddAndCallOnChange(std::function<void()> cb, std::function<void()> onUnbind = {});
		};

		template<typename T>
		class Item : public ItemBase {
			friend class BaseRepository;

			T m_value;
			const std::function<T(T)> m_sanitizer;

		protected:
			Item(BaseRepository* pRepository, const char* pszName, const T& defaultValue)
				: ItemBase(pRepository, pszName)
				, m_value(std::move(defaultValue))
				, m_sanitizer([](T v) { return std::move(v); }) {

				pRepository->m_cleanup += OnChange([pRepository]() { pRepository->Save(); });
			}

			Item(BaseRepository* pRepository, const char* pszName, const T& defaultValue, std::function<T(const T&)> validator)
				: ItemBase(pRepository, pszName)
				, m_value(std::move(defaultValue))
				, m_sanitizer(validator) {

				pRepository->m_cleanup += OnChange([pRepository]() { pRepository->Save(); });
			}

			bool LoadFrom(const nlohmann::json& data) override;
			void SaveTo(nlohmann::json& data) const override;

		public:
			~Item() override = default;

			Item<T>& operator=(const T& rv) {
				auto sanitized = m_sanitizer(rv);
				if (m_value != sanitized) {
					m_value = std::move(sanitized);
					TriggerOnChange();
				}
				return *this;
			}

			Item<T>& operator=(T&& rv) {
				auto sanitized = m_sanitizer(std::move(rv));
				if (m_value != sanitized) {
					m_value = std::move(sanitized);
					TriggerOnChange();
				}
				return *this;
			}

			[[nodiscard]] operator T() const& {
				return m_value;
			}

			[[nodiscard]] const T& Value() const {
				return m_value;
			}

			template<typename = std::enable_if_t<std::is_same_v<T, bool>>>
			Item<T>& Toggle() {
				m_value = !m_value;
				TriggerOnChange();
				return *this;
			}

			template<typename = std::enable_if_t<std::is_same_v<T, bool>>>
			[[nodiscard]] Utils::CallOnDestruction AddAndCallOnBoolChange(std::function<void()> onTrue, std::function<void()> onFalse) {
				if (!onTrue)
					onTrue = []() {};

				if (!onFalse)
					onFalse = []() {};

				if (m_value)
					onTrue();
				else
					onFalse();

				auto cb = [this, onTrue = std::move(onTrue), onFalse]() {
					if (m_value)
						onTrue();
					else
						onFalse();
				};
				auto uncb = [this, onFalse = std::move(onFalse)]() {
					if (m_value)
						onFalse();
				};
				return OnChange(std::move(cb), std::move(uncb));
			}
		};

		class BaseRepository {
			friend class ItemBase;
			template<typename T>
			friend class Item;

			bool m_loaded = false;
			struct {
				std::mutex Mtx;
				bool PendingSave = false;
				size_t SupressionCounter = 0;
			} m_suppressSave;

			const Config* m_pConfig;
			const std::filesystem::path m_sConfigPath;
			const std::string m_parentKey;

			const std::shared_ptr<Misc::Logger> m_logger;

			std::vector<ItemBase*> m_allItems;

		protected:
			Utils::CallOnDestruction::Multiple m_cleanup;

		public:
			BaseRepository(__in_opt const Config* pConfig, std::filesystem::path path, std::string parentKey);
			virtual ~BaseRepository();

			[[nodiscard]] auto Loaded() const { return m_loaded; }

			Utils::CallOnDestruction WithSuppressSave();

			void Save(const std::filesystem::path& to = {});
			virtual void Reload(const std::filesystem::path& from = {});

			[[nodiscard]] auto GetConfigPath() const { return m_sConfigPath; }

		protected:
			template<typename T>
			static Item<T> CreateConfigItem(BaseRepository* pRepository, const char* pszName) {
				return Item<T>(pRepository, pszName, T{});
			}

			template<typename T>
			static Item<T> CreateConfigItem(BaseRepository* pRepository, const char* pszName, T defaultValue) {
				return Item<T>(pRepository, pszName, defaultValue);
			}

			template<typename T>
			static Item<T> CreateConfigItem(BaseRepository* pRepository, const char* pszName, T defaultValue, std::function<T(const T&)> validator) {
				return Item<T>(pRepository, pszName, defaultValue, validator);
			}
		};

		// Relative paths are relative to the directory of the DLL.
		// All items accept relative paths.

		class RuntimeRepository : public BaseRepository {
			friend class Config;
			using BaseRepository::BaseRepository;

		public:

			// Miscellaneous configuration
			Item<bool> AlwaysOnTop_GameMainWindow = CreateConfigItem(this, "AlwaysOnTop_GameMainWindow", false);

			Item<bool> AlwaysOnTop_XivAlexMainWindow = CreateConfigItem(this, "AlwaysOnTop_XivAlexMainWindow", true);
			Item<bool> HideOnMinimize_XivAlexMainWindow = CreateConfigItem(this, "HideOnMinimize_XivAlexMainWindow", false);

			Item<bool> AlwaysOnTop_XivAlexLogWindow = CreateConfigItem(this, "AlwaysOnTop_XivAlexLogWindow", false);
			Item<bool> UseWordWrap_XivAlexLogWindow = CreateConfigItem(this, "UseWordWrap_XivAlexLogWindow", false);
			Item<bool> UseMonospaceFont_XivAlexLogWindow = CreateConfigItem(this, "UseMonospaceFont_XivAlexLogWindow", false);

			Item<bool> UseNetworkTimingHandler = CreateConfigItem(this, "UseNetworkTimingHandler", true);
			Item<HighLatencyMitigationMode> HighLatencyMitigationMode = CreateConfigItem(this, "HighLatencyMitigationMode", HighLatencyMitigationMode::SimulateNormalizedRttAndLatency);
			Item<bool> UseHighLatencyMitigationLogging = CreateConfigItem(this, "UseHighLatencyMitigationLogging", true);
			Item<bool> UseHighLatencyMitigationPreviewMode = CreateConfigItem(this, "UseHighLatencyMitigationPreviewMode", false);

			// Troubleshooting purposes. If you think it's not working, change this to zero, and see if anything's different.
			// * If you find nothing has changed, try checking stuff in "Network > Troubleshooting".
			// * If you still find nothing has changed, make an issue with a log from the log window.
			// Revert before doing anything other than hitting striking dummies.
			// If you lower this value to an unreasonable extent, expect to get called out and banned from ranking communities.
			// SE probably doesn't care enough, but other players will.
			Item<int64_t> ExpectedAnimationLockDurationUs = CreateConfigItem(this, "ExpectedAnimationLockDurationUs", 75000LL);

			// Should be the doubled value of the above.
			Item<int64_t> MaximumAnimationLockDurationUs = CreateConfigItem(this, "MaximumAnimationLockDurationUs", 150000LL);

			Item<bool> ReducePacketDelay = CreateConfigItem(this, "ReducePacketDelay", false);
			Item<bool> TakeOverLoopbackAddresses = CreateConfigItem(this, "TakeOverLoopback", false);
			Item<bool> TakeOverPrivateAddresses = CreateConfigItem(this, "TakeOverPrivateAddresses", false);
			Item<bool> TakeOverAllAddresses = CreateConfigItem(this, "TakeOverAllAddresses", false);
			Item<bool> TakeOverAllPorts = CreateConfigItem(this, "TakeOverAllPorts", false);

			Item<bool> UseOpcodeFinder = CreateConfigItem(this, "UseOpcodeFinder", false);
			Item<bool> UseEffectApplicationDelayLogger = CreateConfigItem(this, "UseEffectApplicationDelayLogger", false);
			Item<bool> ShowLoggingWindow = CreateConfigItem(this, "ShowLoggingWindow", true);
			Item<bool> ShowControlWindow = CreateConfigItem(this, "ShowControlWindow", true);
			Item<bool> UseAllIpcMessageLogger = CreateConfigItem(this, "UseAllIpcMessageLogger", false);
			
			Item<bool> UseHashTrackerKeyLogging = CreateConfigItem(this, "UseHashTrackerKeyLogging", false);
			Item<bool> LogAllDataFileRead = CreateConfigItem(this, "LogAllDataFileRead", false);
			Item<Sqex::Language> ResourceLanguageOverride = CreateConfigItem(this, "ResourceLanguageOverride", Sqex::Language::Unspecified);
			Item<Sqex::Language> VoiceResourceLanguageOverride = CreateConfigItem(this, "VoiceResourceLanguageOverride", Sqex::Language::Unspecified);

			Item<Sqex::Language> RememberedGameLaunchLanguage = CreateConfigItem(this, "RememberedGameLaunchLanguage", Sqex::Language::Unspecified);
			Item<Sqex::Region> RememberedGameLaunchRegion = CreateConfigItem(this, "RememberedGameLaunchRegion", Sqex::Region::Unspecified);

			Item<bool> CheckForUpdatedOpcodesOnStartup = CreateConfigItem(this, "CheckForUpdatedOpcodesOnStartup", true);

			Item<bool> UseMoreCpuTime = CreateConfigItem(this, "UseMoreCpuPower", false);
			Item<bool> SynchronizeProcessing = CreateConfigItem(this, "SynchronizeProcessing", false);

			Item<uint64_t> LockFramerateInterval = CreateConfigItem<uint64_t>(this, "LockFramerate", 0, [](const uint64_t& val) {
				return std::min<uint64_t>(std::max<uint64_t>(0, val), 1000000);
				});
			Item<bool> LockFramerateAutomatic = CreateConfigItem(this, "LockFramerateAutomatic", false);
			Item<double> LockFramerateTargetFramerateRangeFrom = CreateConfigItem(this, "LockFramerateTargetFramerateRangeFrom", 50.);
			Item<double> LockFramerateTargetFramerateRangeTo = CreateConfigItem(this, "LockFramerateTargetFramerateRangeTo", 60.);
			Item<uint64_t> LockFramerateMaximumRenderIntervalDeviation = CreateConfigItem<uint64_t>(this, "LockFramerateMaximumRenderIntervalDeviation", 100, [](const uint64_t& val) {
				return std::min<uint64_t>(std::max<uint64_t>(0, val), 1000000);
				});
			Item<uint64_t> LockFramerateGlobalCooldown = CreateConfigItem<uint64_t>(this, "LockFramerateGlobalCooldown", 250);

			Item<bool> UseMainThreadTimingHandler = CreateConfigItem(this, "UseMainThreadTimingHandler", false);

			Item<Language> Language = CreateConfigItem(this, "Language", Language::SystemDefault);

			// If not set, default to files in System32 (SysWOW64) in %WINDIR% (GetSystemDirectory)
			// If set but invalid, show errors.
			Item<std::vector<std::filesystem::path>> ChainLoadPath_d3d11 = CreateConfigItem<std::vector<std::filesystem::path>>(this, "ChainLoadPath_d3d11");
			Item<std::vector<std::filesystem::path>> ChainLoadPath_dxgi = CreateConfigItem<std::vector<std::filesystem::path>>(this, "ChainLoadPath_dxgi");
			Item<std::vector<std::filesystem::path>> ChainLoadPath_d3d9 = CreateConfigItem<std::vector<std::filesystem::path>>(this, "ChainLoadPath_d3d9");
			Item<std::vector<std::filesystem::path>> ChainLoadPath_dinput8 = CreateConfigItem<std::vector<std::filesystem::path>>(this, "ChainLoadPath_dinput8");

			Item<bool> UseModding = CreateConfigItem(this, "UseModding", false);
			Item<bool> CompressModdedFiles = CreateConfigItem(this, "CompressModdedFiles", false);
			Item<bool> TtmpFlattenSubdirectoryDisplay = CreateConfigItem(this, "TtmpFlattenSubdirectoryDisplay", false);
			Item<bool> TtmpUseSubdirectoryTogglingOnFlattenedView = CreateConfigItem(this, "", false);
			Item<bool> TtmpShowDedicatedMenu = CreateConfigItem(this, "TtmpShowDedicatedMenu", false);
			Item<std::vector<std::filesystem::path>> AdditionalSqpackRootDirectories =
				CreateConfigItem<std::vector<std::filesystem::path>>(this, "AdditionalSqpackRootDirectories");
			Item<std::vector<std::filesystem::path>> AdditionalTexToolsModPackSearchDirectories =
				CreateConfigItem<std::vector<std::filesystem::path>>(this, "AdditionalTexToolsModPackSearchDirectories");
			Item<std::vector<std::filesystem::path>> AdditionalGameResourceFileEntryRootDirectories =
				CreateConfigItem<std::vector<std::filesystem::path>>(this, "AdditionalGameResourceFileEntryRootDirectories");
			Item<std::vector<std::filesystem::path>> ExcelTransformConfigFiles =
				CreateConfigItem<std::vector<std::filesystem::path>>(this, "ExcelTransformConfigFiles");
			Item<std::vector<Sqex::Language>> FallbackLanguagePriority =
				CreateConfigItem<std::vector<Sqex::Language>>(this, "FallbackLanguagePriority");

			Item<std::filesystem::path> OverrideFontConfig = CreateConfigItem(this, "OverrideFontConfig", std::filesystem::path());
			
			Item<bool> MuteVoice_Battle = CreateConfigItem(this, "MuteVoice_Battle", false);
			Item<bool> MuteVoice_Cm = CreateConfigItem(this, "MuteVoice_Cm", false);
			Item<bool> MuteVoice_Emote = CreateConfigItem(this, "MuteVoice_Emote", false);
			Item<bool> MuteVoice_Line = CreateConfigItem(this, "MuteVoice_Line", false);

			Item<std::vector<std::filesystem::path>> MusicImportConfig = CreateConfigItem(this, "MusicImportConfig", std::vector<std::filesystem::path>());
			Item<std::map<std::string, std::vector<std::filesystem::path>>> MusicImportConfig_Directories = CreateConfigItem(this, "MusicImportConfig_Directories", std::map<std::string, std::vector<std::filesystem::path>>());
			Item<int> MusicImportTargetSamplingRate = CreateConfigItem(this, "MusicImportTargetSamplingRate", 0);
			
			RuntimeRepository(__in_opt const Config* pConfig, std::filesystem::path path, std::string parentKey);
			~RuntimeRepository() override;
			
			void Reload(const std::filesystem::path& from = {}) override;

			[[nodiscard]] WORD GetLangId() const;
			[[nodiscard]] LPCWSTR GetStringRes(UINT uId) const;
			template <typename ... Args>
			[[nodiscard]] std::wstring FormatStringRes(UINT uId, Args ... args) const {
				return std::format(GetStringRes(uId), std::forward<Args>(args)...);
			}
			[[nodiscard]] std::wstring GetLanguageNameLocalized(Sqex::Language gameLanguage) const;
			[[nodiscard]] std::wstring GetRegionNameLocalized(Sqex::Region gameRegion) const;
			[[nodiscard]] std::vector<std::pair<WORD, std::string>> GetDisplayLanguagePriorities() const;
			
			[[nodiscard]] std::vector<Sqex::Language> GetFallbackLanguageList() const;

			[[nodiscard]] static uint64_t CalculateLockFramerateIntervalUs(double fromFps, double toFps, uint64_t gcdUs, uint64_t maximumRenderIntervalDeviation);

		private:
			std::map<std::string, std::map<std::string, std::string>> m_musicDirectoryPurchaseWebsites;

		public:
			[[nodiscard]] const std::map<std::string, std::string>& GetMusicDirectoryPurchaseWebsites(std::string name) const;
		};

		class GameRepository : public BaseRepository {
			const uint16_t InvalidIpcType = 0x93DB;

			friend class Config;
			using BaseRepository::BaseRepository;

		public:
			// Make the program consume all network connections by default.
			Item<std::string> Server_IpRange = CreateConfigItem(this, "Server_IpRange", std::string("0.0.0.0/0"));
			Item<std::string> Server_PortRange = CreateConfigItem(this, "Server_PortRange", std::string("1-65535"));

			// Set defaults so that the values will never be a valid IPC code.
			// Assumes structure doesn't change too often.
			// Will be loaded from configuration file on initialization.
			Item<uint16_t> S2C_ActionEffects[5]{
				CreateConfigItem(this, "S2C_ActionEffect01", InvalidIpcType),
				CreateConfigItem(this, "S2C_ActionEffect08", InvalidIpcType),
				CreateConfigItem(this, "S2C_ActionEffect16", InvalidIpcType),
				CreateConfigItem(this, "S2C_ActionEffect24", InvalidIpcType),
				CreateConfigItem(this, "S2C_ActionEffect32", InvalidIpcType),
			};
			Item<uint16_t> S2C_ActorControl = CreateConfigItem(this, "S2C_ActorControl", InvalidIpcType);
			Item<uint16_t> S2C_ActorControlSelf = CreateConfigItem(this, "S2C_ActorControlSelf", InvalidIpcType);
			Item<uint16_t> S2C_ActorCast = CreateConfigItem(this, "S2C_ActorCast", InvalidIpcType);
			Item<uint16_t> S2C_EffectResult5 = CreateConfigItem(this, "S2C_EffectResult5", InvalidIpcType);
			Item<uint16_t> S2C_EffectResult6 = CreateConfigItem(this, "S2C_EffectResult6", InvalidIpcType);
			Item<uint16_t> S2C_EffectResult6Basic = CreateConfigItem(this, "S2C_EffectResult6Basic", InvalidIpcType);
			Item<uint16_t> C2S_ActionRequest[2]{
				CreateConfigItem(this, "C2S_ActionRequest", InvalidIpcType),
				CreateConfigItem(this, "C2S_ActionRequestGroundTargeted", InvalidIpcType),
			};
		};

		class InitRepository : public BaseRepository {
			friend class Config;
			using BaseRepository::BaseRepository;

		public:
			// Default value if empty: %APPDATA%/XivAlexander
			Item<std::filesystem::path> FixedConfigurationFolderPath = CreateConfigItem(this, "FixedConfigurationFolderPath", std::filesystem::path());
			// Default value if empty: %LOCALAPPDATA%/XivAlexander
			Item<std::filesystem::path> XivAlexFolderPath = CreateConfigItem(this,
#ifdef _DEBUG
				"XivAlexFolderPath_DEBUG"
#else
				"XivAlexFolderPath"
#endif
				, std::filesystem::path());

			std::filesystem::path ResolveConfigStorageDirectoryPath();
			std::filesystem::path ResolveXivAlexInstallationPath();
			std::filesystem::path ResolveRuntimeConfigPath();
			std::filesystem::path ResolveGameOpcodeConfigPath();
		};

		static std::filesystem::path TranslatePath(const std::filesystem::path& path, const std::filesystem::path& relativeTo = {});

	protected:
		static std::weak_ptr<Config> s_instance;

		Config(std::filesystem::path initializationConfigPath);

	public:
		InitRepository Init;
		RuntimeRepository Runtime;
		GameRepository Game;

		virtual ~Config();

		void Reload();

		static std::shared_ptr<Config> Acquire();
	};
}
