#include "pch.h"

#include <string>

#include "XivAlexanderCommon/Sqex_EscapedString.h"
#include "XivAlexanderCommon/Sqex_Excel_Reader.h"
#include "XivAlexanderCommon/Sqex_Sqpack_Creator.h"
#include "XivAlexanderCommon/Sqex_Sqpack_Reader.h"

int main() {
	std::string nl = "\x02\x10\x01\x03";
	system("chcp 65001");

	auto creator = Sqex::Sqpack::Creator("ffxiv", "050000", 2000000000);
	auto addResult = creator.AddEntriesFromSqPack(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\050000.win32.index)", true, true);;
	for (const auto& itm : addResult.Replaced) {
		std::cout << std::format("Replaced {}\n", itm->PathSpec());
	}
	for (const auto& itm : addResult.Error) {
		std::cout << std::format("Error {}\n", itm.first);
	}

	const auto views = creator.AsViews(true);
	char buf[4096];

	{
		const auto index1 = Utils::Win32::File::Create(LR"(Z:\scratch\050000.win32.index)", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0);
		for (size_t i = 0; i < views.Index->StreamSize(); i += sizeof buf) {
			const auto len = std::min(sizeof buf, views.Index->StreamSize() - i);
			views.Index->ReadStream(i, buf, len);
			void(index1.Write(i, buf, len));
		}

		const auto index2 = Utils::Win32::File::Create(LR"(Z:\scratch\050000.win32.index2)", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0);
		for (size_t i = 0; i < views.Index2->StreamSize(); i += sizeof buf) {
			const auto len = std::min(sizeof buf, views.Index2->StreamSize() - i);
			views.Index2->ReadStream(i, buf, len);
			void(index2.Write(i, buf, len));
		}

		const auto dat0 = Utils::Win32::File::Create(LR"(Z:\scratch\050000.win32.dat0)", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0);
		for (size_t i = 0; i < views.Data[0]->StreamSize(); i += sizeof buf) {
			const auto len = std::min(sizeof buf, views.Data[0]->StreamSize() - i);
			views.Data[0]->ReadStream(i, buf, len);
			void(dat0.Write(i, buf, len));
		}
	}
	{
		const auto dat0 = Utils::Win32::File::Create(LR"(Z:\scratch\050000.win32.dat0)", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
		const auto len = views.Data[0]->StreamSize();
		while (true) {
			std::vector<uint8_t> buf1(65536);
			std::vector<uint8_t> buf2(65536);
			uint64_t offset = std::rand() * std::rand() % len;
			uint64_t readlen = std::min<uint64_t>(len - offset, std::max<uint64_t>(1024, std::rand() % 65536));
			views.Data[0]->ReadStream(offset, std::span(buf1).subspan(0, readlen));
			void(dat0.Read(offset, std::span(buf2).subspan(0, readlen)));
			if (memcmp(&buf1[0], &buf2[0], readlen) != 0)
				__debugbreak();
		}
	}
	Sqex::Sqpack::Reader reader1(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\050000.win32.index)");
	Sqex::Sqpack::Reader reader2(LR"(Z:\scratch\050000.win32.index)");
	return 0;
}
