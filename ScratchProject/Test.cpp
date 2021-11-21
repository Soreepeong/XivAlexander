#include "pch.h"

#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Creator.h>

int main() {
	const Sqex::Sqpack::Reader reader(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\040000.win32.index)");
	for (const auto p : {
		"chara/equipment/e6127/texture/v01_c0101e6127_top_n.tex",
		"chara/equipment/e6127/e6127.imc",
		"chara/equipment/e6127/model/c0901e6127_top.mdl",
		"chara/equipment/e6127/material/v0001/mt_c0101e6127_top_a.mtrl",
		"chara/equipment/e6127/texture/v01_c0101e6127_top_m.tex",
		"chara/equipment/e6127/model/c0201e6127_top.mdl",
	}) {
		const auto path = std::filesystem::path((std::filesystem::path(LR"(C:\Users\SP\Desktop\test\)") / p).wstring() + L".test");
		std::filesystem::create_directories(path.parent_path());
		const auto stream = reader.GetFile(p);
		const auto data = stream->ReadStreamIntoVector<char>(0);
		const auto f = Utils::Win32::Handle::FromCreateFile(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS);
		f.Write(0, std::span(data));
	}
	return 0;
}