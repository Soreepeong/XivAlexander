#pragma once

#include <concepts>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "Utils/Win32/LoadedModule.h"

namespace XivAlexander::Game::Signatures {
	class Address {
		const void* m_pointer{};

	public:
		Address() = default;

		Address(const void* pointer)
			: m_pointer(pointer) {}

		template<typename T> requires std::is_pointer_v<T>
		operator T() const {
			return reinterpret_cast<T>(const_cast<void*>(m_pointer));
		}

		[[nodiscard]] const void* Get() const { return m_pointer; }

		explicit operator bool() const { return !!m_pointer; }

		bool operator==(const Address& r) const = default;
	};

	[[nodiscard]] std::string to_string(const void* pointer);
	[[nodiscard]] std::string to_string(Address address);

	template<typename T>
	concept HasToString = requires(const T& value) {
		{ to_string(value) } -> std::convertible_to<std::string>;
	};

	template<typename T>
	struct IsDescribable : std::bool_constant<std::is_pointer_v<T> || std::is_arithmetic_v<T> || std::is_enum_v<T> || HasToString<T>> {};

	template<typename T>
	struct IsDescribable<std::optional<T>> : IsDescribable<T> {};

	template<typename T, typename TAllocator>
	struct IsDescribable<std::vector<T, TAllocator>> : IsDescribable<T> {};

	template<typename T>
	concept Describable = IsDescribable<T>::value;

	template<typename T>
	[[nodiscard]] std::string DescribeContainer(const std::optional<T>& value);

	template<typename T, typename TAllocator>
	[[nodiscard]] std::string DescribeContainer(const std::vector<T, TAllocator>& values);

	template<typename T>
	[[nodiscard]] std::string Describe(const T& value) {
		if constexpr (std::is_pointer_v<T>)
			return to_string(reinterpret_cast<const void*>(value));
		else if constexpr (std::is_same_v<T, bool>)
			return value ? "true" : "false";
		else if constexpr (std::is_arithmetic_v<T>)
			return std::format("0x{:X}", value);
		else if constexpr (std::is_enum_v<T>)
			return std::format("{}", static_cast<std::underlying_type_t<T>>(value));
		else if constexpr (HasToString<T>)
			return to_string(value);
		else
			return DescribeContainer(value);
	}

	template<typename T>
	std::string DescribeContainer(const std::optional<T>& value) {
		return value ? Describe(*value) : "none";
	}

	template<typename T, typename TAllocator>
	std::string DescribeContainer(const std::vector<T, TAllocator>& values) {
		std::string result = std::format("{} [", values.size());
		for (size_t i = 0; i < values.size(); i++) {
			if (i)
				result += ", ";
			result += Describe(values[i]);
		}
		return result + "]";
	}

	enum class ResolveError {
		Ok,
		NotFound,
		Ambiguous,
		Invalid,
		Mismatch,
		Dependency,
	};

	struct ResolveStatus {
		ResolveError Code = ResolveError::Ok;
		std::string Detail;

		bool operator==(ResolveError code) const { return Code == code; }
	};

	/// What a resolver is given to look into the module with; defined in Game/ResolveContext.h, which only the
	/// signature definitions include.
	class ResolveContext;

	class ComplexSignatureBase {
		std::string m_name;

	public:
		explicit ComplexSignatureBase(std::string_view name);
		ComplexSignatureBase(const ComplexSignatureBase&) = delete;
		ComplexSignatureBase(ComplexSignatureBase&&) = delete;
		ComplexSignatureBase& operator=(const ComplexSignatureBase&) = delete;
		ComplexSignatureBase& operator=(ComplexSignatureBase&&) = delete;
		virtual ~ComplexSignatureBase();

		[[nodiscard]] std::string_view Name() const { return m_name; }

		[[nodiscard]] virtual ResolveStatus Check(const Utils::Win32::LoadedModule& module = Utils::Win32::LoadedModule::MainModule()) const = 0;

		/// \returns Every complex signature defined.
		static std::vector<const ComplexSignatureBase*> All();

	protected:
		using InvokeFn = void(*)(const ComplexSignatureBase& self, ResolveContext& ctx, void* out);

		/// Runs a resolver against \p module, turning whatever it throws into the status returned.
		ResolveStatus RunResolver(const Utils::Win32::LoadedModule& module, InvokeFn invoke, void* out) const;

		void LogResolved(const Utils::Win32::LoadedModule& module, const std::string& description) const;
		void LogFailed(const Utils::Win32::LoadedModule& module, const ResolveStatus& status) const;
	};

	/// A value derived from the game's code, with how it is derived kept next to the signatures it is derived from.
	/// The resolver runs once per module; its value or its failure is kept for later.
	template<typename T>
	class ComplexSignature final : public ComplexSignatureBase {
		static_assert(Describable<T>, "a complex signature's value must be a pointer, a number, an enum, an optional or vector of those, or have a to_string");

	public:
		using ResolverFn = T(*)(ResolveContext& ctx);

	private:
		using CachedResult = std::variant<T, ResolveStatus>;

		const ResolverFn m_resolver;
		mutable std::mutex m_mtx;
		mutable std::map<HMODULE, CachedResult> m_cache;

	public:
		ComplexSignature(std::string_view name, ResolverFn resolver)
			: ComplexSignatureBase(name)
			, m_resolver(resolver) {}

		ResolveStatus Resolve(T& out, const Utils::Win32::LoadedModule& module = Utils::Win32::LoadedModule::MainModule()) const {
			const auto& result = Cached(module);
			if (const auto value = std::get_if<T>(&result)) {
				out = *value;
				return {};
			}
			return std::get<ResolveStatus>(result);
		}

		ResolveStatus Check(const Utils::Win32::LoadedModule& module = Utils::Win32::LoadedModule::MainModule()) const override {
			const auto& result = Cached(module);
			if (std::holds_alternative<T>(result))
				return {};
			return std::get<ResolveStatus>(result);
		}

	private:
		friend class ResolveContext;

		const CachedResult& Cached(const Utils::Win32::LoadedModule& module) const {
			{
				std::lock_guard lock(m_mtx);
				if (const auto it = m_cache.find(*module); it != m_cache.end())
					return it->second;
			}

			auto result = Run(module);
			std::unique_lock lock(m_mtx);
			const auto [it, inserted] = m_cache.try_emplace(*module, std::move(result));
			lock.unlock();

			if (inserted) {
				if (const auto value = std::get_if<T>(&it->second))
					LogResolved(module, Describe(*value));
				else
					LogFailed(module, std::get<ResolveStatus>(it->second));
			}
			return it->second;
		}

		CachedResult Run(const Utils::Win32::LoadedModule& module) const {
			std::optional<T> value;
			auto status = RunResolver(module, &Invoke, &value);
			if (status == ResolveError::Ok)
				return CachedResult(std::in_place_index<0>, std::move(*value));
			return CachedResult(std::in_place_index<1>, std::move(status));
		}

		static void Invoke(const ComplexSignatureBase& self, ResolveContext& ctx, void* out) {
			static_cast<std::optional<T>*>(out)->emplace(static_cast<const ComplexSignature&>(self).m_resolver(ctx));
		}
	};
}
