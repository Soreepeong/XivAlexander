#include "pch.h"
#include "MainApp/Modding/ExcelMerger.h"

#include <xivres/packed_stream.standard.h>
#include <xivres/sqpack.reader.h>
#include <xivres/util.sha1.h>

#include "Game/CommandLine.h"
#include "MainApp/Modding/MergedText.h"
#include "MainApp/Modding/StreamTags.h"
#include "MainApp/Windows/ProgressPopupWindow.h"
#include "Misc/GameInstallationDetector.h"
#include "Misc/Logger.h"
#include "Config.h"
#include "resource.h"
#include "Utils/StringUtils.h"
#include "XivAlexander.h"

namespace XivAlexander::Apps::MainApp::Features::Modding {
	namespace {
		// Bump when the output for the same inputs changes.
		constexpr auto CacheFormatVersion = 2;

		using Utils::ToUtf8;

		// Empty if it can't be read.
		std::string ReadFile(const std::filesystem::path& path) {
			std::ifstream in(path, std::ios::binary);
			return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
		}

		std::string ReadTextFile(const std::filesystem::path& path) {
			auto s = ReadFile(path);
			while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' '))
				s.pop_back();
			return s;
		}

		/// ffxivgame.ver, then every exN.ver.
		std::string DescribeVersions(const std::filesystem::path& gameRoot) {
			auto res = std::format("{}={}", ToUtf8(canonical(gameRoot)), ReadTextFile(gameRoot / "ffxivgame.ver"));
			std::error_code ec;
			std::vector<std::filesystem::path> expansions;
			for (const auto& entry : std::filesystem::directory_iterator(gameRoot / "sqpack", ec)) {
				if (entry.is_directory(ec) && entry.path().filename().wstring().starts_with(L"ex"))
					expansions.push_back(entry.path());
			}
			std::ranges::sort(expansions);
			for (const auto& dir : expansions)
				res += std::format(";{}={}", ToUtf8(dir.filename()), ReadTextFile(dir / std::format(L"{}.ver", dir.filename().wstring())));
			return res;
		}

		std::string Sha1Hex(const std::string& data) {
			xivres::util::hash_sha1 sha1;
			sha1.process_bytes(data.data(), data.size());
			uint8_t digest[20];
			sha1.get_digest_bytes(digest);
			std::string res;
			for (const auto b : digest)
				res += std::format("{:02x}", b);
			return res;
		}

		/// The PE header's TimeDateStamp of this DLL, which every build changes.
		uint32_t BuildStamp() {
			const auto base = reinterpret_cast<const uint8_t*>(static_cast<HMODULE>(Dll::Module()));
			const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
			return reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew)->FileHeader.TimeDateStamp;
		}

		/// 0 if it can't be read.
		int64_t LastWriteTicks(const std::filesystem::path& path) {
			std::error_code ec;
			const auto time = std::filesystem::last_write_time(path, ec);
			return ec ? 0 : time.time_since_epoch().count();
		}

		/// The language the game was started in, as the server knows it.
		xivres::game_language DetectClientLanguage() {
			try {
				switch (Misc::GameInstallationDetector::GetGameReleaseInfo().Region) {
					case xivres::game_release_publisher::ShengquGames:
						return xivres::game_language::ChineseSimplified;
					case xivres::game_release_publisher::ActozSoft:
						return xivres::game_language::Korean;
					case xivres::game_release_publisher::UserjoyGames:
						return xivres::game_language::TraditionalChinese;
					default:
						break;
				}
				if (!Dll::GetCommandLineError().empty())
					return xivres::game_language::Unspecified;
				return Game::CommandLine::WellKnown::GetEffectiveLanguage(Game::CommandLine::FromString(Dll::GetGameCommandLine()));
			} catch (...) {
				return xivres::game_language::Unspecified;
			}
		}

		void WriteFileAtomically(const std::filesystem::path& path, std::span<const char> data) {
			const auto temp = std::filesystem::path(path).concat(L".tmp");
			{
				std::ofstream out(temp, std::ios::binary | std::ios::trunc);
				out.write(data.data(), static_cast<std::streamsize>(data.size()));
				if (!out)
					throw std::runtime_error(std::format("failed to write {}", ToUtf8(temp)));
			}
			std::filesystem::rename(temp, path);
		}

		// Kept for the session, so that what the game sends can be told apart from the text in it.
		void PublishMergedText(const xivres::sqpack::reader& target, xivres::game_language baseLanguage, const std::vector<std::shared_ptr<xivres::packed_stream>>& files) {
			const auto logger = Misc::Logger::Acquire();
			try {
				const auto started = std::chrono::steady_clock::now();
				auto merged = ExcelMerger::CollectMergedText(target, baseLanguage, files);
				if (merged) {
					for (const auto& [language, table] : merged->Languages) {
						logger->Format(LogCategory::VirtualSqPacks, "[ExcelMerge] {}: {} merged name text(s), {} with a single base text, kept for outgoing text ({:.1f}s)",
							xivres::game_language_code(language), table.Merged.size(), table.ToBase.size(),
							std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
					}
				}
				MergedText::SetCurrent(std::move(merged));
			} catch (const std::exception& e) {
				logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "[ExcelMerge] Merged names in outgoing text will not be changed back: {}", e.what());
			}
		}

		std::shared_ptr<xivres::packed_stream> MakeGeneratedStream(const std::string& path, std::shared_ptr<xivres::stream> data) {
			data->emplace_tag<SourceNoteTag>("merged excel data");
			return std::make_shared<xivres::passthrough_packed_stream<xivres::standard_passthrough_packer>>(xivres::path_spec(path), std::move(data));
		}
	}

	std::vector<std::shared_ptr<xivres::packed_stream>> ExcelMerger::SetUp(const std::filesystem::path& sqpackPath, Window::ProgressPopupWindow& progressWindow) {
		const auto config = Config::Acquire();
		const auto logger = Misc::Logger::Acquire();
		const auto& rt = config->Runtime;
		MergedText::SetCurrent(nullptr);

		std::vector<std::filesystem::path> roots;
		for (const auto& dir : ToggledPath::EnabledOf(rt.Modding.AdditionalSqpackRootDirectories.Value()))
			roots.emplace_back(Config::TranslatePath(dir));
		if (roots.empty())
			return {};

		const auto verbose = rt.Modding.Logging.ExcelMerge.Value();
		try {
			const auto gameRoot = sqpackPath.parent_path();
			const auto indexName = std::filesystem::path(L"sqpack") / L"ffxiv" / L"0a0000.win32.index";
			const auto target = xivres::sqpack::reader::from_path(gameRoot / indexName);

			Options options;
			options.BaseLanguage = DetectClientLanguage();
			// The RSV keys kept from it must be in the language the server is told.
			if (options.BaseLanguage == xivres::game_language::Unspecified) {
				logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "[ExcelMerge] Skipped, as the game's language isn't known");
				return {};
			}
			options.FallbackLanguages = rt.GetFallbackLanguageList();
			options.AllowContentMatching = true;
			options.FillPluralColumns = true;
			if (const auto display = rt.Modding.Languages.ResourceOverride.Value(); display != xivres::game_language::Unspecified)
				options.Languages.push_back(display);

			auto cacheKey = std::format("format:{}\nbuild:{:08X}\ntarget:{}\nbase:{}\n", CacheFormatVersion, BuildStamp(), DescribeVersions(gameRoot), static_cast<int>(options.BaseLanguage));

			std::vector<Source> sources;
			for (const auto& root : roots) {
				std::error_code ec;
				if (!exists(root / indexName, ec) || equivalent(root, gameRoot, ec))
					continue;
				sources.push_back({root, std::make_shared<xivres::sqpack::reader>(xivres::sqpack::reader::from_path(root / indexName))});
				cacheKey += std::format("source:{}\n", DescribeVersions(root));
			}
			if (sources.empty())
				return {};

			cacheKey += "fallback:";
			for (const auto lang : options.FallbackLanguages)
				cacheKey += std::format("{},", static_cast<int>(lang));
			cacheKey += "\nlanguages:";
			for (const auto lang : options.Languages)
				cacheKey += std::format("{},", static_cast<int>(lang));
			cacheKey += std::format("\ncontent:{}\nplural:{}\nfill:cell\n", options.AllowContentMatching, options.FillPluralColumns);

			for (const auto& file : ToggledPath::EnabledOf(rt.Modding.TextTransformationFiles.Value())) {
				const auto path = Config::TranslatePath(file);
				const auto content = ReadFile(path);
				cacheKey += std::format("override:{}:{}:{}\n", ToUtf8(path), Sha1Hex(content), LastWriteTicks(path));
				try {
					auto parsed = nlohmann::ordered_json::parse(content);
					if (!parsed.is_object())
						throw std::runtime_error("not an object");
					options.Overrides.emplace_back(std::move(parsed));
				} catch (const std::exception& e) {
					logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "[ExcelMerge] Ignoring override file {}: {}", path, e.what());
				}
			}

			const auto cacheDir = config->Init.ResolveConfigStorageDirectoryPath() / L"Cached"
				/ xivres::util::unicode::convert<std::wstring>(Misc::GameInstallationDetector::GetGameReleaseInfo().CountryCode)
				/ L"ffxiv" / L"0a0000" / L"ExcelMerge";
			const auto indexPath = cacheDir / L"index.json";
			const auto dataPath = cacheDir / L"data.bin";
			const auto reportPath = cacheDir / L"report.json";

			std::vector<std::shared_ptr<xivres::packed_stream>> result;

			// Step. Use the cache if it was made from the same inputs.
			try {
				std::ifstream in(indexPath, std::ios::binary);
				if (in) {
					const auto index = nlohmann::json::parse(in);
					if (index.at("key").get<std::string>() == cacheKey) {
						const auto data = std::make_shared<xivres::file_stream>(dataPath);
						for (const auto& p : index.at("passthrough"))
							result.emplace_back(MakePassThroughStream(*sources.at(p.at("source").get<size_t>()).Reader, p.at("path").get<std::string>()));
						for (const auto& g : index.at("generated")) {
							const auto offset = g.at("offset").get<std::streamoff>();
							const auto size = g.at("size").get<std::streamsize>();
							if (offset + size > data->size())
								throw std::runtime_error("data file is truncated");
							result.emplace_back(MakeGeneratedStream(g.at("path").get<std::string>(), std::make_shared<xivres::partial_view_stream>(data, offset, size)));
						}
						logger->Format(LogCategory::VirtualSqPacks, "[ExcelMerge] Using {} cached file(s) from {}", result.size(), cacheDir);
						PublishMergedText(target, options.BaseLanguage, result);
						return result;
					}
				}
			} catch (const std::exception& e) {
				result.clear();
				logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "[ExcelMerge] Cache unusable, merging again: {}", e.what());
			}

			progressWindow.UpdateMessage(xivres::util::unicode::convert<std::string>(rt.GetStringRes(IDS_TITLE_MERGINGEXCEL)));
			progressWindow.UpdateProgress(0, 1);
			Callbacks callbacks;
			callbacks.Log = [logger, verbose](bool warning, const std::string& message) {
				if (warning)
					logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "[ExcelMerge] {}", message);
				else if (verbose)
					logger->Format(LogCategory::VirtualSqPacks, "[ExcelMerge] {}", message);
			};
			callbacks.Progress = [&progressWindow](size_t done, size_t total) { progressWindow.UpdateProgress(done, total); };
			callbacks.Cancelled = [&progressWindow] { return progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0; };

			const auto started = std::chrono::steady_clock::now();
			auto output = Run(target, sources, options, callbacks);
			if (output.Cancelled) {
				logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "[ExcelMerge] Cancelled; no text is merged this session");
				return {};
			}
			logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks,
				"[ExcelMerge] Base language {}: {} sheet(s) passed through, {} generated, {} skipped, in {:.1f}s",
				xivres::game_language_code(options.BaseLanguage), output.FastPathSheets, output.GeneratedSheets, output.SkippedSheets,
				std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());

			// Step. Store the cache; the key last, so that a partial one is never used.
			try {
				create_directories(cacheDir);
				std::error_code ec;
				std::filesystem::remove(indexPath, ec);

				auto index = nlohmann::json::object();
				index["passthrough"] = nlohmann::json::array();
				for (const auto& p : output.PassThroughs)
					index["passthrough"].push_back({{"source", p.SourceIndex}, {"path", p.Path}});
				index["generated"] = nlohmann::json::array();
				std::vector<char> blob;
				for (const auto& g : output.Generated) {
					index["generated"].push_back({{"path", g.Path}, {"offset", blob.size()}, {"size", g.Data.size()}});
					blob.insert(blob.end(), g.Data.begin(), g.Data.end());
				}
				index["key"] = cacheKey;

				auto report = nlohmann::json::object();
				report["baseLanguage"] = xivres::game_language_code(options.BaseLanguage);
				report["sources"] = nlohmann::json::array();
				for (const auto& s : sources)
					report["sources"].push_back(ToUtf8(s.GameRoot));
				report["summary"] = {{"fastPath", output.FastPathSheets}, {"generated", output.GeneratedSheets}, {"skipped", output.SkippedSheets}};
				report["sheets"] = std::move(output.Report);

				WriteFileAtomically(dataPath, blob);
				const auto reportText = report.dump(1);
				WriteFileAtomically(reportPath, reportText);
				const auto indexText = index.dump();
				WriteFileAtomically(indexPath, indexText);
				logger->Format(LogCategory::VirtualSqPacks, "[ExcelMerge] Decisions per sheet written to {}", reportPath);
			} catch (const std::exception& e) {
				logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "[ExcelMerge] Failed to store the cache: {}", e.what());
			}

			for (const auto& p : output.PassThroughs)
				result.emplace_back(MakePassThroughStream(*sources.at(p.SourceIndex).Reader, p.Path));
			for (auto& g : output.Generated) {
				auto bytes = std::vector<uint8_t>(g.Data.begin(), g.Data.end());
				std::vector<char>().swap(g.Data);
				result.emplace_back(MakeGeneratedStream(g.Path, std::make_shared<xivres::memory_stream>(std::move(bytes))));
			}
			PublishMergedText(target, options.BaseLanguage, result);
			return result;
		} catch (const std::exception& e) {
			logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "[ExcelMerge] Failed; no text is merged this session: {}", e.what());
			return {};
		}
	}
}
