#include "pch.h"

#include <XivAlexanderCommon/Sqex_Sqpack_Creator.h>
#include <XivAlexanderCommon/Sqex_Sqpack_EntryRawStream.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>
#include <XivAlexanderCommon/Sqex_ThirdParty_TexTools.h>
#include "XivAlexanderCommon/Sqex_Est.h"
#include "XivAlexanderCommon/Sqex_EqpGmp.h"
#include "XivAlexanderCommon/Sqex_Imc.h"
#include "XivAlexanderCommon/Sqex_Eqdp.h"

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

	const auto ttmpl = Sqex::ThirdParty::TexTools::TTMPL::FromStream(Sqex::FileRandomAccessStream(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\TexToolsMods\Mod_Odin set with cape\TTMPL.mpl)"));
	const auto ttmpd = std::make_shared<Sqex::FileRandomAccessStream>(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\TexToolsMods\Mod_Odin set with cape\TTMPD.mpd)");
	for (const auto& ttmpEntry : ttmpl.SimpleModsList) {
		if (ttmpEntry.FullPath.length() < 5)
			continue;
		auto metaExt = ttmpEntry.FullPath.substr(ttmpEntry.FullPath.length() - 5);
		CharLowerA(&metaExt[0]);
		if (metaExt != ".meta")
			continue;
		try {
			const auto metadata = Sqex::ThirdParty::TexTools::ItemMetadata(Sqex::Sqpack::EntryRawStream(std::make_shared<Sqex::Sqpack::RandomAccessStreamAsEntryProviderView>(ttmpEntry.FullPath, ttmpd, ttmpEntry.ModOffset, ttmpEntry.ModSize)));
			metadata.ApplyImcEdits([&]() -> Sqex::Imc::File& {
				const auto imcPath = metadata.ImcPath();
				if (const auto it = imcs.find(imcPath); it == imcs.end())
					return *(imcs[imcPath] = std::make_unique<Sqex::Imc::File>(*reader[imcPath]));
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
			continue;
		}
	}

	Utils::Win32::Handle::FromCreateFile(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\chara/xls/charadb/extra_met.est)", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(estHead.Data()));
	Utils::Win32::Handle::FromCreateFile(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\chara/xls/charadb/extra_top.est)", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(estBody.Data()));
	Utils::Win32::Handle::FromCreateFile(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\chara/xls/charadb/hairskeletontemplate.est)", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(estHair.Data()));
	Utils::Win32::Handle::FromCreateFile(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\chara/xls/charadb/faceskeletontemplate.est)", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(estFace.Data()));
	Utils::Win32::Handle::FromCreateFile(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\chara/xls/equipmentparameter/equipmentparameter.eqp)", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(Sqex::EqpGmp::CollapsedFile(eqp).Data()));
	Utils::Win32::Handle::FromCreateFile(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\chara/xls/equipmentparameter/gimmickparameter.eqp)", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(Sqex::EqpGmp::CollapsedFile(gmp).Data()));
	for (const auto& [filename, d] : imcs)
		Utils::Win32::Handle::FromCreateFile(std::format(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\{})", filename), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(d->Data()));
	for (const auto& [typeRace, d] : eqdps)
		Utils::Win32::Handle::FromCreateFile(std::format(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\{})",
			Sqex::ThirdParty::TexTools::ItemMetadata::EqdpPath(typeRace.first, typeRace.second)),
			GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(Sqex::Eqdp::File(*d).Data()));
	return 0;
}