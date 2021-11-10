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

	std::vector<uint64_t> ts;
	std::vector<std::string> worklist;
	for (const auto& f : std::filesystem::recursive_directory_iterator(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\)")) {
		if (f.path().extension() != L".index")
			continue;
		if (f.path().filename() != L"040000.win32.index")
			continue;

		std::cout << f.path() << std::endl;
		
		worklist.emplace_back(std::format("{}: read", f.path().filename().string()));
		ts.push_back(GetTickCount64());
		const Sqex::Sqpack::Reader reader(f.path());

		worklist.emplace_back(std::format("{}: add", f.path().filename().string()));
		ts.push_back(GetTickCount64());
		Sqex::Sqpack::Creator creator(f.path().parent_path().filename().string(), f.path().filename().string());
		creator.AddEntriesFromSqPack(f.path(), true, true);

		worklist.emplace_back(std::format("{}: asviews", f.path().filename().string()));
		ts.push_back(GetTickCount64());
		auto views = creator.AsViews(false);
	}

	ts.push_back(GetTickCount64());
	for (size_t i = 0; i < ts.size() - 1; ++i) {
		std::cout << std::format("{}: {}ms\n", worklist[i], ts[i + 1] - ts[i]);
	}
	return 0;
}