#include "pch.h"

#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Creator.h>

int main() {

	struct Dfs {
		Utils::LE<uint32_t> FullPathHash;
		Utils::LE<uint32_t> Unused;
		Sqex::Sqpack::SqIndex::LEDataLocator Locator;
		Utils::LE<uint32_t> ConflictIndex;
		char FullPath[0xF0];
	};

	std::vector<uint64_t> ts{GetTickCount64()};
	std::vector<std::string> worklist;
	for (const auto& f : std::filesystem::recursive_directory_iterator(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\)")) {
		if (f.path().extension() != L".index")
			continue;

		std::cout << f.path() << std::endl;
		worklist.emplace_back(f.path().filename().string());

		const Sqex::Sqpack::Reader reader(f.path());

		ts.push_back(GetTickCount64());

		/*Sqex::Sqpack::Creator creator(f.path().parent_path().filename().string(), f.path().filename().string());
		creator.AddEntriesFromSqPack(f.path(), true, true);
		auto views = creator.AsViews(false);*/
	}

	for (size_t i = 1; i < ts.size(); ++i) {
		std::cout << std::format("{}: {}ms\n", worklist[i - 1], ts[i] - ts[i - 1]);
	}
	return 0;
}