#include "pch.h"

#include <XivAlexanderCommon/Sqex/Sqpack/Creator.h>
#include <XivAlexanderCommon/Sqex/Sqpack/EntryRawStream.h>
#include <XivAlexanderCommon/Sqex/Sqpack/Reader.h>
#include <XivAlexanderCommon/Sqex/ThirdParty/TexTools.h>
#include "XivAlexanderCommon/Sqex/Est.h"
#include "XivAlexanderCommon/Sqex/EqpGmp.h"
#include "XivAlexanderCommon/Sqex/Imc.h"
#include "XivAlexanderCommon/Sqex/Eqdp.h"

int main() {
	const Sqex::Sqpack::Reader reader(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\040000.win32.index)");

	auto estHead = Sqex::Est::File(*reader["chara/xls/charadb/extra_met.est"]);
	auto estBody = Sqex::Est::File(*reader["chara/xls/charadb/extra_top.est"]);
	auto estHair = Sqex::Est::File(*reader["chara/xls/charadb/hairskeletontemplate.est"]);
	auto estFace = Sqex::Est::File(*reader["chara/xls/charadb/faceskeletontemplate.est"]);
	auto eqp = Sqex::EqpGmp::ExpandedFile(*reader["chara/xls/equipmentparameter/equipmentparameter.eqp"]);
	auto gmp = Sqex::EqpGmp::ExpandedFile(*reader["chara/xls/equipmentparameter/gimmickparameter.gmp"]);
	std::map<std::string, std::unique_ptr<Sqex::Imc::File>> imcs;
	std::map<std::pair<Sqex::ThirdParty::TexTools::ItemMetadata::TargetItemType, uint32_t>, std::unique_ptr<Sqex::Eqdp::ExpandedFile>> eqdps;

	const auto ttmpl = Sqex::ThirdParty::TexTools::TTMPL::FromStream(Sqex::FileRandomAccessStream(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\TexToolsMods\Testing\Updated EDGY Hades Weapons\TTMPL.mpl)"));
	const auto ttmpd = std::make_shared<Sqex::FileRandomAccessStream>(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\TexToolsMods\Testing\Updated EDGY Hades Weapons\TTMPD.mpd)");
	ttmpl.ForEachEntry([&](const Sqex::ThirdParty::TexTools::ModEntry& ttmpEntry) {
		std::cout << std::format("Extracting {}...\n", ttmpEntry.FullPath);
		auto path = std::filesystem::path(std::format(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\{})", ttmpEntry.FullPath));
		create_directories(path.parent_path());
		
		if (ttmpEntry.FullPath == "chara/weapon/w2199/obj/body/b0001/texture/v03_w2199b0001_m.tex")
			__debugbreak();
		auto provider = std::make_shared<Sqex::Sqpack::RandomAccessStreamAsEntryProviderView>(ttmpEntry.FullPath, ttmpd, ttmpEntry.ModOffset, ttmpEntry.ModSize);
		auto rawStream = Sqex::Sqpack::EntryRawStream(provider);
		auto data = rawStream.ReadStreamIntoVector<uint8_t>(0);
		Utils::Win32::Handle::FromCreateFile(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(data));
		
		if (ttmpEntry.IsMetadata()) {
			try {
				const auto metadata = Sqex::ThirdParty::TexTools::ItemMetadata(ttmpEntry.FullPath, rawStream);
				metadata.ApplyImcEdits([&]() -> Sqex::Imc::File& {
					if (const auto it = imcs.find(metadata.SourceImcPath); it == imcs.end())
						return *(imcs[metadata.SourceImcPath] = std::make_unique<Sqex::Imc::File>(*reader[metadata.SourceImcPath]));
					else
						return *it->second;
					});
				metadata.ApplyEqdpEdits([&](auto type, auto race) -> Sqex::Eqdp::ExpandedFile& {
					const auto key = std::make_pair(type, race);
					if (const auto it = eqdps.find(key); it == eqdps.end())
						return *(eqdps[key] = std::make_unique<Sqex::Eqdp::ExpandedFile>(*reader[Sqex::ThirdParty::TexTools::ItemMetadata::EqdpPath(type, race)]));
					else
						return *it->second;
					});
				metadata.ApplyEqpEdits(eqp);
				metadata.ApplyGmpEdits(gmp);
				if (metadata.EstType == Sqex::ThirdParty::TexTools::ItemMetadata::TargetEstType::Body)
					metadata.ApplyEstEdits(estBody);
				else if (metadata.EstType == Sqex::ThirdParty::TexTools::ItemMetadata::TargetEstType::Face)
					metadata.ApplyEstEdits(estFace);
				else if (metadata.EstType == Sqex::ThirdParty::TexTools::ItemMetadata::TargetEstType::Hair)
					metadata.ApplyEstEdits(estHair);
				else if (metadata.EstType == Sqex::ThirdParty::TexTools::ItemMetadata::TargetEstType::Head)
					metadata.ApplyEstEdits(estHead);
			} catch (const Sqex::ThirdParty::TexTools::ItemMetadata::NotItemMetadataError&) {
				// pass
			}
		}
	});

	std::filesystem::create_directories(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\chara/xls/charadb/)");
	std::filesystem::create_directories(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\chara/xls/equipmentparameter/)");
	Utils::Win32::Handle::FromCreateFile(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\chara/xls/charadb/extra_met.est)", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(estHead.Data()));
	Utils::Win32::Handle::FromCreateFile(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\chara/xls/charadb/extra_top.est)", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(estBody.Data()));
	Utils::Win32::Handle::FromCreateFile(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\chara/xls/charadb/hairskeletontemplate.est)", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(estHair.Data()));
	Utils::Win32::Handle::FromCreateFile(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\chara/xls/charadb/faceskeletontemplate.est)", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(estFace.Data()));
	Utils::Win32::Handle::FromCreateFile(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\chara/xls/equipmentparameter/equipmentparameter.eqp)", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(Sqex::EqpGmp::CollapsedFile(eqp).Data()));
	Utils::Win32::Handle::FromCreateFile(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\chara/xls/equipmentparameter/gimmickparameter.eqp)", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(Sqex::EqpGmp::CollapsedFile(gmp).Data()));
	for (const auto& [filename, d] : imcs) {
		auto path = std::filesystem::path(std::format(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\{})", filename));
		create_directories(path.parent_path());
		Utils::Win32::Handle::FromCreateFile(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(d->Data()));
	}
	for (const auto& [typeRace, d] : eqdps) {
		auto path = std::filesystem::path(std::format(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\{})",
			Sqex::ThirdParty::TexTools::ItemMetadata::EqdpPath(typeRace.first, typeRace.second)));
		create_directories(path.parent_path());
		Utils::Win32::Handle::FromCreateFile(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(Sqex::Eqdp::File(*d).Data()));
	}
	return 0;
}