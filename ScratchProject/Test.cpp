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

	char buf[8192]{};
	for (const auto& f : std::filesystem::directory_iterator(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\)")) {
		if (f.path().extension() != L".index")
			continue;

		if (f.path().filename() != "080000.win32.index")
			continue;
		/*const Sqex::Sqpack::Reader reader(f.path());
		if (reader.Index2.HashConflictSegment.size() > 1) {
			std::cout << f.path() << std::endl;
		}
		continue;*/

		std::cout << f.path() << std::endl;
		{
			Sqex::Sqpack::Creator creator(f.path().parent_path().filename().string(), f.path().filename().string());
			creator.AddEntriesFromSqPack(f.path(), true, true);
			auto views = creator.AsViews(true);

			auto fp = Utils::Win32::Handle::FromCreateFile(std::filesystem::path(LR"(Z:\xivtest)") / f.path().filename(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS);
			for (size_t i = 0, i_ = views.Index->StreamSize(); i < i_; i += sizeof buf) {
				const auto readlen = std::min(sizeof buf, i_ - i);
				views.Index->ReadStream(i, buf, readlen);
				fp.Write(i, buf, readlen);
			}
			fp = Utils::Win32::Handle::FromCreateFile(std::filesystem::path(LR"(Z:\xivtest)") / f.path().filename().replace_extension(".index2"), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS);
			for (size_t i = 0, i_ = views.Index2->StreamSize(); i < i_; i += sizeof buf) {
				const auto readlen = std::min(sizeof buf, i_ - i);
				views.Index2->ReadStream(i, buf, readlen);
				fp.Write(i, buf, readlen);
			}
			for (size_t i = 0; i < views.Data.size(); ++i) {
				fp = Utils::Win32::Handle::FromCreateFile(std::filesystem::path(LR"(Z:\xivtest)") / f.path().filename().replace_extension(std::format(".dat{}", i)), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS);
				for (size_t ptr = 0, i_ = views.Data[i]->StreamSize(); ptr < i_; ptr += sizeof buf) {
					const auto readlen = std::min(sizeof buf, i_ - ptr);
					views.Data[i]->ReadStream(ptr, buf, readlen);
					fp.Write(ptr, buf, readlen);
				}
			}
		}
		const Sqex::Sqpack::Reader testReader(std::filesystem::path(LR"(Z:\xivtest)") / f.path().filename());
		__debugbreak();
	}

	return 0;
}