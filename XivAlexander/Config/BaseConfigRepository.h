#pragma once

#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <xivres/util.listener_manager.h>
#include <xivres/util.on_dtor.h>

namespace XivAlexander {
	namespace Misc {
		class Logger;
	}

	class Config;
	class BaseConfigRepository;
	class ConfigGroup;
	class ConfigItemBase;
	template<typename T>
	class ConfigItem;

	/// What config items are declared in: a repository, or a group.
	class ConfigNode {
		friend class BaseConfigRepository;
		friend class ConfigGroup;
		friend class ConfigItemBase;

		BaseConfigRepository* const m_pRepository;
		std::vector<ConfigItemBase*> m_items;

	protected:
		explicit ConfigNode(BaseConfigRepository* pRepository) : m_pRepository(pRepository) {}

		void LoadItemsFrom(const nlohmann::json& data);
		void SaveItemsTo(nlohmann::json& data) const;

		/// Called after an item declared in this node changed.
		virtual void OnItemChange() {}

		/// Gets this node as an item, if it is a group.
		[[nodiscard]] virtual const ConfigItemBase* AsItem() const { return nullptr; }

	public:
		ConfigNode(const ConfigNode&) = delete;
		ConfigNode& operator=(const ConfigNode&) = delete;
		virtual ~ConfigNode() = default;

		/// The items and groups declared in this node, in declaration order.
		[[nodiscard]] const std::vector<ConfigItemBase*>& Items() const { return m_items; }
	};

	class ConfigItemBase {
		friend class BaseConfigRepository;
		friend class ConfigNode;
		friend class ConfigGroup;
		template<typename T>
		friend class ConfigItem;

		ConfigNode* const m_pParent;
		BaseConfigRepository* const m_pBaseRepository;

	protected:
		ConfigItemBase(ConfigNode* pParent, const char* pszName);

		virtual bool LoadFrom(const nlohmann::json&) = 0;
		virtual void SaveTo(nlohmann::json&) const = 0;

		void TriggerOnChange();

	public:
		ConfigItemBase(const ConfigItemBase&) = delete;
		ConfigItemBase& operator=(const ConfigItemBase&) = delete;
		ConfigItemBase(ConfigItemBase&&) = delete;
		ConfigItemBase& operator=(ConfigItemBase&&) = delete;
		virtual ~ConfigItemBase() = default;

		const char* const Name;
		xivres::util::listener_manager<ConfigItemBase, void> OnChange;
		[[nodiscard]] xivres::util::on_dtor AddAndCallOnChange(std::function<void()> cb, std::function<void()> onUnbind = {});
	};

	template<typename T>
	class ConfigItem : public ConfigItemBase {
		friend class BaseConfigRepository;

		T m_value;
		const std::function<T(T)> m_sanitizer;

	protected:
		bool LoadFrom(const nlohmann::json& data) override;
		void SaveTo(nlohmann::json& data) const override;

	public:
		ConfigItem(ConfigNode* pParent, const char* pszName);
		ConfigItem(ConfigNode* pParent, const char* pszName, const T& defaultValue);
		ConfigItem(ConfigNode* pParent, const char* pszName, const T& defaultValue, std::function<T(const T&)> validator);

		~ConfigItem() override = default;

		template<typename = std::enable_if_t<!std::is_base_of_v<ConfigItemBase, T>>>
		ConfigItem& operator=(const T& rv) {
			auto sanitized = m_sanitizer(rv);
			if (m_value != sanitized) {
				m_value = std::move(sanitized);
				TriggerOnChange();
			}
			return *this;
		}

		template<typename = std::enable_if_t<!std::is_base_of_v<ConfigItemBase, T>>>
		ConfigItem& operator=(T&& rv) {
			auto sanitized = m_sanitizer(std::move(rv));
			if (m_value != sanitized) {
				m_value = std::move(sanitized);
				TriggerOnChange();
			}
			return *this;
		}

		[[nodiscard]] operator T() const & {
			return m_value;
		}

		[[nodiscard]] const T& Value() const {
			return m_value;
		}

		template<typename = std::enable_if_t<std::is_same_v<T, bool>>>
		ConfigItem& Toggle() {
			m_value = !m_value;
			TriggerOnChange();
			return *this;
		}

		template<typename = std::enable_if_t<std::is_same_v<T, bool>>>
		[[nodiscard]] xivres::util::on_dtor AddAndCallOnBoolChange(std::function<void()> onTrue, std::function<void()> onFalse) {
			if (!onTrue)
				onTrue = [] {};

			if (!onFalse)
				onFalse = [] {};

			if (m_value)
				onTrue();
			else
				onFalse();

			auto cb = [this, onTrue = std::move(onTrue), onFalse] {
				if (m_value)
					onTrue();
				else
					onFalse();
			};
			auto uncb = [this, onFalse = std::move(onFalse)] {
				if (m_value)
					onFalse();
			};
			return OnChange(std::move(cb), std::move(uncb));
		}
	};

	/// Config items declared together, and kept in an object of the group's key. Each item can be subscribed to on its
	/// own; the group's OnChange is called once after any of them changed, or once per Batch.
	class ConfigGroup : public ConfigItemBase, public ConfigNode {
		size_t m_batchDepth = 0;
		bool m_changedInBatch = false;

	protected:
		bool LoadFrom(const nlohmann::json& data) override;
		void SaveTo(nlohmann::json& data) const override;
		void OnItemChange() override;
		[[nodiscard]] const ConfigItemBase* AsItem() const override { return this; }

	public:
		ConfigGroup(ConfigNode* pParent, const char* pszKey);
		~ConfigGroup() override = default;

		/// Defers the group's OnChange, and saving, until the returned object is destroyed. Items' own OnChange are
		/// still called as each changes.
		[[nodiscard]] xivres::util::on_dtor Batch();
	};

	class BaseConfigRepository : public ConfigNode {
		friend class ConfigItemBase;
		friend class ConfigGroup;
		template<typename T>
		friend class ConfigItem;

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

		[[nodiscard]] std::string FindParentKey(const nlohmann::json& totalConfig) const;

	protected:
		xivres::util::on_dtor::multi m_cleanup;

		/// Brings this repository's object, as it was read from the file, up to date: before loading, and before saving.
		virtual void Migrate(nlohmann::json& config) const {}

		/// Moves the value at key from of config to where item is kept now, unless there is one there already.
		static void MoveKey(nlohmann::json& config, const char* from, const ConfigItemBase& item);

	public:
		BaseConfigRepository(__in_opt const Config* pConfig, std::filesystem::path path, std::string parentKey);
		virtual ~BaseConfigRepository();

		[[nodiscard]] auto Loaded() const { return m_loaded; }

		xivres::util::on_dtor WithSuppressSave();

		void Save(const std::filesystem::path& to = {});
		virtual void Reload(const std::filesystem::path& from = {});

		[[nodiscard]] auto GetConfigPath() const { return m_sConfigPath; }
	};

	// The constructors reach into the repository, so they are defined once it is complete.
	template<typename T>
	ConfigItem<T>::ConfigItem(ConfigNode* pParent, const char* pszName)
		: ConfigItem(pParent, pszName, T{}) {}

	template<typename T>
	ConfigItem<T>::ConfigItem(ConfigNode* pParent, const char* pszName, const T& defaultValue)
		: ConfigItemBase(pParent, pszName)
		, m_value(std::move(defaultValue))
		, m_sanitizer([](T v) { return std::move(v); }) {
		m_pBaseRepository->m_cleanup += OnChange([pRepository = m_pBaseRepository] { pRepository->Save(); });
	}

	template<typename T>
	ConfigItem<T>::ConfigItem(ConfigNode* pParent, const char* pszName, const T& defaultValue, std::function<T(const T&)> validator)
		: ConfigItemBase(pParent, pszName)
		, m_value(std::move(defaultValue))
		, m_sanitizer(validator) {
		m_pBaseRepository->m_cleanup += OnChange([pRepository = m_pBaseRepository] { pRepository->Save(); });
	}

	// In the header so that any repository's translation unit can instantiate them.
	// uint16_t is written as hex and so has its own, over in the source file.
	template<typename T>
	bool ConfigItem<T>::LoadFrom(const nlohmann::json& data) {
		if (const auto it = data.find(Name); it != data.end()) {
			T newValue;
			try {
				newValue = it->get<T>();
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
	void ConfigItem<T>::SaveTo(nlohmann::json& data) const {
		data[Name] = m_value;
	}


	template<>
	bool ConfigItem<uint16_t>::LoadFrom(const nlohmann::json& data);
	template<>
	void ConfigItem<uint16_t>::SaveTo(nlohmann::json& data) const;
}
