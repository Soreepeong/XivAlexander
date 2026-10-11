#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>
#include <srell.hpp>

namespace XivAlexander {
	/// A case-insensitive regex compiled on first use; copies share the compiled one, so copy it with its pattern.
	class LazyRegex {
		mutable std::atomic<std::shared_ptr<const srell::u8cregex>> m_regex;

	public:
		LazyRegex() = default;
		LazyRegex(const LazyRegex& r) : m_regex(r.m_regex.load()) {}
		LazyRegex& operator=(const LazyRegex& r) {
			m_regex.store(r.m_regex.load());
			return *this;
		}

		[[nodiscard]] const srell::u8cregex& Get(const std::string& pattern) const;
	};

	struct PathReplacementRule {
		bool Enabled = true;
		std::string From;
		std::string To;
		bool Stop = true;

		[[nodiscard]] const srell::u8cregex& Regex() const;

		bool operator==(const PathReplacementRule& r) const;
		bool operator!=(const PathReplacementRule& r) const { return !operator==(r); }

	private:
		LazyRegex m_regex;
	};

	void to_json(nlohmann::json&, const PathReplacementRule&);
	void from_json(const nlohmann::json&, PathReplacementRule&);

	struct LogPathFilter {
		bool Enabled = true;
		std::string Pattern;
		bool Include = true;

		[[nodiscard]] const srell::u8cregex& Regex() const;

		bool operator==(const LogPathFilter& r) const;
		bool operator!=(const LogPathFilter& r) const { return !operator==(r); }

	private:
		LazyRegex m_regex;
	};

	void to_json(nlohmann::json&, const LogPathFilter&);
	void from_json(const nlohmann::json&, LogPathFilter&);

	/// Voice language (the voice files' suffix: ja, en, ...) of a cutscene speaker by name; an empty name stands for anyone unnamed.
	struct ForcedCharacterLanguage {
		bool Enabled = true;
		std::string Name;
		std::string Language;

		bool operator==(const ForcedCharacterLanguage&) const = default;
	};

	void to_json(nlohmann::json&, const ForcedCharacterLanguage&);
	void from_json(const nlohmann::json&, ForcedCharacterLanguage&);

	/// A listed file or folder that can be turned off without removing it; read from a plain path string too (enabled).
	struct ToggledPath {
		bool Enabled = true;
		std::filesystem::path Path;

		bool operator==(const ToggledPath&) const = default;

		[[nodiscard]] static std::vector<std::filesystem::path> EnabledOf(const std::vector<ToggledPath>& list);
	};

	void to_json(nlohmann::json&, const ToggledPath&);
	void from_json(const nlohmann::json&, ToggledPath&);
}
