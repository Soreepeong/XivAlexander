#pragma once
namespace App {
	namespace Misc {
		class Logger;
	}

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
			const char* m_pszName;

		protected:
			BaseRepository* const m_pBaseRepository;

			ItemBase(BaseRepository* pRepository, const char* pszName);

			virtual bool LoadFrom(const nlohmann::json&, bool announceChanged = false) = 0;
			virtual void SaveTo(nlohmann::json&) const = 0;

			void AnnounceChanged() {
				OnChangeListener(*this);
			}

		public:
			virtual ~ItemBase() = default;

			[[nodiscard]]
			const char* Name() const;

			Utils::ListenerManager<ItemBase, void, ItemBase&> OnChangeListener;
		};

		template<typename T>
		class Item : public ItemBase {
			friend class BaseRepository;
			T m_value;
			const std::function<T(const T&)> m_fnValidator;

		protected:
			Item(BaseRepository* pRepository, const char* pszName, T defaultValue)
				: ItemBase(pRepository, pszName)
				, m_value(defaultValue)
				, m_fnValidator(nullptr) {
			}

			Item(BaseRepository* pRepository, const char* pszName, T defaultValue, std::function<T(const T&)> validator)
				: ItemBase(pRepository, pszName)
				, m_value(defaultValue)
				, m_fnValidator(validator) {
			}

			bool Assign(const T& rv) {
				const auto sanitized = m_fnValidator ? m_fnValidator(rv) : rv;
				m_value = sanitized;
				return sanitized == rv;
			}

			bool LoadFrom(const nlohmann::json& data, bool announceChanged = false) override;

			void SaveTo(nlohmann::json& data) const override;

		public:
			~Item() override = default;

			const T& operator=(const T& rv) {
				if (m_value == rv)
					return m_value;

				m_value = rv;
				AnnounceChanged();
				return m_value;
			}

			operator T() const& {
				return m_value;
			}
		};

		class BaseRepository {
			friend class ItemBase;
			template<typename T>
			friend class Item;

			bool m_loaded = false;

			const Config* m_pConfig;
			const std::filesystem::path m_sConfigPath;
			const std::string m_parentKey;

			const std::shared_ptr<Misc::Logger> m_logger;

			std::vector<ItemBase*> m_allItems;
			std::vector<Utils::CallOnDestruction> m_destructionCallbacks;

		public:
			BaseRepository(__in_opt const Config* pConfig, std::filesystem::path path, std::string parentKey);

			[[nodiscard]] bool Loaded() const { return m_loaded; }

			void Save();
			void Reload(bool announceChange = false);

			[[nodiscard]] const std::filesystem::path& GetConfigPath() const {
				return m_sConfigPath;
			}

		protected:
			template<typename T>
			static Item<T> CreateConfigItem(BaseRepository* pRepository, const char* pszName, T defaultValue) {
				return Item<T>(pRepository, pszName, defaultValue);
			}
			template<typename T, typename ... Args>
			static Item<T> CreateConfigItem(BaseRepository* pRepository, const char* pszName, T defaultValue, Args...args) {
				return Item<T>(pRepository, pszName, defaultValue, std::forward<Args...>(args));
			}
		};

		enum class Language {
			SystemDefault,
			English,
			Korean,
			Japanese,
		};

		// when used as game launch parameter, subtract by one.
		enum class GameLanguage {
			Unspecified = 0,
			Japanese = 1,
			English = 2,
			German = 3,
			French = 4,
			ChineseSimplified = 5,
			ChineseTraditional = 6,
			Korean = 7,
		};

		enum class GameRegion {
			Unspecified = 0,
			Japan = 1,
			NorthAmerica = 2,
			Europe = 3,
			China = 100001,
			Korea = 100002,
		};

		enum class HighLatencyMitigationMode {
			SubtractLatency,
			SimulateRtt,
			SimulateNormalizedRttAndLatency,
		};

		static const std::map<Language, WORD> LanguageIdMap;
		static const std::map<GameLanguage, WORD> GameLanguageIdMap;
		static const std::map<WORD, int> LanguageIdNameResourceIdMap;
		static const std::map<GameRegion, int> RegionResourceIdMap;

		class Runtime : public BaseRepository {
			friend class Config;
			using BaseRepository::BaseRepository;

		public:

			// Miscellaneous configuration
			Item<bool> AlwaysOnTop = CreateConfigItem(this, "AlwaysOnTop", false);

			Item<bool> UseHighLatencyMitigation = CreateConfigItem(this, "UseHighLatencyMitigation", true);
			Item<HighLatencyMitigationMode> HighLatencyMitigationMode = CreateConfigItem(this, "HighLatencyMitigationMode", HighLatencyMitigationMode::SimulateNormalizedRttAndLatency);
			Item<bool> UseEarlyPenalty = CreateConfigItem(this, "UseEarlyPenalty", false);
			Item<bool> UseHighLatencyMitigationLogging = CreateConfigItem(this, "UseHighLatencyMitigationLogging", true);
			Item<bool> UseHighLatencyMitigationPreviewMode = CreateConfigItem(this, "UseHighLatencyMitigationPreviewMode", false);

			Item<bool> ReducePacketDelay = CreateConfigItem(this, "ReducePacketDelay", true);
			Item<bool> TakeOverLoopbackAddresses = CreateConfigItem(this, "TakeOverLoopback", false);
			Item<bool> TakeOverPrivateAddresses = CreateConfigItem(this, "TakeOverPrivateAddresses", false);
			Item<bool> TakeOverAllAddresses = CreateConfigItem(this, "TakeOverAllAddresses", false);
			Item<bool> TakeOverAllPorts = CreateConfigItem(this, "TakeOverAllPorts", false);

			Item<bool> UseOpcodeFinder = CreateConfigItem(this, "UseOpcodeFinder", false);
			Item<bool> UseEffectApplicationDelayLogger = CreateConfigItem(this, "UseEffectApplicationDelayLogger", false);
			Item<bool> ShowLoggingWindow = CreateConfigItem(this, "ShowLoggingWindow", true);
			Item<bool> ShowControlWindow = CreateConfigItem(this, "ShowControlWindow", true);
			Item<bool> UseAllIpcMessageLogger = CreateConfigItem(this, "UseAllIpcMessageLogger", false);

			Item<bool> UseHashTracker = CreateConfigItem(this, "UseHashTracker", false);
			Item<bool> UseHashTrackerKeyLogging = CreateConfigItem(this, "UseHashTrackerKeyLogging", false);
			Item<GameLanguage> HashTrackerLanguageOverride = CreateConfigItem(this, "HashTrackerLanguageOverride", GameLanguage::Unspecified);

			Item<Language> Language = CreateConfigItem(this, "Language", Language::SystemDefault);

			[[nodiscard]] WORD GetLangId() const;
			[[nodiscard]] LPCWSTR GetStringRes(UINT uId) const;
			template <typename ... Args>
			[[nodiscard]] std::wstring FormatStringRes(UINT uId, Args ... args) const {
				return std::format(GetStringRes(uId), std::forward<Args>(args)...);
			}
			[[nodiscard]] std::wstring GetLanguageNameLocalized(GameLanguage gameLanguage) const;
			[[nodiscard]] std::wstring GetRegionNameLocalized(GameRegion gameRegion) const;
		};

		class Game : public BaseRepository {
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
			Item<uint16_t> S2C_AddStatusEffect = CreateConfigItem(this, "S2C_AddStatusEffect", InvalidIpcType);
			Item<uint16_t> C2S_ActionRequest[2]{
				CreateConfigItem(this, "C2S_ActionRequest", InvalidIpcType),
				CreateConfigItem(this, "C2S_ActionRequestGroundTargeted", InvalidIpcType),
			};
		};

		class InitializationConfig : public BaseRepository {
			friend class Config;
			using BaseRepository::BaseRepository;

		public:
			// Relative paths are relative to the directory of the DLL.
			// All items accept relative paths.

			// If not set, defaults to %APPDATA%/XivAlexander
			Item<std::string> FixedConfigurationFolderPath = CreateConfigItem(this, "FixedConfigurationFolderPath", std::string(""));

			// If not set, defaults to files in System32 (SysWOW64) in %WINDIR% (GetSystemDirectory)
			// If set but invalid, will cause errors.
			Item<std::string> ChainLoadPath_d3d11 = CreateConfigItem(this, "ChainLoadPath_d3d11", std::string(""));
			Item<std::string> ChainLoadPath_dxgi = CreateConfigItem(this, "ChainLoadPath_dxgi", std::string(""));
			Item<std::string> ChainLoadPath_d3d9= CreateConfigItem(this, "ChainLoadPath_d3d9", std::string(""));
			Item<std::string> ChainLoadPath_dinput8= CreateConfigItem(this, "ChainLoadPath_dinput8", std::string(""));

			std::filesystem::path ResolveConfigStorageDirectoryPath();
			std::filesystem::path ResolveRuntimeConfigPath();
			std::filesystem::path ResolveGameOpcodeConfigPath();
		};

		static std::filesystem::path TranslatePath(const std::string&, bool dontTranslateEmpty = true);

	protected:
		static std::weak_ptr<Config> s_instance;
		bool m_bSuppressSave = false;

		Config(std::filesystem::path initializationConfigPath);

	public:
		InitializationConfig Init;
		Runtime Runtime;
		Game Game;

		virtual ~Config();

		void SetQuitting();

		static std::shared_ptr<Config> Acquire();
	};
}
