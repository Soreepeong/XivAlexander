#include "pch.h"

#include <XivAlexanderCommon/Sqex_Sound_MusicImporter.h>
#include <XivAlexanderCommon/Sqex_Sound_Reader.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>
#include <XivAlexanderCommon/Utils_Win32_ThreadPool.h>

int main() {
	const Sqex::Sqpack::Reader sfxReader(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\070000.win32.index)");
	const Sqex::Sqpack::Reader bgmReaders[4]{
		{LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\0c0000.win32.index)"},
		{LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ex1\0c0100.win32.index)"},
		{LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ex2\0c0200.win32.index)"},
		{LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ex3\0c0300.win32.index)"},
	};

	const std::vector<std::filesystem::path> configFiles{
		LR"(..\StaticData\MusicImportConfig\Before Meteor.json)",
		LR"(..\StaticData\MusicImportConfig\A Realm Reborn.json)",
		LR"(..\StaticData\MusicImportConfig\Before The Fall.json)",
		LR"(..\StaticData\MusicImportConfig\Heavensward.json)",
		LR"(..\StaticData\MusicImportConfig\The Far Edge Of Fate.json)",
		LR"(..\StaticData\MusicImportConfig\Stormblood.json)",
		LR"(..\StaticData\MusicImportConfig\Shadowbringers.json)",
		LR"(..\StaticData\MusicImportConfig\Death Unto Dawn.json)",
		LR"(..\StaticData\MusicImportConfig\Monster Hunter World.json)",
	};
	const std::map<std::string, std::filesystem::path> directories{
		{"Before Meteor", LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 1.0 - Before Meteor)"},
		{"A Realm Reborn", LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 2.0 - A Realm Reborn)"},
		{"Before The Fall", LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 2.5 - Before The Fall)"},
		{"Heavensward", LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 3.0 - Heavensward)"},
		{"The Far Edge Of Fate", LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 3.5 - The Far Edge Of Fate)"},
		{"Stormblood", LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 4.0 - Stormblood)"},
		{"Shadowbringers", LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers)"},
		{"Death Unto Dawn", LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn)"},
		{"Monster Hunter World", LR"(D:\OneDrive\Musics\Sorted by OSTs\Monster Hunter World\Monster Hunter World Original Soundtrack)"},
	};

	std::vector<Sqex::Sound::MusicImportItem> items;
	for (const auto& confFile : configFiles) {
		try {
			Sqex::Sound::MusicImportConfig conf;
			from_json(Utils::ParseJsonFromFile(canonical(confFile)), conf);

			std::string defaultDir;
			for (const auto& [dirName, dirConfig] : conf.searchDirectories) {
				if (dirConfig.default_)
					defaultDir = dirName;
			}

			for (auto& item : conf.items) {
				for (auto& source : item.source | std::views::values) {
					for (auto& fileList : source.inputFiles) {
						for (auto& patternList : fileList) {
							if (!patternList.directory.has_value())
								patternList.directory = defaultDir;
						}
					}
				}
				items.emplace_back(std::move(item));
			}
		} catch (const std::runtime_error& e) {
			std::cout << std::format("Error on {}: {}\n", confFile.filename().wstring(), e.what());
		}
	}

	auto tp = Utils::Win32::TpEnvironment(IsDebuggerPresent() ? 1 : 0);
	// auto tp = Utils::Win32::TpEnvironment();
	// auto tp = Utils::Win32::TpEnvironment(1);
	size_t index = 0;
	size_t count = 0;
	std::set<std::filesystem::path> tt;
	for (const auto& item : items) {
		for (const auto& target : item.target) {
			if (!target.enable)
				continue;

			for (const auto& path : target.path)
				tt.insert(path);

			auto allTargetExists = true;
			for (const auto& path : target.path)
				allTargetExists &= std::filesystem::exists(std::format(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\{0})", path.wstring()));
			if (allTargetExists)
				continue;

			count++;
			tp.SubmitWork([&index, &count, &directories, &item, &target, &sfxReader, &bgmReaders] {
				SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
				std::cout << std::format("[{}/{}] {}\n", index++, count, target.path.front());

				try {
					Sqex::Sound::MusicImporter importer(item.source, target, Utils::Win32::ResolvePathFromFileName("ffmpeg.exe"), Utils::Win32::ResolvePathFromFileName("ffprobe.exe"));
					for (const auto& path : target.path) {
						const auto rootName = path.begin()->wstring();
						if (lstrcmpiW(rootName.c_str(), L"sound") == 0)
							importer.AppendReader(std::make_shared<Sqex::Sound::ScdReader>(sfxReader[path]));
						else if (lstrcmpiW(rootName.c_str(), L"music") == 0) {
							const auto ex = (++path.begin())->wstring();
							if (ex == L"ffxiv")
								importer.AppendReader(std::make_shared<Sqex::Sound::ScdReader>(bgmReaders[0][path]));
							else if (ex == L"ex1")
								importer.AppendReader(std::make_shared<Sqex::Sound::ScdReader>(bgmReaders[1][path]));
							else if (ex == L"ex2")
								importer.AppendReader(std::make_shared<Sqex::Sound::ScdReader>(bgmReaders[2][path]));
							else if (ex == L"ex3")
								importer.AppendReader(std::make_shared<Sqex::Sound::ScdReader>(bgmReaders[3][path]));
							else
								throw std::invalid_argument(std::format("{} is not a valid expac folder name", ex));
						} else
							throw std::invalid_argument(std::format("{} is not a valid root folder name", rootName));
					}

					auto resolved = false;
					for (const auto& [dirName, dirPath] : directories)
						resolved |= importer.ResolveSources(dirName, dirPath);
					if (!resolved)
						throw std::runtime_error("Not all source files are found");
					importer.Merge([](std::filesystem::path path, std::vector<uint8_t> data) {
						const auto targetPath = std::filesystem::path(std::format(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\{0})", path.wstring()));
						create_directories(targetPath.parent_path());
						Utils::Win32::Handle::FromCreateFile(targetPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, 0).Write(0, std::span(data));
						});
				} catch (const std::exception& e) {
					std::cout << std::format("Error on {}: {}\n", target.path.front(), e.what());
				}
				});
		}
	}
	tp.WaitOutstanding();

	std::cout << std::format("Total {} items\n", tt.size());
	return 0;
}
