#include "pch.h"
#include "Sqex.h"

int main() {
	const auto targetBasePath = LR"(Z:\scratch\t2)";
	for (const auto& rootDir : {
		std::filesystem::path(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack)"),
		// std::filesystem::path(LR"(C:\Program Files (x86)\FINAL FANTASY XIV - KOREA\game\sqpack)"),
		// std::filesystem::path(LR"(D:\Program Files (x86)\SNDA\FFXIV\game\sqpack)"),
		}) {

		for (const auto& entry1 : std::filesystem::directory_iterator(rootDir)) {
			if (!entry1.is_directory())
				continue;
			if (entry1.path().filename().wstring() != L"ffxiv")
				continue;
			for (const auto& entry2 : std::filesystem::directory_iterator(entry1)) {
				const auto path = entry2.path();
				if (path.extension() != ".index")
					continue;

				try {
					std::cout << "Working on " << path << "..." << std::endl;
					auto vpack = Sqex::Sqpack::VirtualSqPack();
					vpack.AddEntriesFromSqPack(path, true, true);
					const auto replBase = std::filesystem::path(path).replace_extension("");
					for (const auto& entry3 : std::filesystem::recursive_directory_iterator(replBase)) {
						const auto& currPath = entry3.path();
						if (is_directory(currPath))
							continue;

						const auto relPath = proximate(currPath, replBase);
						auto pathComponent = relPath.parent_path().string();
						auto nameComponent = relPath.filename().string();
						for (auto& c : pathComponent)
							if (c == '\\')
								c = '/';
						CharLowerA(&pathComponent[0]);
						CharLowerA(&nameComponent[0]);
						const auto fullPath = std::format("{}/{}", pathComponent, nameComponent);
						vpack.AddEntryFromFile(
							Sqex::Sqpack::SqexHash(pathComponent),
							Sqex::Sqpack::SqexHash(nameComponent),
							Sqex::Sqpack::SqexHash(fullPath),
							currPath
						);
					}
					vpack.Freeze(false);

					std::cout << "Testing..." << std::endl;
					{
						char buf[65536];
						size_t pos = 0;
						for (uint32_t i = 0; i < vpack.NumOfDataFiles(); ++i) {
							pos = 0;
							while (true) {
								const auto w = vpack.ReadData(i, pos, buf, sizeof buf);;
								if (!w)
									break;
								pos += w;
							}
						}
					}

					std::cout << "Writing index..." << std::endl;
					const auto targetIndexPath = std::filesystem::path(std::format(LR"({}\{})", targetBasePath, path.filename().replace_extension(".index")));
					{
						const auto f1 = Utils::Win32::File::Create(
							targetIndexPath,
							GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0);
						char buf[65536];
						size_t pos = 0;
						while (true) {
							const auto w = vpack.ReadIndex1(pos, buf, sizeof buf);
							if (!w)
								break;
							pos += f1.Write(pos, buf, w);
						}
					}
					std::cout << "Writing index2..." << std::endl;
					{
						const auto f1 = Utils::Win32::File::Create(
							std::format(R"({}\{})", targetBasePath, path.filename().replace_extension(".index2")),
							GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0);
						char buf[65536];
						size_t pos = 0;
						while (true) {
							const auto w = vpack.ReadIndex2(pos, buf, sizeof buf);
							if (!w)
								break;
							pos += f1.Write(pos, buf, w);
						}
					}
					for (uint32_t i = 0; i < vpack.NumOfDataFiles(); ++i) {
						std::cout << "Writing dat" << i << "..." << std::endl;
						char buf[65536];
						size_t pos = 0;
						const auto f1 = Utils::Win32::File::Create(
							std::format(R"({}\{})", targetBasePath, path.filename().replace_extension(std::format(".dat{}", i))),
							GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0);
						while (true) {
							const auto w = vpack.ReadData(i, pos, buf, sizeof buf);
							if (!w)
								break;
							pos += f1.Write(pos, buf, w);
						}
					}
					std::cout << "OK" << std::endl;
					return 0;
				} catch (const std::invalid_argument& e) {
					std::cout << e.what() << std::endl;
					throw;
				}

			}
		}
	}

	return 0;
}
