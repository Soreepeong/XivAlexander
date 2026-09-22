#pragma once

#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "Game/ComplexSignature.h"
#include "Game/Signatures.h"
#include "Utils/Win32/LoadedModule.h"

namespace XivAlexander::Game::Signatures {
	class ResolveFailure : public std::exception {
	public:
		ResolveStatus Status;

		ResolveFailure(ResolveError code, std::string detail)
			: Status{code, std::move(detail)} {}

		[[nodiscard]] const char* what() const noexcept override { return Status.Detail.c_str(); }
	};

	class ResolveContext {
		const Utils::Win32::LoadedModule& m_module;

		explicit ResolveContext(const Utils::Win32::LoadedModule& module)
			: m_module(module) {}

		friend class ComplexSignatureBase;

	public:
		ResolveContext(const ResolveContext&) = delete;
		ResolveContext& operator=(const ResolveContext&) = delete;

		[[nodiscard]] const Utils::Win32::LoadedModule& Module() const { return m_module; }

		[[nodiscard]] std::span<const uint8_t> Image() const;
		[[nodiscard]] std::span<const uint8_t> Section(std::string_view name) const;
		[[nodiscard]] std::span<const uint8_t> Text() const { return Section(".text"); }

		[[nodiscard]] bool InSection(const void* p, std::string_view section) const;
		const void* RequireInSection(const void* p, std::string_view section, std::string_view what) const;

		[[nodiscard]] std::span<const uint8_t> FunctionContaining(const void* p, std::string_view what) const;
		[[nodiscard]] std::span<const uint8_t> FunctionStartingAt(const void* p, std::string_view what) const;
		[[nodiscard]] std::span<const uint8_t> FunctionStartingAt(const void* p) const;

		[[nodiscard]] ScanResult Unique(const RegexSignature& signature, std::span<const uint8_t> data, std::string_view what) const;
		[[nodiscard]] ScanResult First(const RegexSignature& signature, std::span<const uint8_t> data, std::string_view what) const;
		[[nodiscard]] std::optional<ScanResult> Find(const RegexSignature& signature, std::span<const uint8_t> data) const;
		[[nodiscard]] std::vector<ScanResult> All(const RegexSignature& signature, std::span<const uint8_t> data) const;
		[[nodiscard]] ScanResult MatchAt(const RegexSignature& signature, std::span<const uint8_t> data, std::string_view what) const;
		[[nodiscard]] std::optional<ScanResult> TryMatchAt(const RegexSignature& signature, std::span<const uint8_t> data) const;

		[[nodiscard]] std::vector<const void*> Vtable(const void* const* vtable, size_t maxSlots, std::string_view what) const;

		template<typename T>
		T InRange(T value, T lo, T hi, std::string_view what) const {
			if (value < lo || hi <= value)
				Fail(ResolveError::Invalid, std::format("{} {:#x} is not within [{:#x}, {:#x})", what, value, lo, hi));
			return value;
		}

		template<typename... TArgs>
		void Require(bool condition, ResolveError code, std::format_string<TArgs...> format, TArgs&&... args) const {
			if (!condition)
				Fail(code, std::format(format, std::forward<TArgs>(args)...));
		}

		[[noreturn]] static void Fail(ResolveError code, std::string detail);

		template<typename T>
		const T& Get(const ComplexSignature<T>& dependency) const {
			const auto& result = dependency.Cached(m_module);
			if (const auto value = std::get_if<T>(&result))
				return *value;
			Fail(ResolveError::Dependency, std::get<ResolveStatus>(result).Detail);
		}

		template<typename T>
		const T* TryGet(const ComplexSignature<T>& dependency) const {
			return std::get_if<T>(&dependency.Cached(m_module));
		}

		template<typename TPredicate>
		auto UniqueSlot(std::span<const void* const> slots, std::string_view what, TPredicate&& predicate) const {
			using TValue = std::invoke_result_t<TPredicate&, size_t, std::span<const uint8_t>>::value_type;
			std::optional<std::pair<size_t, TValue>> found;
			for (size_t i = 0; i < slots.size(); i++) {
				auto value = predicate(i, FunctionStartingAt(slots[i]));
				if (!value)
					continue;
				if (found)
					Fail(ResolveError::Ambiguous, std::format("{}: vtable slots {} and {} both match", what, found->first, i));
				found.emplace(i, std::move(*value));
			}
			if (!found)
				Fail(ResolveError::NotFound, std::format("{}: none of {} vtable slots match", what, slots.size()));
			return std::move(*found);
		}

		template<typename TPredicate>
		size_t UniqueSlotIndex(std::span<const void* const> slots, std::string_view what, TPredicate&& predicate) const {
			return UniqueSlot(slots, what, [&predicate](size_t i, std::span<const uint8_t> fn) -> std::optional<std::monostate> {
				if (predicate(i, fn))
					return std::monostate{};
				return std::nullopt;
			}).first;
		}
	};
}
