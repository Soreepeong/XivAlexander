#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
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

		virtual void OnItemChange() {}

		[[nodiscard]] virtual const ConfigItemBase* AsItem() const { return nullptr; }

	public:
		ConfigNode(const ConfigNode&) = delete;
		ConfigNode& operator=(const ConfigNode&) = delete;
		virtual ~ConfigNode() = default;

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

		virtual void LoadFrom(const nlohmann::json&) = 0;
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

	template<typename T, bool = std::is_trivially_copyable_v<T>>
	struct ConfigValueIsAtomic : std::false_type {};

	template<typename T>
	struct ConfigValueIsAtomic<T, true> : std::bool_constant<std::atomic<T>::is_always_lock_free> {};

	/// Thread-safe: values are read and written whole, either as a lock-free atomic or as an immutable snapshot.
	template<typename T>
	class ConfigItem : public ConfigItemBase {
		friend class BaseConfigRepository;

		static constexpr bool IsAtomic = ConfigValueIsAtomic<T>::value;

		std::conditional_t<IsAtomic, std::atomic<T>, std::atomic<std::shared_ptr<const T>>> m_value;
		std::mutex m_writeMtx;
		const std::function<T(T)> m_sanitizer;

		static auto MakeStorage(const T& value) {
			if constexpr (IsAtomic)
				return value;
			else
				return std::make_shared<const T>(value);
		}

		void Store(T value) {
			{
				const auto lock = std::lock_guard(m_writeMtx);
				if constexpr (IsAtomic) {
					if (!(m_value.load() != value))
						return;
					m_value.store(value);
				} else {
					if (!(*m_value.load() != value))
						return;
					m_value.store(std::make_shared<const T>(std::move(value)));
				}
			}
			TriggerOnChange();
		}

	protected:
		void LoadFrom(const nlohmann::json& data) override;
		void SaveTo(nlohmann::json& data) const override;

	public:
		ConfigItem(ConfigNode* pParent, const char* pszName);
		ConfigItem(ConfigNode* pParent, const char* pszName, const T& defaultValue);
		ConfigItem(ConfigNode* pParent, const char* pszName, const T& defaultValue, std::function<T(const T&)> validator);

		~ConfigItem() override = default;

		template<typename = std::enable_if_t<!std::is_base_of_v<ConfigItemBase, T>>>
		ConfigItem& operator=(const T& rv) {
			Store(m_sanitizer(rv));
			return *this;
		}

		template<typename = std::enable_if_t<!std::is_base_of_v<ConfigItemBase, T>>>
		ConfigItem& operator=(T&& rv) {
			Store(m_sanitizer(std::move(rv)));
			return *this;
		}

		[[nodiscard]] operator T() const & {
			return Value();
		}

		/// Returns a copy, so that another thread assigning a new value cannot invalidate it.
		[[nodiscard]] T Value() const {
			if constexpr (IsAtomic)
				return m_value.load();
			else
				return *m_value.load();
		}

		/// Returns the current value without copying it; it stays unchanged even if a new value gets assigned.
		[[nodiscard]] std::shared_ptr<const T> Snapshot() const requires (!IsAtomic) {
			return m_value.load();
		}

		template<typename = std::enable_if_t<std::is_same_v<T, bool>>>
		ConfigItem& Toggle() {
			{
				const auto lock = std::lock_guard(m_writeMtx);
				m_value.store(!m_value.load());
			}
			TriggerOnChange();
			return *this;
		}

		template<typename = std::enable_if_t<std::is_same_v<T, bool>>>
		[[nodiscard]] xivres::util::on_dtor AddAndCallOnBoolChange(std::function<void()> onTrue, std::function<void()> onFalse) {
			if (!onTrue)
				onTrue = [] {};

			if (!onFalse)
				onFalse = [] {};

			if (Value())
				onTrue();
			else
				onFalse();

			auto cb = [this, onTrue = std::move(onTrue), onFalse] {
				if (Value())
					onTrue();
				else
					onFalse();
			};
			auto uncb = [this, onFalse = std::move(onFalse)] {
				if (Value())
					onFalse();
			};
			return OnChange(std::move(cb), std::move(uncb));
		}
	};

	/// Items kept in an object under the group's key; the group's OnChange fires once per item change, or once per Batch.
	class ConfigGroup : public ConfigItemBase, public ConfigNode {
		size_t m_batchDepth = 0;
		bool m_changedInBatch = false;

	protected:
		void LoadFrom(const nlohmann::json& data) override;
		void SaveTo(nlohmann::json& data) const override;
		void OnItemChange() override;
		[[nodiscard]] const ConfigItemBase* AsItem() const override { return this; }

	public:
		ConfigGroup(ConfigNode* pParent, const char* pszKey);
		~ConfigGroup() override = default;

		/// Defers the group's OnChange and saving until the returned object is destroyed; items' own OnChange still fire immediately.
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

		const std::filesystem::path m_sConfigPath;
		const std::string m_parentKey;

		const std::shared_ptr<Misc::Logger> m_logger;

		[[nodiscard]] std::string FindParentKey(const nlohmann::json& totalConfig) const;

	protected:
		xivres::util::on_dtor::multi m_cleanup;

		/// Upgrades this repository's object as read from the file; called before loading and before saving.
		virtual void Migrate(nlohmann::json& config) const {}

		/// Moves config[from] to where item is kept now, unless a value is already there.
		static void MoveKey(nlohmann::json& config, const char* from, const ConfigItemBase& item);

	public:
		BaseConfigRepository(std::filesystem::path path, std::string parentKey);
		virtual ~BaseConfigRepository();

		[[nodiscard]] auto Loaded() const { return m_loaded; }

		xivres::util::on_dtor WithSuppressSave();

		void Save(const std::filesystem::path& to = {});
		virtual void Reload(const std::filesystem::path& from = {});

		[[nodiscard]] auto GetConfigPath() const { return m_sConfigPath; }
	};

	template<typename T>
	ConfigItem<T>::ConfigItem(ConfigNode* pParent, const char* pszName)
		: ConfigItem(pParent, pszName, T{}) {}

	template<typename T>
	ConfigItem<T>::ConfigItem(ConfigNode* pParent, const char* pszName, const T& defaultValue)
		: ConfigItemBase(pParent, pszName)
		, m_value(MakeStorage(defaultValue))
		, m_sanitizer([](T v) { return std::move(v); }) {
		m_pBaseRepository->m_cleanup += OnChange([pRepository = m_pBaseRepository] { pRepository->Save(); });
	}

	template<typename T>
	ConfigItem<T>::ConfigItem(ConfigNode* pParent, const char* pszName, const T& defaultValue, std::function<T(const T&)> validator)
		: ConfigItemBase(pParent, pszName)
		, m_value(MakeStorage(defaultValue))
		, m_sanitizer(validator) {
		m_pBaseRepository->m_cleanup += OnChange([pRepository = m_pBaseRepository] { pRepository->Save(); });
	}

	template<typename T>
	struct IsStdVector : std::false_type {};
	template<typename E, typename A>
	struct IsStdVector<std::vector<E, A>> : std::true_type {};

	// uint16_t is written as hex; its specializations are in the source file.
	template<typename T>
	void ConfigItem<T>::LoadFrom(const nlohmann::json& data) {
		if (const auto it = data.find(Name); it != data.end()) {
			std::optional<T> newValue;
			try {
				if constexpr (IsStdVector<T>::value) {
					// One invalid entry drops only itself; dropping the list would also erase it from the file on the next save.
					if (it->is_array()) {
						T list;
						for (const auto& element : *it) {
							try {
								list.push_back(element.template get<typename T::value_type>());
							} catch (...) {
#ifdef _DEBUG
								throw;
#endif
							}
						}
						newValue = std::move(list);
					} else {
						newValue = it->get<T>();
					}
				} else {
					newValue = it->get<T>();
				}
			} catch (...) {
				// TODO: show how the value is invalid
#ifdef _DEBUG
				throw;
#endif
			}
			if (newValue)
				*this = std::move(*newValue);
		}
	}

	template<typename T>
	void ConfigItem<T>::SaveTo(nlohmann::json& data) const {
		data[Name] = Value();
	}


	template<>
	void ConfigItem<uint16_t>::LoadFrom(const nlohmann::json& data);
	template<>
	void ConfigItem<uint16_t>::SaveTo(nlohmann::json& data) const;
}
