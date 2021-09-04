#pragma once

#include <XivAlexanderCommon/Sqex.h>
#include <XivAlexanderCommon/Utils_ListenerManager.h>

namespace App {
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

			[[nodiscard]] auto Name() const { return m_pszName; }

			Utils::ListenerManager<ItemBase, void, ItemBase&> OnChangeListener;
		};

		template<typename T>
		class Item : public ItemBase {
			friend class BaseRepository;
			T m_value;
			const std::function<T(const T&)> m_fnValidator;

		protected:
			Item(BaseRepository* pRepository, const char* pszName, const T& defaultValue)
				: ItemBase(pRepository, pszName)
				, m_value(std::move(defaultValue))
				, m_fnValidator(nullptr) {
			}

			Item(BaseRepository* pRepository, const char* pszName, const T& defaultValue, std::function<T(const T&)> validator)
				: ItemBase(pRepository, pszName)
				, m_value(std::move(defaultValue))
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

			Item<T>& operator=(const T& rv) {
				if (m_value == rv)
					return *this;

				m_value = rv;
				AnnounceChanged();
				return *this;
			}

			[[nodiscard]] operator T() const& {
				return m_value;
			}

			[[nodiscard]] const T& Value() const {
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

			[[nodiscard]] auto Loaded() const { return m_loaded; }

			void Save();
			void Reload(bool announceChange = false);

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

			template<typename T, typename ... Args>
			static Item<T> CreateConfigItem(BaseRepository* pRepository, const char* pszName, T defaultValue, Args...args) {
				return Item<T>(pRepository, pszName, defaultValue, std::forward<Args...>(args));
			}
		};

		// Relative paths are relative to the directory of the DLL.
		// All items accept relative paths.

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

			Item<bool> UseHashTrackerKeyLogging = CreateConfigItem(this, "UseHashTrackerKeyLogging", false);
			Item<Sqex::Language> HashTrackerLanguageOverride = CreateConfigItem(this, "HashTrackerLanguageOverride", Sqex::Language::Unspecified);

			Item<Language> Language = CreateConfigItem(this, "Language", Language::SystemDefault);

			// If not set, default to files in System32 (SysWOW64) in %WINDIR% (GetSystemDirectory)
			// If set but invalid, show errors.
			Item<std::vector<std::filesystem::path>> ChainLoadPath_d3d11 = CreateConfigItem<std::vector<std::filesystem::path>>(this, "ChainLoadPath_d3d11");
			Item<std::vector<std::filesystem::path>> ChainLoadPath_dxgi = CreateConfigItem<std::vector<std::filesystem::path>>(this, "ChainLoadPath_dxgi");
			Item<std::vector<std::filesystem::path>> ChainLoadPath_d3d9 = CreateConfigItem<std::vector<std::filesystem::path>>(this, "ChainLoadPath_d3d9");
			Item<std::vector<std::filesystem::path>> ChainLoadPath_dinput8 = CreateConfigItem<std::vector<std::filesystem::path>>(this, "ChainLoadPath_dinput8");

			//// https://github.com/goatcorp/Dalamud/blob/master/Dalamud/DalamudStartInfo.cs
			//Item<bool> ChainLoadDalamud_Enable = CreateConfigItem(this, "ChainLoadDalamud_Enable", false);
			//Item<int> ChainLoadDalamud_WaitMs = CreateConfigItem(this, "ChainLoadDalamud_WaitMs", 0);
			//Item<std::filesystem::path> ChainLoadDalamud_WorkingDirectory = CreateConfigItem<std::filesystem::path>(this, "ChainLoadDalamud_WorkingDirectory");
			//// Following 4 paths are relative to ChainLoadDalamud_WorkingDirectory.
			//Item<std::filesystem::path> ChainLoadDalamud_ConfigurationPath = CreateConfigItem<std::filesystem::path>(this, "ChainLoadDalamud_ConfigurationPath");
			//Item<std::filesystem::path> ChainLoadDalamud_PluginDirectory = CreateConfigItem<std::filesystem::path>(this, "ChainLoadDalamud_PluginDirectory");
			//Item<std::filesystem::path> ChainLoadDalamud_DefaultPluginDirectory = CreateConfigItem<std::filesystem::path>(this, "ChainLoadDalamud_DefaultPluginDirectory");
			//Item<std::filesystem::path> ChainLoadDalamud_AssetDirectory = CreateConfigItem<std::filesystem::path>(this, "ChainLoadDalamud_AssetDirectory");
			//Item<bool> ChainLoadDalamud_OptOutMbCollection = CreateConfigItem<bool>(this, "ChainLoadDalamud_OptOutMbCollection", true);
			// ^ TODO: this doesn't work at the moment - see also AutoLoadAsDependencyModule.cpp

			Item<bool> UseResourceOverriding = CreateConfigItem(this, "UseResourceOverriding", true);
			Item<bool> UseDefaultTexToolsModPackSearchDirectory = CreateConfigItem(this, "UseDefaultTexToolsModPackSearchDirectory", true);
			Item<std::vector<std::filesystem::path>> AdditionalTexToolsModPackSearchDirectories =
				CreateConfigItem<std::vector<std::filesystem::path>>(this, "AdditionalTexToolsModPackSearchDirectories");
			Item<bool> UseDefaultGameResourceFileEntryRootDirectory = CreateConfigItem(this, "UseDefaultGameResourceFileEntryRootDirectory", true);
			Item<std::vector<std::filesystem::path>> AdditionalGameResourceFileEntryRootDirectories =
				CreateConfigItem<std::vector<std::filesystem::path>>(this, "AdditionalGameResourceFileEntryRootDirectories");

			[[nodiscard]] WORD GetLangId() const;
			[[nodiscard]] LPCWSTR GetStringRes(UINT uId) const;
			template <typename ... Args>
			[[nodiscard]] std::wstring FormatStringRes(UINT uId, Args ... args) const {
				return std::format(GetStringRes(uId), std::forward<Args>(args)...);
			}
			[[nodiscard]] std::wstring GetLanguageNameLocalized(Sqex::Language gameLanguage) const;
			[[nodiscard]] std::wstring GetRegionNameLocalized(Sqex::Region gameRegion) const;
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

		static std::filesystem::path TranslatePath(const std::filesystem::path& path, const std::filesystem::path& relativeTo  ={});
		static std::filesystem::path EnsureDirectory(const std::filesystem::path&);

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
