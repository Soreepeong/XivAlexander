#pragma once

#include <optional>
#include <string>

#include <nlohmann/json_fwd.hpp>
#include <srell.hpp>

namespace XivAlexander {
	struct PathReplacementRule {
		std::string From;
		std::string To;
		bool Stop = true;

		[[nodiscard]] const srell::u8cregex& Regex() const;

		bool operator==(const PathReplacementRule& r) const;
		bool operator!=(const PathReplacementRule& r) const { return !operator==(r); }

	private:
		mutable std::optional<srell::u8cregex> m_regex;
	};

	void to_json(nlohmann::json&, const PathReplacementRule&);
	void from_json(const nlohmann::json&, PathReplacementRule&);

	struct LogPathFilter {
		std::string Pattern;
		bool Include = true;

		[[nodiscard]] const srell::u8cregex& Regex() const;

		bool operator==(const LogPathFilter& r) const;
		bool operator!=(const LogPathFilter& r) const { return !operator==(r); }

	private:
		mutable std::optional<srell::u8cregex> m_regex;
	};

	void to_json(nlohmann::json&, const LogPathFilter&);
	void from_json(const nlohmann::json&, LogPathFilter&);
}
