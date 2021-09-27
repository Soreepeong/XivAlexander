#include "pch.h"
#include "App_Misc_VirtualSqPacks.h"

#include <XivAlexanderCommon/Sqex_EscapedString.h>
#include <XivAlexanderCommon/Sqex_Excel_Generator.h>
#include <XivAlexanderCommon/Sqex_Excel_Reader.h>
#include <XivAlexanderCommon/Sqex_FontCsv_Creator.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Creator.h>
#include <XivAlexanderCommon/Sqex_Sqpack_EntryProvider.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>
#include <XivAlexanderCommon/Sqex_ThirdParty_TexTools.h>
#include <XivAlexanderCommon/Utils_Win32_Process.h>
#include <XivAlexanderCommon/Utils_Win32_ThreadPool.h>
#include <XivAlexanderCommon/XivAlex.h>

#include "App_ConfigRepository.h"
#include "App_Misc_ExcelTransformConfig.h"
#include "App_Misc_Logger.h"
#include "App_Window_ProgressPopupWindow.h"
#include "DllMain.h"
#include "resource.h"

struct App::Misc::VirtualSqPacks::Implementation {
	const std::shared_ptr<Config> m_config;
	const std::shared_ptr<Logger> m_logger;
	const std::filesystem::path m_sqpackPath;

	static constexpr int PathTypeIndex = -1;
	static constexpr int PathTypeIndex2 = -2;
	static constexpr int PathTypeInvalid = -3;

	std::map<std::filesystem::path, Sqex::Sqpack::Creator::SqpackViews> m_sqpackViews;
	std::map<HANDLE, std::unique_ptr<OverlayedHandleData>> m_overlayedHandles;
	std::set<std::filesystem::path> m_ignoredIndexFiles;

	struct TtmpSet {
		std::filesystem::path path;
		Sqex::ThirdParty::TexTools::TTMPL ttmpl;
		Utils::Win32::File ttmpd;
		nlohmann::json config;
	};

	std::vector<TtmpSet> m_ttmps;

	Implementation(std::filesystem::path sqpackPath)
		: m_config(Config::Acquire())
		, m_logger(Logger::Acquire())
		, m_sqpackPath(std::move(sqpackPath)) {

		const auto actCtx = Dll::ActivationContext().With();
		Window::ProgressPopupWindow progressWindow(Dll::FindGameMainWindow(false));
		progressWindow.UpdateMessage("Discovering files...");
		progressWindow.Show();

		std::map<std::filesystem::path, std::unique_ptr<Sqex::Sqpack::Creator>> creators;
		for (const auto& expac : std::filesystem::directory_iterator(m_sqpackPath)) {
			if (!expac.is_directory())
				continue;

			for (const auto& sqpack : std::filesystem::directory_iterator(expac)) {
				if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
					throw std::runtime_error("Cancelled");

				auto ext = sqpack.path().extension().wstring();
				CharLowerW(&ext[0]);
				if (ext != L".index")
					continue;

				creators.emplace(sqpack, std::make_unique<Sqex::Sqpack::Creator>(
					Utils::ToUtf8(expac.path().filename().wstring()),
					Utils::ToUtf8(sqpack.path().filename().replace_extension().replace_extension().wstring())
				));
			}
		}

		{
			std::vector<std::filesystem::path> ttmpls;
			std::vector<std::filesystem::path> dirs;
			if (m_config->Runtime.UseDefaultTexToolsModPackSearchDirectory) {
				dirs.emplace_back(m_sqpackPath / "TexToolsMods");
				dirs.emplace_back(m_config->Init.ResolveConfigStorageDirectoryPath() / "TexToolsMods");
			}

			for (const auto& dir : m_config->Runtime.AdditionalTexToolsModPackSearchDirectories.Value()) {
				if (!dir.empty())
					dirs.emplace_back(Config::TranslatePath(dir));
			}

			for (const auto& dir : dirs) {
				if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
					throw std::runtime_error("Cancelled");
				if (dir.empty() || !is_directory(dir))
					continue;

				try {
					for (const auto& iter : std::filesystem::recursive_directory_iterator(dir)) {
						if (iter.path().filename() != "TTMPL.mpl")
							continue;
						ttmpls.emplace_back(iter);
					}
				} catch (const std::exception& e) {
					m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider,
						"=> Failed to list items in {}: {}",
						dir, e.what());
					continue;
				}
			}

			std::ranges::sort(ttmpls);
			for (const auto& ttmpl : ttmpls) {
				if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
					throw std::runtime_error("Cancelled");

				try {
					m_ttmps.emplace_back(
						ttmpl,
						Sqex::ThirdParty::TexTools::TTMPL::FromStream(Sqex::FileRandomAccessStream{Utils::Win32::File::Create(ttmpl, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0)}),
						Utils::Win32::File::Create(ttmpl.parent_path() / "TTMPD.mpd", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0)
					);
				} catch (const std::exception& e) {
					m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider,
						"Failed to load TexTools ModPack from {}: {}", ttmpl.wstring(), e.what());
					continue;
				}
				if (const auto choicesPath = ttmpl.parent_path() / "choices.json"; exists(choicesPath)) {
					try {
						m_ttmps.back().config = Utils::ParseJsonFromFile(choicesPath);
						m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider,
							"Choices file loaded from {}", choicesPath.wstring());
					} catch (const std::exception& e) {
						m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider,
							"Failed to load choices from {}: {}", choicesPath.wstring(), e.what());
					}
				}
			}
		}

		if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
			throw std::runtime_error("Cancelled");

		{
			const auto progressMax = creators.size() * (0
				+ 1 // original sqpack
				+ m_config->Runtime.AdditionalSqpackRootDirectories.Value().size() // external sqpack
				+ m_ttmps.size() // TTMP
				+ 1 // replacement file entry
			);
			std::atomic_size_t progressValue = 0;

			Utils::Win32::TpEnvironment pool;
			const auto loaderThread = Utils::Win32::Thread(L"VirtualSqPack Constructor", [&]() {
				for (const auto& indexFile : creators | std::views::keys) {
					pool.SubmitWork([&, &creator = *creators.at(indexFile)]() {
						try {
							if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
								return;

							progressValue += 1;
							if (const auto result = creator.AddEntriesFromSqPack(indexFile, true, true);
								!result.Added.empty() || !result.Replaced.empty()) {
								m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider,
									"=> Processed SqPack {}/{}: Added {}, replaced {}, ignored {}, error {}",
									creator.DatExpac, creator.DatName,
									result.Added.size(), result.Replaced.size(), result.SkippedExisting.size(), result.Error.size());
								for (const auto& error : result.Error) {
									m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider,
										"\t=> Error processing {}: {}", error.first, error.second);
								}
							}

							if (creator.DatExpac != "ffxiv" || creator.DatName != "0a0000") {
								for (const auto& additionalSqpackRootDirectory : m_config->Runtime.AdditionalSqpackRootDirectories.Value()) {
									if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
										return;

									progressValue += 1;

									const auto file = additionalSqpackRootDirectory / "sqpack" / indexFile.parent_path().filename() / indexFile.filename();
									if (!exists(file))
										continue;

									const auto batchAddResult = creator.AddEntriesFromSqPack(file, false, false);
									if (!batchAddResult.AnyItem())
										continue;

									m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider,
										"=> Processed external SqPack {}: Added {}",
										file, batchAddResult.Added.size());
									for (const auto& error : batchAddResult.Error) {
										m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider,
											"\t=> Error processing {}: {}", error.first, error.second);
									}
								}
							} else
								progressValue += m_config->Runtime.AdditionalSqpackRootDirectories.Value().size();

							for (const auto& ttmp : m_ttmps) {
								if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
									return;

								progressValue += 1;

								const auto batchAddResult = creator.AddEntriesFromTTMP(ttmp.ttmpl, ttmp.ttmpd, ttmp.config);
								if (!batchAddResult.AnyItem())
									continue;

								m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider,
									"=> Processed TTMPL {}: Added {}, replaced {}",
									ttmp.path, batchAddResult.Added.size(), batchAddResult.Replaced.size());
								for (const auto& error : batchAddResult.Error) {
									m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider,
										"\t=> Error processing {}: {}", error.first, error.second);
								}
							}

							SetUpVirtualFileFromFileEntries(creator, indexFile);
							progressValue += 1;
						} catch (const std::exception& e) {
							pool.Cancel();
							m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider,
								"\t=> Error processing {}: {}", indexFile.wstring(), e.what());
						}
					});
				}
				pool.WaitOutstanding();
			});

			progressWindow.UpdateMessage("Indexing files...");
			progressWindow.UpdateProgress(0, progressMax);
			while (WAIT_TIMEOUT == progressWindow.DoModalLoop(100, {loaderThread})) {
				progressWindow.UpdateProgress(progressValue, progressMax);
			}
			pool.Cancel();
			loaderThread.Wait();
		}

		if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
			throw std::runtime_error("Cancelled");

		for (const auto& [indexFile, pCreator] : creators) {
			if (pCreator->DatExpac == "ffxiv" && pCreator->DatName == "000000")
				SetUpGeneratedFonts(progressWindow, *pCreator, indexFile);
			else if (pCreator->DatExpac == "ffxiv" && pCreator->DatName == "0a0000")
				SetUpMergedExd(progressWindow, *pCreator, indexFile);
			else {
				progressWindow.UpdateMessage("Finalizing...");
				progressWindow.UpdateProgress(0, 0);
			}
			m_sqpackViews.emplace(indexFile, pCreator->AsViews(false));
		}
	}

	void SetUpMergedExd(Window::ProgressPopupWindow& progressWindow, Sqex::Sqpack::Creator& creator, const std::filesystem::path& indexFile) {
		const auto region = std::get<0>(XivAlex::ResolveGameReleaseRegion());

		const auto cachedDir = m_config->Init.ResolveConfigStorageDirectoryPath() / "Cached" / region / creator.DatExpac / creator.DatName;

		std::map<std::string, int> exhTable;
		// maybe generate exl?

		for (const auto& pair : Sqex::Excel::ExlReader(*creator["exd/root.exl"]))
			exhTable.emplace(pair);

		std::string currentCacheKeys;
		{
			const auto gameRoot = indexFile.parent_path().parent_path().parent_path();
			const auto versionFile = Utils::Win32::File::Create(gameRoot / "ffxivgame.ver", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
			const auto versionContent = versionFile.Read<char>(0, static_cast<size_t>(versionFile.GetLength()));
			currentCacheKeys += std::format("SQPACK:{}:{}\n", canonical(gameRoot).wstring(), std::string(versionContent.begin(), versionContent.end()));
		}

		currentCacheKeys += "LANG";
		for (const auto& lang : m_config->Runtime.GetFallbackLanguageList()) {
			currentCacheKeys += std::format(":{}", static_cast<int>(lang));
		}
		currentCacheKeys += "\n";

		std::vector<std::unique_ptr<Sqex::Sqpack::Reader>> readers;
		for (const auto& additionalSqpackRootDirectory : m_config->Runtime.AdditionalSqpackRootDirectories.Value()) {
			const auto file = additionalSqpackRootDirectory / "sqpack" / indexFile.parent_path().filename() / indexFile.filename();
			if (!exists(file))
				continue;

			const auto versionFile = Utils::Win32::File::Create(additionalSqpackRootDirectory / "ffxivgame.ver", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
			const auto versionContent = versionFile.Read<char>(0, static_cast<size_t>(versionFile.GetLength()));
			currentCacheKeys += std::format("SQPACK:{}:{}\n", canonical(additionalSqpackRootDirectory).wstring(), std::string(versionContent.begin(), versionContent.end()));

			readers.emplace_back(std::make_unique<Sqex::Sqpack::Reader>(file));
		}

		for (const auto& configFile : m_config->Runtime.ExcelTransformConfigFiles.Value()) {
			uint8_t hash[20]{};
			try {
				const auto file = Utils::Win32::File::Create(configFile, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
				CryptoPP::SHA1 sha1;
				const auto content = file.Read<uint8_t>(0, static_cast<size_t>(file.GetLength()));
				sha1.Update(content.data(), content.size());
				sha1.Final(reinterpret_cast<byte*>(hash));
			} catch (...) {
			}

			CryptoPP::HexEncoder encoder;
			encoder.Put(hash, sizeof hash);
			encoder.MessageEnd();

			std::string buf(static_cast<size_t>(encoder.MaxRetrievable()), 0);
			encoder.Get(reinterpret_cast<byte*>(&buf[0]), buf.size());

			currentCacheKeys += std::format("CONF:{}:{}\n", configFile.wstring(), buf);
		}

		auto needRecreate = true;
		try {
			const auto file = Utils::Win32::File::Create(cachedDir / "sources", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
			const auto content = file.Read<char>(0, static_cast<size_t>(file.GetLength()));
			needRecreate = !std::ranges::equal(content, currentCacheKeys);
		} catch (...) {
			// pass
		}

		if (needRecreate) {
			create_directories(cachedDir);

			std::vector<std::pair<std::regex, ExcelTransformConfig::PluralColumns>> pluralColumns;
			struct ReplacementRule {
				std::regex exhNamePattern;
				std::regex stringPattern;
				std::vector<Sqex::Language> sourceLanguage;
				std::string replaceTo;
				std::vector<size_t> columnIndices;
				std::map<Sqex::Language, std::vector<std::string>> preprocessReplacements;
				std::vector<std::string> postprocessReplacements;
			};
			std::map<Sqex::Language, std::vector<ReplacementRule>> rowReplacementRules;
			std::map<std::string, std::pair<std::wregex, std::wstring>> columnReplacementTemplates;
			auto replacementFileParseFail = false;
			for (auto configFile : m_config->Runtime.ExcelTransformConfigFiles.Value()) {
				configFile = m_config->TranslatePath(configFile);
				if (configFile.empty())
					continue;

				try {
					ExcelTransformConfig::Config transformConfig;
					from_json(Utils::ParseJsonFromFile(configFile), transformConfig);

					for (const auto& entry : transformConfig.pluralMap) {
						pluralColumns.emplace_back(std::regex(entry.first, std::regex_constants::ECMAScript | std::regex_constants::icase), entry.second);
					}
					for (const auto& entry : transformConfig.replacementTemplates) {
						const auto pattern = Utils::FromUtf8(entry.second.from);
						const auto flags = static_cast<std::regex_constants::syntax_option_type>(std::regex_constants::ECMAScript | (entry.second.icase ? std::regex_constants::icase : 0));
						columnReplacementTemplates.emplace(entry.first, std::make_pair(
							std::wregex(pattern, flags), Utils::FromUtf8(entry.second.to)
						));
					}
					for (const auto& rule : transformConfig.rules) {
						for (const auto& targetGroupName : rule.targetGroups) {
							for (const auto& target : transformConfig.targetGroups.at(targetGroupName).columnIndices) {
								rowReplacementRules[transformConfig.targetLanguage].emplace_back(ReplacementRule{
									std::regex(target.first, std::regex_constants::ECMAScript | std::regex_constants::icase),
									std::regex(rule.stringPattern, std::regex_constants::ECMAScript | std::regex_constants::icase),
									transformConfig.sourceLanguages,
									rule.replaceTo,
									target.second,
									rule.preprocessReplacements,
									rule.postprocessReplacements,
								});
							}
						}
					}
				} catch (const std::exception& e) {
					m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider,
						"=> Error occurred while parsing excel transformation configuration file {}: {}",
						configFile.wstring(), e.what());
					replacementFileParseFail = true;
				}
			}
			if (replacementFileParseFail) {
				m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider,
					"=> Skipping merged 0a0000 file generation");
				return;
			}

			// No external excel files, nor any replacement rules are provided.
			if (readers.empty() && rowReplacementRules.empty())
				return;

			const auto actCtx = Dll::ActivationContext().With();
			Utils::Win32::TpEnvironment pool;
			progressWindow.UpdateMessage(Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_TITLE_GENERATING_EXD_FILES)));

			static constexpr auto ProgressMaxPerTask = 1000;
			std::map<std::string, uint64_t> progressPerTask;
			for (const auto& exhName : exhTable | std::views::keys)
				progressPerTask.emplace(exhName, 0);
			progressWindow.UpdateProgress(0, 1ULL * exhTable.size() * ProgressMaxPerTask);
			{
				const auto ttmpl = Utils::Win32::File::Create(cachedDir / "TTMPL.mpl.tmp", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0);
				const auto ttmpd = Utils::Win32::File::Create(cachedDir / "TTMPD.mpd.tmp", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0);
				uint64_t ttmplPtr = 0, ttmpdPtr = 0;
				std::mutex writeMtx;

				const auto compressThread = Utils::Win32::Thread(L"CompressThread", [&]() {
					for (const auto& exhName : exhTable | std::views::keys) {
						pool.SubmitWork([&]() {
							try {
								if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
									return;

								// Step. Calculate maximum progress
								size_t progressIndex = 0;
								uint64_t currentProgressMax = 0;
								uint64_t currentProgress = 0;
								auto& progressStoreTarget = progressPerTask.at(exhName);
								const auto publishProgress = [&]() {
									progressStoreTarget = (1ULL * progressIndex * ProgressMaxPerTask + (currentProgressMax ? currentProgress * ProgressMaxPerTask / currentProgressMax : 0)) / (readers.size() + 2ULL);
								};

								// Step. Load source EXH/D files
								const auto exhPath = Sqex::Sqpack::EntryPathSpec(std::format("exd/{}.exh", exhName));
								std::unique_ptr<Sqex::Excel::Depth2ExhExdCreator> exCreator;
								{
									const auto exhReaderSource = Sqex::Excel::ExhReader(exhName, *creator[exhPath]);
									if (exhReaderSource.Header.Depth != Sqex::Excel::Exh::Depth::Level2) {
										progressStoreTarget = ProgressMaxPerTask;
										return;
									}

									if (std::ranges::find(exhReaderSource.Languages, Sqex::Language::Unspecified) != exhReaderSource.Languages.end()) {
										progressStoreTarget = ProgressMaxPerTask;
										return;
									}

									m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider,
										"=> Merging {}", exhName);

									exCreator = std::make_unique<Sqex::Excel::Depth2ExhExdCreator>(exhName, *exhReaderSource.Columns, exhReaderSource.Header.SomeSortOfBufferSize);
									exCreator->FillMissingLanguageFrom = m_config->Runtime.GetFallbackLanguageList();

									currentProgressMax = 1ULL * exhReaderSource.Languages.size() * exhReaderSource.Pages.size();
									for (const auto language : exhReaderSource.Languages) {
										for (const auto& page : exhReaderSource.Pages) {
											if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
												return;
											currentProgress++;
											publishProgress();

											const auto exdPathSpec = exhReaderSource.GetDataPathSpec(page, language);
											try {
												const auto exdReader = Sqex::Excel::ExdReader(exhReaderSource, creator[exdPathSpec]);
												exCreator->AddLanguage(language);
												for (const auto i : exdReader.GetIds())
													exCreator->SetRow(i, language, exdReader.ReadDepth2(i));
											} catch (const std::out_of_range&) {
												// pass
											} catch (const std::exception& e) {
												throw std::runtime_error(std::format("Error while processing {}: {}", exdPathSpec, e.what()));
											}
										}
									}
									currentProgress = 0;
									progressIndex++;
									publishProgress();
								}

								auto sourceLanguages = exCreator->Languages;

								// Step. Load external EXH/D files
								for (const auto& reader : readers) {
									try {
										const auto exhReaderCurrent = Sqex::Excel::ExhReader(exhName, *(*reader)[exhPath]);

										currentProgressMax = 1ULL * exhReaderCurrent.Languages.size() * exhReaderCurrent.Pages.size();
										for (const auto language : exhReaderCurrent.Languages) {
											for (const auto& page : exhReaderCurrent.Pages) {
												if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
													return;
												currentProgress++;
												publishProgress();

												const auto exdPathSpec = exhReaderCurrent.GetDataPathSpec(page, language);
												try {
													const auto exdReader = Sqex::Excel::ExdReader(exhReaderCurrent, (*reader)[exdPathSpec]);
													exCreator->AddLanguage(language);
													for (const auto i : exdReader.GetIds()) {
														auto row = exdReader.ReadDepth2(i);
														if (row.size() != exCreator->Columns.size()) {
															const auto& rowSet = exCreator->Data.at(i);
															const std::vector<Sqex::Excel::ExdColumn>* referenceRow = nullptr;
															for (const auto& l : exCreator->FillMissingLanguageFrom) {
																if (auto it = rowSet.find(l);
																	it != rowSet.end()) {
																	referenceRow = &it->second;
																	break;
																}
															}
															if (!referenceRow)
																continue;

															// Exceptions for Chinese client are based on speculations.
															if (((region == L"JP" || region == L"KR") && language == Sqex::Language::ChineseSimplified) && exhName == "Fate") {
																auto replacements = std::vector{row[30].String, row[31].String, row[32].String, row[33].String, row[34].String, row[35].String};
																row = *referenceRow;
																for (size_t j = 0; j < replacements.size(); ++j)
																	row[j].String = std::move(replacements[j]);

															} else if (region == L"CN" && language != Sqex::Language::ChineseSimplified && exhName == "Fate") {
																auto replacements = std::vector{row[0].String, row[1].String, row[2].String, row[3].String, row[4].String, row[5].String};
																row = *referenceRow;
																for (size_t j = 0; j < replacements.size(); ++j)
																	row[30 + j].String = std::move(replacements[j]);

															} else {
																for (size_t j = row.size(); j < referenceRow->size(); ++j)
																	row.push_back((*referenceRow)[j]);
																row.resize(referenceRow->size());
															}
														}
														exCreator->SetRow(i, language, std::move(row), false);
													}
												} catch (const std::out_of_range&) {
													// pass
												} catch (const std::exception& e) {
													m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider,
														"=> Skipping {} because of error: {}", exdPathSpec, e.what());
												}
											}
										}
									} catch (const std::out_of_range&) {
										// pass
									}
									currentProgress = 0;
									progressIndex++;
									publishProgress();
								}

								// Step. Ensure that there are no missing rows from externally sourced exd files
								std::vector<uint32_t> idsToRemove;
								for (auto& kv : exCreator->Data) {
									const auto id = kv.first;
									auto& rowSet = kv.second;
									static_assert(std::is_reference_v<decltype(rowSet)>);

									if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
										return;

									// Step. Find which language to use while filling current row if missing in other languages
									const std::vector<Sqex::Excel::ExdColumn>* referenceRow = nullptr;
									for (const auto& l : exCreator->FillMissingLanguageFrom) {
										if (auto it = rowSet.find(l);
											it != rowSet.end()) {
											referenceRow = &it->second;
											break;
										}
									}
									if (!referenceRow) {
										idsToRemove.push_back(id);
										continue;
									}

									// Step. Figure out which columns to not modify and which are modifiable
									std::set<size_t> columnsToModify, columnsToNeverModify;
									try {
										std::vector<Sqex::Excel::ExdColumn>* cols[4] = {
											&rowSet.at(Sqex::Language::Japanese),
											&rowSet.at(Sqex::Language::English),
											&rowSet.at(Sqex::Language::German),
											&rowSet.at(Sqex::Language::French),
										};
										for (size_t i = 0; i < cols[0]->size(); ++i) {
											std::string compareTarget;
											for (size_t j = 0; j < _countof(cols); ++j) {
												if ((*cols[j])[i].Type != Sqex::Excel::Exh::String) {
													columnsToNeverModify.insert(i);
													break;
												} else if (j != 0 && (*cols[0])[i].String != (*cols[j])[i].String) {
													columnsToModify.insert(i);
													break;
												}
											}
										}
									} catch (const std::out_of_range&) {
										// pass
									}
									try {
										std::vector<Sqex::Excel::ExdColumn>& cols = rowSet.at(Sqex::Language::Korean);
										for (size_t i = 0; i < cols.size(); ++i) {
											if (cols[i].Type != Sqex::Excel::Exh::String) {
												columnsToNeverModify.insert(i);

											} else {
												// If it includes a valid UTF-8 byte sequence that is longer than a byte, set as a candidate.
												const auto& s = cols[i].String;
												for (size_t j = 0; j < s.size(); ++j) {
													char32_t charCode;

													if ((s[j] & 0x80) == 0) {
														// pass; single byte
														charCode = s[j];

													} else if ((s[j] & 0xE0) == 0xC0) {
														// 2 bytes
														if (j + 2 > s.size())
															break; // not enough bytes
														if ((s[j + 1] & 0xC0) != 0x80)
															continue;
														charCode = static_cast<char32_t>(
															((s[j + 0] & 0x1F) << 6) |
															((s[j + 1] & 0x3F) << 0)
														);
														j += 1;

													} else if ((s[j] & 0xF0) == 0xE0) {
														// 3 bytes
														if (j + 3 > s.size())
															break; // not enough bytes
														if ((s[j + 1] & 0xC0) != 0x80
															|| (s[j + 2] & 0xC0) != 0x80)
															continue;
														charCode = static_cast<char32_t>(
															((s[j + 0] & 0x0F) << 12) |
															((s[j + 1] & 0x3F) << 6) |
															((s[j + 2] & 0x3F) << 0)
														);
														j += 2;

													} else if ((s[j] & 0xF8) == 0xF0) {
														// 4 bytes
														if (j + 4 > s.size())
															break; // not enough bytes
														if ((s[j + 1] & 0xC0) != 0x80
															|| (s[j + 2] & 0xC0) != 0x80
															|| (s[j + 3] & 0xC0) != 0x80)
															continue;
														charCode = static_cast<char32_t>(
															((s[j + 0] & 0x07) << 18) |
															((s[j + 1] & 0x3F) << 12) |
															((s[j + 2] & 0x3F) << 6) |
															((s[j + 3] & 0x3F) << 0)
														);
														j += 3;

													} else {
														// invalid
														continue;
													}

													if (charCode >= 128) {
														columnsToModify.insert(i);
														break;
													}
												}
											}
										}
									} catch (const std::out_of_range&) {
										// pass
									}

									// Step. Fill missing rows for languages that aren't from source, and restore columns if unmodifiable
									for (const auto& language : exCreator->Languages) {
										if (auto it = rowSet.find(language);
											it == rowSet.end())
											rowSet[language] = *referenceRow;
										else {
											auto& row = it->second;
											for (size_t i = 0; i < row.size(); ++i) {
												if (columnsToNeverModify.find(i) != columnsToNeverModify.end()) {
													row[i] = (*referenceRow)[i];
												}
											}
										}
									}

									// Step. Adjust language data per use config
									std::map<Sqex::Language, std::vector<Sqex::Excel::ExdColumn>> pendingReplacements;
									for (const auto& language : exCreator->Languages) {
										const auto rules = rowReplacementRules.find(language);
										if (rules == rowReplacementRules.end())
											continue;

										auto row = rowSet.at(language);

										for (const auto columnIndex : columnsToModify) {
											for (const auto& rule : rules->second) {
												if (!rule.columnIndices.empty() && std::ranges::find(rule.columnIndices, columnIndex) == rule.columnIndices.end())
													continue;

												if (!std::regex_search(exhName, rule.exhNamePattern))
													continue;

												if (!std::regex_search(row[columnIndex].String, rule.stringPattern))
													continue;

												std::vector p = {std::format("{}:{}", exhName, id)};
												for (const auto ruleSourceLanguage : rule.sourceLanguage) {
													if (const auto it = rowSet.find(ruleSourceLanguage);
														it != rowSet.end()) {

														if (row[columnIndex].Type != Sqex::Excel::Exh::String)
															throw std::invalid_argument(std::format("Column {} of sourceLanguage {} in {} is not a string column", columnIndex, static_cast<int>(ruleSourceLanguage), exhName));

														ExcelTransformConfig::PluralColumns pluralColummIndices;
														for (const auto& entry : pluralColumns) {
															if (std::regex_search(exhName, entry.first)) {
																pluralColummIndices = entry.second;
																break;
															}
														}

														auto readColumnIndex = columnIndex;
														switch (ruleSourceLanguage) {
															case Sqex::Language::English:
																// add stuff if stuff happens(tm)
																break;

															case Sqex::Language::German:
															case Sqex::Language::French:
																// add stuff if stuff happens(tm)
																break;

															case Sqex::Language::Japanese:
															case Sqex::Language::ChineseSimplified:
															case Sqex::Language::ChineseTraditional:
															case Sqex::Language::Korean: {
																if (pluralColummIndices.capitalizedColumnIndex != ExcelTransformConfig::PluralColumns::Index_NoColumn) {
																	if (readColumnIndex == pluralColummIndices.pluralColumnIndex
																		|| readColumnIndex == pluralColummIndices.singularColumnIndex)
																		readColumnIndex = pluralColummIndices.capitalizedColumnIndex;
																} else {
																	if (readColumnIndex == pluralColummIndices.pluralColumnIndex)
																		readColumnIndex = pluralColummIndices.singularColumnIndex;
																}
																break;
															}
														}
														auto s = it->second[readColumnIndex].String;
														if (const auto rules = rule.preprocessReplacements.find(ruleSourceLanguage); rules != rule.preprocessReplacements.end()) {
															Sqex::EscapedString escaped = s;
															auto u16 = Utils::FromUtf8(escaped.FilteredString());
															for (const auto& ruleName : rules->second) {
																const auto& rep = columnReplacementTemplates.at(ruleName);
																u16 = std::regex_replace(u16, rep.first, rep.second);
															}
															escaped.FilteredString(Utils::ToUtf8(u16));
															s = escaped;
														}
														p.emplace_back(std::move(s));
													} else
														p.emplace_back();
												}

												auto allSame = true;
												size_t nonEmptySize = 0;
												size_t lastNonEmptyIndex = 1;
												for (size_t i = 1; i < p.size(); ++i) {
													if (p[i] != p[0])
														allSame = false;
													if (!p[i].empty()) {
														nonEmptySize++;
														lastNonEmptyIndex = i;
													}
												}
												std::string out;
												if (allSame)
													out = p[1];
												else if (nonEmptySize <= 1)
													out = p[lastNonEmptyIndex];
												else {
													switch (p.size()) {
														case 1:
															out = std::format(rule.replaceTo, p[0]);
															break;
														case 2:
															out = std::format(rule.replaceTo, p[0], p[1]);
															break;
														case 3:
															out = std::format(rule.replaceTo, p[0], p[1], p[2]);
															break;
														case 4:
															out = std::format(rule.replaceTo, p[0], p[1], p[2], p[3]);
															break;
														case 5:
															out = std::format(rule.replaceTo, p[0], p[1], p[2], p[3], p[4]);
															break;
														case 6:
															out = std::format(rule.replaceTo, p[0], p[1], p[2], p[3], p[4], p[5]);
															break;
														case 7:
															out = std::format(rule.replaceTo, p[0], p[1], p[2], p[3], p[4], p[5], p[6]);
															break;
														default:
															throw std::invalid_argument("Only up to 7 source languages are supported");
													}
												}
												if (!rule.postprocessReplacements.empty()) {
													Sqex::EscapedString escaped = out;
													auto u16 = Utils::FromUtf8(escaped.FilteredString());
													for (const auto& ruleName : rule.postprocessReplacements) {
														const auto& rep = columnReplacementTemplates.at(ruleName);
														u16 = std::regex_replace(u16, rep.first, rep.second);
													}
													escaped.FilteredString(Utils::ToUtf8(u16));
													out = escaped;
												}
												row[columnIndex].String = Utils::StringTrim(out);
												break;
											}
										}
										pendingReplacements.emplace(language, std::move(row));
									}
									for (auto& pair : pendingReplacements)
										exCreator->SetRow(id, pair.first, pair.second);
								}

								{
									auto compiled = exCreator->Compile();
									m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider,
										"=> Saving {}", exhName);

									currentProgressMax = compiled.size();
									for (auto& kv : compiled) {
										const auto& entryPathSpec = kv.first;
										auto& data = kv.second;
										currentProgress++;
										publishProgress();

										const auto targetPath = cachedDir / entryPathSpec.Original;

										const auto provider = Sqex::Sqpack::MemoryBinaryEntryProvider(entryPathSpec, std::make_shared<Sqex::MemoryRandomAccessStream>(std::move(*reinterpret_cast<std::vector<uint8_t>*>(&data))));
										const auto len = provider.StreamSize();
										const auto dv = provider.ReadStreamIntoVector<char>(0, static_cast<SSIZE_T>(len));

										if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
											return;
										const auto lock = std::lock_guard(writeMtx);
										const auto entryLine = std::format("{}\n", nlohmann::json::object({
											{"FullPath", Utils::ToUtf8(entryPathSpec.Original.wstring())},
											{"ModOffset", ttmpdPtr},
											{"ModSize", len},
											{"DatFile", "0a0000"},
										}).dump());
										ttmplPtr += ttmpl.Write(ttmplPtr, std::span(entryLine));
										ttmpdPtr += ttmpd.Write(ttmpdPtr, std::span(dv));
									}
									currentProgress = 0;
									progressIndex++;
									publishProgress();
								}
							} catch (const std::exception& e) {
								m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, "=> Error: {}", e.what());
								progressWindow.Cancel();
							}
						});
					}
					pool.WaitOutstanding();
				});
				while (WAIT_TIMEOUT == progressWindow.DoModalLoop(100, {compressThread})) {
					uint64_t p = 0;
					for (const auto& val : progressPerTask | std::views::values)
						p += val;
					progressWindow.UpdateProgress(p, progressPerTask.size() * ProgressMaxPerTask);
				}
				pool.Cancel();
				compressThread.Wait();
			}

			if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0) {
				try {
					std::filesystem::remove(cachedDir / "TTMPL.mpl.tmp");
				} catch (...) {
					// whatever
				}
				try {
					std::filesystem::remove(cachedDir / "TTMPD.mpd.tmp");
				} catch (...) {
					// whatever
				}
				return;
			}

			try {
				std::filesystem::remove(cachedDir / "TTMPL.mpl");
			} catch (...) {
				// whatever
			}
			try {
				std::filesystem::remove(cachedDir / "TTMPD.mpd");
			} catch (...) {
				// whatever
			}
			std::filesystem::rename(cachedDir / "TTMPL.mpl.tmp", cachedDir / "TTMPL.mpl");
			std::filesystem::rename(cachedDir / "TTMPD.mpd.tmp", cachedDir / "TTMPD.mpd");
			Utils::Win32::File::Create(cachedDir / "sources", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0)
				.Write(0, currentCacheKeys.data(), currentCacheKeys.size());
		}

		try {
			const auto logCacher = creator.Log([&](const auto& s) {
				m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider, "=> {}", s);
			});
			const auto result = creator.AddEntriesFromTTMP(cachedDir);
		} catch (const std::exception& e) {
			m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, "=> Error: {}", e.what());
		}
	}

	bool SetUpVirtualFileFromFileEntries(Sqex::Sqpack::Creator& creator, const std::filesystem::path& indexPath) {
		auto additionalEntriesFound = false;
		std::vector<std::filesystem::path> dirs;

		if (m_config->Runtime.UseDefaultGameResourceFileEntryRootDirectory) {
			dirs.emplace_back(indexPath.parent_path().parent_path());
			dirs.emplace_back(m_config->Init.ResolveConfigStorageDirectoryPath() / "ReplacementFileEntries");
		}

		for (const auto& dir : m_config->Runtime.AdditionalGameResourceFileEntryRootDirectories.Value()) {
			if (!dir.empty())
				dirs.emplace_back(Config::TranslatePath(dir));
		}

		for (size_t i = 0, i_ = dirs.size(); i < i_; ++i) {
			dirs.emplace_back(dirs[i] / std::format("{}.win32", creator.DatExpac) / creator.DatName);
			dirs[i] = dirs[i] / creator.DatExpac / creator.DatName;
		}

		for (const auto& dir : dirs) {
			if (!is_directory(dir))
				continue;

			std::vector<std::filesystem::path> files;

			try {
				for (const auto& iter : std::filesystem::recursive_directory_iterator(dir)) {
					if (is_directory(iter))
						continue;
					files.emplace_back(iter);
				}
			} catch (const std::exception& e) {
				m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider,
					"=> Failed to list items in {}: {}",
					dir, e.what());
				continue;
			}

			std::ranges::sort(files);
			for (const auto& file : files) {
				if (is_directory(file))
					continue;

				try {
					const auto result = creator.AddEntryFromFile(relative(file, dir), file);
					const auto item = result.AnyItem();
					if (!item)
						throw std::runtime_error("Unexpected error");
					m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider,
						"=> {} file {}: (nameHash={:08x}, pathHash={:08x}, fullPathHash={:08x})",
						result.Added.empty() ? "Replaced" : "Added",
						item->PathSpec().Original,
						item->PathSpec().NameHash,
						item->PathSpec().PathHash,
						item->PathSpec().FullPathHash);
					additionalEntriesFound = true;
				} catch (const std::exception& e) {
					m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider,
						"=> Failed to add file {}: {}",
						file, e.what());
				}
			}
		}
		return additionalEntriesFound;
	}

	void SetUpGeneratedFonts(Window::ProgressPopupWindow& progressWindow, Sqex::Sqpack::Creator& creator, const std::filesystem::path& indexPath) {
		const auto fontConfigPath = m_config->Runtime.OverrideFontConfig.Value();
		if (fontConfigPath.empty())
			return;

		try {
			const auto [region, _] = XivAlex::ResolveGameReleaseRegion();
			const auto cachedDir = m_config->Init.ResolveConfigStorageDirectoryPath() / "Cached" / region / creator.DatExpac / creator.DatName;

			std::string currentCacheKeys;
			{
				const auto gameRoot = indexPath.parent_path().parent_path().parent_path();
				const auto versionFile = Utils::Win32::File::Create(gameRoot / "ffxivgame.ver", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
				const auto versionContent = versionFile.Read<char>(0, static_cast<size_t>(versionFile.GetLength()));
				currentCacheKeys += std::format("SQPACK:{}:{}\n", canonical(gameRoot).wstring(), std::string(versionContent.begin(), versionContent.end()));
			}

			if (const auto& configFile = m_config->Runtime.OverrideFontConfig.Value(); !configFile.empty()) {
				uint8_t hash[20]{};
				try {
					const auto file = Utils::Win32::File::Create(configFile, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
					CryptoPP::SHA1 sha1;
					const auto content = file.Read<uint8_t>(0, static_cast<size_t>(file.GetLength()));
					sha1.Update(content.data(), content.size());
					sha1.Final(reinterpret_cast<byte*>(hash));
				} catch (...) {
				}

				CryptoPP::HexEncoder encoder;
				encoder.Put(hash, sizeof hash);
				encoder.MessageEnd();

				std::string buf(static_cast<size_t>(encoder.MaxRetrievable()), 0);
				encoder.Get(reinterpret_cast<byte*>(&buf[0]), buf.size());

				currentCacheKeys += std::format("CONF:{}:{}\n", configFile, buf);
			}

			auto needRecreate = true;
			try {
				const auto file = Utils::Win32::File::Create(cachedDir / "sources", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
				const auto content = file.Read<char>(0, static_cast<size_t>(file.GetLength()));
				needRecreate = !std::ranges::equal(content, currentCacheKeys);
			} catch (...) {
				// pass
			}

			if (needRecreate) {
				create_directories(cachedDir);

				progressWindow.UpdateMessage(Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_TITLE_GENERATING_FONTS)));

				m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider,
					"=> Generating font per file: {}",
					fontConfigPath.wstring());

				auto cfg = Utils::ParseJsonFromFile(fontConfigPath).get<Sqex::FontCsv::CreateConfig::FontCreateConfig>();

				Sqex::FontCsv::FontSetsCreator fontCreator(cfg, Utils::Win32::Process::Current().PathOf().parent_path());
				while (WAIT_TIMEOUT == progressWindow.DoModalLoop(100, {fontCreator.GetWaitableObject()})) {
					const auto progress = fontCreator.GetProgress();
					progressWindow.UpdateProgress(progress.Progress, progress.Max);

					if (progress.Indeterminate)
						progressWindow.UpdateMessage(std::format("{} (+{})", m_config->Runtime.GetStringRes(IDS_TITLE_GENERATING_FONTS), progress.Indeterminate));
					else
						progressWindow.UpdateMessage(Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_TITLE_GENERATING_FONTS)));
				}
				if (progressWindow.GetCancelEvent().Wait(0) != WAIT_OBJECT_0) {
					progressWindow.UpdateMessage(Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_TITLE_COMPRESSING)));
					Utils::Win32::TpEnvironment pool;
					const auto streams = fontCreator.GetResult().GetAllStreams();

					std::atomic_int64_t progress = 0;
					uint64_t maxProgress = 0;
					for (auto& stream : streams | std::views::values)
						maxProgress += stream->StreamSize() * 2;
					progressWindow.UpdateProgress(progress, maxProgress);

					const auto ttmpl = Utils::Win32::File::Create(cachedDir / "TTMPL.mpl.tmp", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0);
					const auto ttmpd = Utils::Win32::File::Create(cachedDir / "TTMPD.mpd.tmp", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0);
					uint64_t ttmplPtr = 0, ttmpdPtr = 0;
					std::mutex writeMtx;

					const auto compressThread = Utils::Win32::Thread(L"CompressThread", [&]() {
						for (const auto& kv : streams) {
							const auto& entryPathSpec = kv.first;
							const auto& stream = kv.second;

							pool.SubmitWork([&]() {
								if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
									return;

								std::shared_ptr<Sqex::Sqpack::EntryProvider> provider;
								auto extension = entryPathSpec.Original.extension().wstring();
								CharLowerW(&extension[0]);

								if (extension == L".tex")
									provider = std::make_shared<Sqex::Sqpack::MemoryTextureEntryProvider>(entryPathSpec, stream);
								else
									provider = std::make_shared<Sqex::Sqpack::MemoryBinaryEntryProvider>(entryPathSpec, stream);
								const auto len = provider->StreamSize();
								const auto dv = provider->ReadStreamIntoVector<char>(0, static_cast<SSIZE_T>(len));
								progress += stream->StreamSize();

								if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
									return;
								const auto lock = std::lock_guard(writeMtx);
								const auto entryLine = std::format("{}\n", nlohmann::json::object({
									{"FullPath", Utils::ToUtf8(entryPathSpec.Original.wstring())},
									{"ModOffset", ttmpdPtr},
									{"ModSize", len},
									{"DatFile", "000000"},
								}).dump());
								ttmplPtr += ttmpl.Write(ttmplPtr, std::span(entryLine));
								ttmpdPtr += ttmpd.Write(ttmpdPtr, std::span(dv));
								progress += stream->StreamSize();
							});
						}
						pool.WaitOutstanding();
					});

					while (WAIT_TIMEOUT == progressWindow.DoModalLoop(100, {compressThread})) {
						progressWindow.UpdateProgress(progress, maxProgress);
					}
					pool.Cancel();
					compressThread.Wait();
				}

				if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0) {
					try {
						std::filesystem::remove(cachedDir / "TTMPL.mpl.tmp");
					} catch (...) {
						// whatever
					}
					try {
						std::filesystem::remove(cachedDir / "TTMPD.mpd.tmp");
					} catch (...) {
						// whatever
					}
					return;
				}

				try {
					std::filesystem::remove(cachedDir / "TTMPL.mpl");
				} catch (...) {
					// whatever
				}
				try {
					std::filesystem::remove(cachedDir / "TTMPD.mpd");
				} catch (...) {
					// whatever
				}
				std::filesystem::rename(cachedDir / "TTMPL.mpl.tmp", cachedDir / "TTMPL.mpl");
				std::filesystem::rename(cachedDir / "TTMPD.mpd.tmp", cachedDir / "TTMPD.mpd");
				Utils::Win32::File::Create(cachedDir / "sources", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0)
					.Write(0, currentCacheKeys.data(), currentCacheKeys.size());
			}

			try {
				const auto logCacher = creator.Log([&](const auto& s) {
					m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider, "=> {}", s);
				});
				const auto result = creator.AddEntriesFromTTMP(cachedDir);
				if (!result.Added.empty() || !result.Replaced.empty())
					return;
			} catch (const std::exception& e) {
				m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, "=> Error: {}", e.what());
			}
		} catch (const std::exception& e) {
			m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, e.what());
		}
	}
};

App::Misc::VirtualSqPacks::VirtualSqPacks(std::filesystem::path sqpackPath)
	: m_pImpl(std::make_unique<Implementation>(std::move(sqpackPath))) {
}

App::Misc::VirtualSqPacks::~VirtualSqPacks() = default;

HANDLE App::Misc::VirtualSqPacks::Open(const std::filesystem::path& path) {
	try {
		const auto fileToOpen = absolute(path);
		const auto recreatedFilePath = m_pImpl->m_sqpackPath / fileToOpen.parent_path().filename() / fileToOpen.filename();
		const auto indexFile = std::filesystem::path(recreatedFilePath).replace_extension(L".index");
		const auto index2File = std::filesystem::path(recreatedFilePath).replace_extension(L".index2");
		if (!exists(indexFile) || !exists(index2File))
			return nullptr;

		int pathType = Implementation::PathTypeInvalid;

		if (fileToOpen == indexFile) {
			pathType = Implementation::PathTypeIndex;
		} else if (fileToOpen == index2File) {
			pathType = Implementation::PathTypeIndex2;
		} else {
			for (auto i = 0; i < 8; ++i) {
				const auto datFile = std::filesystem::path(recreatedFilePath).replace_extension(std::format(L".dat{}", i));
				if (fileToOpen == datFile) {
					pathType = i;
					break;
				}
			}
		}

		if (pathType == Implementation::PathTypeInvalid)
			return nullptr;

		auto overlayedHandle = std::make_unique<OverlayedHandleData>(Utils::Win32::Event::Create(), fileToOpen, LARGE_INTEGER{}, nullptr);

		for (const auto& view : m_pImpl->m_sqpackViews) {
			if (equivalent(view.first, indexFile)) {
				switch (pathType) {
					case Implementation::PathTypeIndex:
						overlayedHandle->Stream = view.second.Index;
						break;

					case Implementation::PathTypeIndex2:
						overlayedHandle->Stream = view.second.Index2;
						break;

					default:
						if (pathType < 0 || static_cast<size_t>(pathType) >= view.second.Data.size())
							throw std::runtime_error("invalid #");
						overlayedHandle->Stream = view.second.Data[pathType];
				}
				break;
			}
		}

		m_pImpl->m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider,
			"Taking control of {}/{} (parent: {}/{}, type: {})",
			fileToOpen.parent_path().filename(), fileToOpen.filename(),
			indexFile.parent_path().filename(), indexFile.filename(),
			pathType);

		if (!overlayedHandle->Stream)
			return nullptr;

		const auto key = static_cast<HANDLE>(overlayedHandle->IdentifierHandle);
		m_pImpl->m_overlayedHandles.insert_or_assign(key, std::move(overlayedHandle));
		return key;
	} catch (const Utils::Win32::Error& e) {
		m_pImpl->m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, L"CreateFileW: {}, Message: {}", path.wstring(), e.what());
	} catch (const std::exception& e) {
		m_pImpl->m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, "CreateFileW: {}, Message: {}", path.wstring(), e.what());
	}
	return nullptr;
}

bool App::Misc::VirtualSqPacks::Close(HANDLE handle) {
	return m_pImpl->m_overlayedHandles.erase(handle);
}

App::Misc::VirtualSqPacks::OverlayedHandleData* App::Misc::VirtualSqPacks::Get(HANDLE handle) {
	const auto vpit = m_pImpl->m_overlayedHandles.find(handle);
	if (vpit != m_pImpl->m_overlayedHandles.end())
		return vpit->second.get();
	return nullptr;
}

bool App::Misc::VirtualSqPacks::EntryExists(const Sqex::Sqpack::EntryPathSpec& pathSpec) const {
	for (const auto& t : m_pImpl->m_sqpackViews | std::views::values) {
		if (t.EntryOffsets.find(pathSpec) != t.EntryOffsets.end())
			return true;
	}
	return false;
}
