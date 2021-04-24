#pragma once
namespace App {
	class ConfigRepository;
	template<typename T>
	class ConfigItem;

	class ConfigItemBase {
		friend class ConfigRepository;
		template<typename T>
		friend class ConfigItem;
		const char* m_pszName;

		ConfigItemBase(ConfigRepository* pRepository, const char* pszName);
		ConfigItemBase(const ConfigRepository&) = delete;
		ConfigItemBase(ConfigRepository&&) = delete;
		ConfigItemBase& operator =(const ConfigItemBase&) = delete;
		ConfigItemBase& operator =(ConfigItemBase&&) = delete;

		virtual bool LoadFrom(const nlohmann::json&, bool announceChanged = false) = 0;
		virtual void SaveTo(nlohmann::json&) const = 0;

	protected:
		void AnnounceChanged() {
			OnChangeListener(*this);
		}

	public:
		const char* Name() const;

		Utils::ListenerManager<ConfigItemBase, void, ConfigItemBase&> OnChangeListener;
	};

	template<typename T>
	class ConfigItem : public ConfigItemBase {
		friend class ConfigRepository;
		T m_value;
		const std::function<T(const T&)> m_fnValidator;

		ConfigItem(ConfigRepository* pRepository, const char* pszName, T defaultValue)
			: ConfigItemBase(pRepository, pszName)
			, m_value(defaultValue)
		    , m_fnValidator(nullptr) {
		}

		ConfigItem(ConfigRepository* pRepository, const char* pszName, T defaultValue, std::function<T(const T&)> validator)
			: ConfigItemBase(pRepository, pszName)
			, m_value(defaultValue)
			, m_fnValidator(validator) {
		}

		bool Assign(const T& rv) {
			const auto sanitized = m_fnValidator ? m_fnValidator(rv) : rv;
			m_value = sanitized;
			return sanitized == rv;
		}
		 
		virtual bool LoadFrom(const nlohmann::json& data, bool announceChanged = false) override;

		virtual void SaveTo(nlohmann::json& data) const override;

	public:
		const T& operator =(const T& rv) {
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

	class ConfigRepository {
		friend class ConfigItemBase;
		static std::unique_ptr<ConfigRepository> s_pConfig;

		const uint16_t InvalidIpcType = 0x93DB;

		const std::string m_sGamePath;
		const std::wstring m_sConfigPath;

		std::vector<ConfigItemBase*> m_allItems;
		std::vector<Utils::CallOnDestruction> m_destructionCallbacks;

		bool m_bSuppressSave = false;

	public:
		// Set defaults so that the values will never be a valid IPC code.
		// Assumes structure doesn't change too often.
		// Will be loaded from configuration file on initialization.
		ConfigItem<uint16_t> S2C_ActionEffects[5]{
			{this, "SkillResultResponse01", InvalidIpcType },
			{this, "SkillResultResponse08", InvalidIpcType },
			{this, "SkillResultResponse16", InvalidIpcType },
			{this, "SkillResultResponse24", InvalidIpcType },
			{this, "SkillResultResponse32", InvalidIpcType },
		};
		ConfigItem<uint16_t> S2C_ActorControl{ this, "ActorControl", InvalidIpcType };
		ConfigItem<uint16_t> S2C_ActorControlSelf{ this, "ActorControlSelf", InvalidIpcType };
		ConfigItem<uint16_t> S2C_ActorCast{ this, "ActorCast", InvalidIpcType };
		ConfigItem<uint16_t> S2C_AddStatusEffect{ this, "AddStatusEffect", InvalidIpcType };
		ConfigItem<uint16_t> C2S_ActionRequest[2]{
			{this, "RequestUseAction", InvalidIpcType},
			{this, "RequestUseAction2", InvalidIpcType},
		};

		// Miscellaneous configuration
		ConfigItem<bool> AlwaysOnTop{ this, "AlwaysOnTop", false };
		ConfigItem<bool> UseHighLatencyMitigation{ this, "UseHighLatencyMitigation", true };
		ConfigItem<bool> UseHighLatencyMitigationLogging{ this, "UseHighLatencyMitigationLogging", true };
		ConfigItem<bool> ReducePacketDelay{ this, "ReducePacketDelay", true };
		ConfigItem<bool> UseLatencyCorrection{ this, "UseLatencyCorrection", true };
		ConfigItem<int> BaseLatencyPenalty{ this, "BaseLatencyPenalty", 1, [](int newValue) {
			return std::min(60, std::max(1, newValue));
		} };
		ConfigItem<bool> UseOpcodeFinder{ this, "UseOpcodeFinder", false };
		ConfigItem<bool> UseEffectApplicationDelayLogger{ this, "UseEffectApplicationDelayLogger", false };
		ConfigItem<bool> UseAutoAdjustingExtraDelay{ this, "UseAutoAdjustingExtraDelay", true };
		ConfigItem<bool> ShowLoggingWindow{ this, "ShowLoggingWindow", false };
		ConfigItem<bool> ShowControlWindow{ this, "ShowControlWindow", false };
		ConfigItem<std::string> GameServerIpRange{ this, "GameServerIpRange",
			"124.150.157.0/24,"  // Japanese
			"195.82.50.0/24,"    // European
			"204.2.229.0/24,"    // North American
			"183.111.189.3/24,"  // Korean
			"127.0.0.0/8,"       // Loopback
			"10.0.0.0/8,"        // Private range A
			"172.16.0.0/12,"     // Private range B
			"192.168.0.0/16,"    // Private range C
		};
		ConfigItem<std::string> GameServerPortRange{ this, "GameServerPortRange", "10000,10001-65535" };
		ConfigItem<bool> UseAllIpcMessageLogger{ this, "UseAllIpcMessageLogger", false };

		ConfigRepository();

		void Save();
		void Reload(bool announceChange = false);
		void SetQuitting();

		static std::string GetGamePath();
		static std::wstring GetConfigPath();

		static ConfigRepository& Config();
		static void DestroyConfig();
	};
}
