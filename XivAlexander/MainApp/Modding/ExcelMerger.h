#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <xivres/common.h>

namespace xivres {
	class packed_stream;
}

namespace xivres::sqpack {
	class reader;
}

namespace XivAlexander::Apps::MainApp::Window {
	class ProgressPopupWindow;
}

namespace XivAlexander::Apps::MainApp::Features::Modding {
	class MergedText;

	/// Provides another regional client's languages for this client's sheets, keeping this client's own cells except localizable strings.
	class ExcelMerger {
	public:
		struct Source {
			std::filesystem::path GameRoot;
			std::shared_ptr<const xivres::sqpack::reader> Reader;
		};

		struct Options {
			// The language the game runs in; rows start from it, and cells kept are kept in it.
			xivres::game_language BaseLanguage = xivres::game_language::Unspecified;
			// For pages the base language lacks.
			std::vector<xivres::game_language> FallbackLanguages;
			// Languages to add besides those a sheet declares but the client does not have (the Korean client declares only Korean).
			std::vector<xivres::game_language> Languages;
			bool AllowContentMatching = false;
			bool FillPluralColumns = true;
			// Override files in order: {"<sheet regex>": {"<string index>": <source string index|null|[steps]>, "*": [steps], "keep": [...], "unkeep": [...]}}.
			std::vector<nlohmann::ordered_json> Overrides;
		};

		struct PassThrough {
			size_t SourceIndex{};
			std::string Path;
		};

		struct Generated {
			std::string Path;
			std::vector<char> Data;
		};

		struct Output {
			std::vector<PassThrough> PassThroughs;
			std::vector<Generated> Generated;
			nlohmann::json Report = nlohmann::json::object();
			size_t FastPathSheets{};
			size_t GeneratedSheets{};
			size_t SkippedSheets{};
			bool Cancelled = false;
		};

		struct Callbacks {
			std::function<void(bool warning, const std::string& message)> Log;
			std::function<void(size_t done, size_t total)> Progress;
			std::function<bool()> Cancelled;
		};

		/// Decides and produces the files of every sheet; thread-safe readers are read from several threads.
		static Output Run(const xivres::sqpack::reader& target, const std::vector<Source>& sources, const Options& options, const Callbacks& callbacks);

		/// The entry as stored in the reader's data files, under its full path.
		static std::shared_ptr<xivres::packed_stream> MakePassThroughStream(const xivres::sqpack::reader& reader, const std::string& path);

		/// Name text in the files that differs from the target's in the base language; null when there is none.
		static std::shared_ptr<const MergedText> CollectMergedText(const xivres::sqpack::reader& target, xivres::game_language baseLanguage, const std::vector<std::shared_ptr<xivres::packed_stream>>& files);

		/// Runs the merge for the running client when enabled, using and updating the cache; empty when disabled, cancelled, or failed.
		static std::vector<std::shared_ptr<xivres::packed_stream>> SetUp(const std::filesystem::path& sqpackPath, Window::ProgressPopupWindow& progressWindow);
	};
}
