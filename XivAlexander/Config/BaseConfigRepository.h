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
	template<typename T>
	class ConfigItem;

	class ConfigItemBase {
		friend class BaseConfigRepository;
		template<typename T>
		friend class ConfigItem;

		BaseConfigRepository* const m_pBaseRepository;

	protected:
		ConfigItemBase(BaseConfigRepository* pRepository, const char* pszName);

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
		ConfigItem(BaseConfigRepository* pRepository, const char* pszName);
		ConfigItem(BaseConfigRepository* pRepository, const char* pszName, const T& defaultValue);
		ConfigItem(BaseConfigRepository* pRepository, const char* pszName, const T& defaultValue, std::function<T(const T&)> validator);

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

	class BaseConfigRepository {
		friend class ConfigItemBase;
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

		std::vector<ConfigItemBase*> m_allItems;

	protected:
		xivres::util::on_dtor::multi m_cleanup;

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
	ConfigItem<T>::ConfigItem(BaseConfigRepository* pRepository, const char* pszName)
		: ConfigItem(pRepository, pszName, T{}) {}

	template<typename T>
	ConfigItem<T>::ConfigItem(BaseConfigRepository* pRepository, const char* pszName, const T& defaultValue)
		: ConfigItemBase(pRepository, pszName)
		, m_value(std::move(defaultValue))
		, m_sanitizer([](T v) { return std::move(v); }) {
		pRepository->m_cleanup += OnChange([pRepository] { pRepository->Save(); });
	}

	template<typename T>
	ConfigItem<T>::ConfigItem(BaseConfigRepository* pRepository, const char* pszName, const T& defaultValue, std::function<T(const T&)> validator)
		: ConfigItemBase(pRepository, pszName)
		, m_value(std::move(defaultValue))
		, m_sanitizer(validator) {
		pRepository->m_cleanup += OnChange([pRepository] { pRepository->Save(); });
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
