#include "pch.h"
#include "XivAlexanderCommon/Sqex/ThirdParty/TexTools.h"
#include "XivAlexanderCommon/Sqex/Sqpack.h"
#include "XivAlexanderCommon/Sqex.h"

using namespace std::string_literals;

template<typename T>
T JsonValueOrDefault(const nlohmann::json& json, const char* key, T defaultValue, T nullDefaultValue) {
	if (const auto it = json.find(key); it != json.end()) {
		if (it->is_null())
			return nullDefaultValue;
		return it->get<T>();
	}
	return defaultValue;
}

void Sqex::ThirdParty::TexTools::to_json(nlohmann::json& j, const ModPackEntry& p) {
	j = nlohmann::json::object({
		{"Name", p.Name},
		{"Author", p.Author},
		{"Version", p.Version},
		{"Url", p.Url},
		});
}

void Sqex::ThirdParty::TexTools::from_json(const nlohmann::json& j, ModPackEntry& p) {
	if (j.is_null())
		return;
	if (!j.is_object())
		throw CorruptDataException("ModPackEntry must be an object");

	p.Name = JsonValueOrDefault(j, "Name", ""s, ""s);
	p.Author = JsonValueOrDefault(j, "Author", ""s, ""s);
	p.Version = JsonValueOrDefault(j, "Version", ""s, ""s);
	p.Url = JsonValueOrDefault(j, "Url", ""s, ""s);
}

void Sqex::ThirdParty::TexTools::to_json(nlohmann::json& j, const ModEntry& p) {
	j = nlohmann::json::object({
		{"Name", p.Name},
		{"Category", p.Category},
		{"FullPath", p.FullPath},
		{"ModOffset", p.ModOffset},
		{"ModSize", p.ModSize},
		{"DatFile", p.DatFile},
		{"IsDefault", p.IsDefault},
		});
	if (p.ModPack)
		j["ModPackEntry"] = *p.ModPack;
}

void Sqex::ThirdParty::TexTools::from_json(const nlohmann::json& j, ModEntry& p) {
	if (!j.is_object())
		throw CorruptDataException("ModEntry must be an object");

	p.Name = JsonValueOrDefault(j, "Name", ""s, ""s);
	p.Category = JsonValueOrDefault(j, "Category", ""s, ""s);
	p.FullPath = JsonValueOrDefault(j, "FullPath", ""s, ""s);
	p.ModOffset = JsonValueOrDefault(j, "ModOffset", 0ULL, 0ULL);
	p.ModSize = JsonValueOrDefault(j, "ModSize", 0ULL, 0ULL);
	p.DatFile = JsonValueOrDefault(j, "DatFile", ""s, ""s);
	p.IsDefault = JsonValueOrDefault(j, "IsDefault", false, false);
	if (const auto it = j.find("ModPackEntry"); it != j.end() && !it->is_null())
		p.ModPack = it->get<ModPackEntry>();
}

void Sqex::ThirdParty::TexTools::ModPackPage::to_json(nlohmann::json& j, const Option& p) {
	j = nlohmann::json::object({
		{"Name", p.Name},
		{"Description", p.Description},
		{"ImagePath", p.ImagePath},
		{"ModsJsons", p.ModsJsons},
		{"GroupName", p.GroupName},
		{"SelectionType", p.SelectionType},
		{"IsChecked", p.IsChecked},
		});
}

void Sqex::ThirdParty::TexTools::ModPackPage::from_json(const nlohmann::json& j, Option& p) {
	if (!j.is_object())
		throw CorruptDataException("Option must be an object");

	p.Name = JsonValueOrDefault(j, "Name", ""s, ""s);
	p.Description = JsonValueOrDefault(j, "Description", ""s, ""s);
	p.ImagePath = JsonValueOrDefault(j, "ImagePath", ""s, ""s);
	if (const auto it = j.find("ModsJsons"); it != j.end() && !it->is_null())
		p.ModsJsons = it->get<decltype(p.ModsJsons)>();
	p.GroupName = JsonValueOrDefault(j, "GroupName", ""s, ""s);
	p.SelectionType = JsonValueOrDefault(j, "SelectionType", ""s, ""s);
	p.IsChecked = JsonValueOrDefault(j, "IsChecked", false, false);
}

void Sqex::ThirdParty::TexTools::ModPackPage::to_json(nlohmann::json& j, const ModGroup& p) {
	j = nlohmann::json::object({
		{"GroupName", p.GroupName},
		{"SelectionType", p.SelectionType},
		{"OptionList", p.OptionList},
		});
}

void Sqex::ThirdParty::TexTools::ModPackPage::from_json(const nlohmann::json& j, ModGroup& p) {
	if (!j.is_object())
		throw CorruptDataException("Option must be an object");

	p.GroupName = JsonValueOrDefault(j, "GroupName", ""s, ""s);
	p.SelectionType = JsonValueOrDefault(j, "SelectionType", ""s, ""s);
	if (const auto it = j.find("OptionList"); it != j.end() && !it->is_null())
		p.OptionList = it->get<decltype(p.OptionList)>();
}

void Sqex::ThirdParty::TexTools::ModPackPage::to_json(nlohmann::json& j, const Page& p) {
	j = nlohmann::json::object({
		{"PageIndex", p.PageIndex},
		{"ModGroups", p.ModGroups},
		});
}

void Sqex::ThirdParty::TexTools::ModPackPage::from_json(const nlohmann::json& j, Page& p) {
	if (!j.is_object())
		throw CorruptDataException("Option must be an object");

	p.PageIndex = JsonValueOrDefault(j, "PageIndex", 0, 0);
	if (const auto it = j.find("ModGroups"); it != j.end() && !it->is_null())
		p.ModGroups = it->get<decltype(p.ModGroups)>();
}

void Sqex::ThirdParty::TexTools::to_json(nlohmann::json& j, const TTMPL& p) {
	j = nlohmann::json::object({
		{"MinimumFrameworkVersion", p.MinimumFrameworkVersion},
		{"FormatVersion", p.FormatVersion},
		{"Name", p.Name},
		{"Author", p.Author},
		{"Version", p.Version},
		{"Description", p.Description},
		{"Url", p.Url},
		{"ModPackPages", p.ModPackPages},
		{"SimpleModsList", p.SimpleModsList},
		});
}

void Sqex::ThirdParty::TexTools::from_json(const nlohmann::json& j, TTMPL& p) {
	if (!j.is_object())
		throw CorruptDataException("TTMPL must be an object");

	p.MinimumFrameworkVersion = JsonValueOrDefault(j, "MinimumFrameworkVersion", ""s, ""s);
	p.FormatVersion = JsonValueOrDefault(j, "FormatVersion", ""s, ""s);
	p.Name = JsonValueOrDefault(j, "Name", ""s, ""s);
	p.Author = JsonValueOrDefault(j, "Author", ""s, ""s);
	p.Version = JsonValueOrDefault(j, "Version", ""s, ""s);
	p.Description = JsonValueOrDefault(j, "Description", ""s, ""s);
	p.Url = JsonValueOrDefault(j, "Url", ""s, ""s);
	if (const auto it = j.find("ModPackPages"); it != j.end() && !it->is_null())
		p.ModPackPages = it->get<decltype(p.ModPackPages)>();
	if (const auto it = j.find("SimpleModsList"); it != j.end() && !it->is_null())
		p.SimpleModsList = it->get<decltype(p.SimpleModsList)>();
}

Sqex::ThirdParty::TexTools::TTMPL Sqex::ThirdParty::TexTools::TTMPL::FromStream(const RandomAccessStream& stream) {
	const auto size = stream.StreamSize();
	if (size > 16 * 1024 * 1024)
		throw CorruptDataException("File too big (>16MB).");

	std::string buf(static_cast<size_t>(size), '\0');
	stream.ReadStream(0, &buf[0], buf.size());

	std::istringstream in(buf);
	TTMPL res;
	while (!in.eof()) {
		nlohmann::json j;
		try {
			in >> j;
		} catch (...) {
			if (in.eof())
				break;
		}
		if (j.find("ModOffset") != j.end()) {
			res.SimpleModsList.emplace_back(j.get<ModEntry>());
		} else {
			return j.get<TTMPL>();
		}
	}
	return res;
}

void Sqex::ThirdParty::TexTools::TTMPL::ForEachEntry(std::function<void(Sqex::ThirdParty::TexTools::ModEntry&)> cb) {
	for (auto& entry : SimpleModsList)
		cb(entry);

	for (auto& modPackPage : ModPackPages)
		for (auto& modGroup : modPackPage.ModGroups)
			for (auto& option : modGroup.OptionList)
				for (auto& entry : option.ModsJsons)
					cb(entry);
}

void Sqex::ThirdParty::TexTools::TTMPL::ForEachEntry(std::function<void(const Sqex::ThirdParty::TexTools::ModEntry&)> cb) const {
	for (auto& entry : SimpleModsList)
		cb(entry);

	for (auto& modPackPage : ModPackPages)
		for (auto& modGroup : modPackPage.ModGroups)
			for (auto& option : modGroup.OptionList)
				for (auto& entry : option.ModsJsons)
					cb(entry);
}

Sqex::ThirdParty::TexTools::TTMPL::TraverseCallbackResult Sqex::ThirdParty::TexTools::TTMPL::ForEachEntryInterruptible(std::function<TraverseCallbackResult(Sqex::ThirdParty::TexTools::ModEntry&)> cb) {
	for (auto& entry : SimpleModsList)
		if (Break == cb(entry))
			return Break;

	for (auto& modPackPage : ModPackPages)
		for (auto& modGroup : modPackPage.ModGroups)
			for (auto& option : modGroup.OptionList)
				for (auto& entry : option.ModsJsons)
					if (Break == cb(entry))
						return Break;

	return Continue;
}

Sqex::ThirdParty::TexTools::TTMPL::TraverseCallbackResult Sqex::ThirdParty::TexTools::TTMPL::ForEachEntryInterruptible(std::function<TraverseCallbackResult(const Sqex::ThirdParty::TexTools::ModEntry&)> cb) const {
	for (const auto& entry : SimpleModsList)
		if (Break == cb(entry))
			return Break;

	for (const auto& modPackPage : ModPackPages)
		for (const auto& modGroup : modPackPage.ModGroups)
			for (const auto& option : modGroup.OptionList)
				for (const auto& entry : option.ModsJsons)
					if (Break == cb(entry))
						return Break;

	return Continue;
}

std::string Sqex::ThirdParty::TexTools::ModEntry::ToExpacDatPath() const {
	Sqpack::EntryPathSpec pathSpec(FullPath);
	return std::format("{}/{}", pathSpec.DatExpac(), pathSpec.DatFile());
}

bool Sqex::ThirdParty::TexTools::ModEntry::IsMetadata() const {
	if (FullPath.length() < 5)
		return false;
	auto metaExt = FullPath.substr(FullPath.length() - 5);
	CharLowerA(&metaExt[0]);
	return metaExt == ".meta";
}

const srell::u8cregex Sqex::ThirdParty::TexTools::ItemMetadata::CharacterMetaPathTest(
	"^(?<FullPathPrefix>chara"
	"/(?<PrimaryType>[a-z]+)"
	"/(?<PrimaryCode>[a-z])(?<PrimaryId>[0-9]+)"
	"(?:/obj"
	"/(?<SecondaryType>[a-z]+)"
	"/(?<SecondaryCode>[a-z])(?<SecondaryId>[0-9]+))?"
	"/).*?(?:_(?<Slot>[a-z]{3}))?\\.meta$"
	, srell::u8cregex::icase);
const srell::u8cregex Sqex::ThirdParty::TexTools::ItemMetadata::HousingMetaPathTest(
	"^(?<FullPathPrefix>bgcommon"
	"/hou"
	"/(?<PrimaryType>[a-z]+)"
	"/general"
	"/(?<PrimaryId>[0-9]+)"
	"/).*?\\.meta$"
	, srell::u8cregex::icase);

Sqex::ThirdParty::TexTools::ItemMetadata::ItemMetadata(std::string gamePath, const RandomAccessStream& stream)
	: Data(stream.ReadStreamIntoVector<uint8_t>(0))
	, Version(*reinterpret_cast<const uint32_t*>(&Data[0]))
	, TargetPath(std::move(gamePath))
	, SourcePath(reinterpret_cast<const char*>(&Data[sizeof Version]))
	, Header(*reinterpret_cast<const MetaDataHeader*>(&Data[sizeof Version + SourcePath.size() + 1]))
	, AllEntries(span_cast<MetaDataEntryLocator>(Data, Header.FirstEntryLocatorOffset, Header.EntryCount)) {
	if (srell::u8csmatch matches;
		srell::regex_search(TargetPath, matches, CharacterMetaPathTest)) {
		PrimaryType = matches["PrimaryType"].str();
		PrimaryId = static_cast<uint16_t>(std::strtol(matches["PrimaryId"].str().c_str(), nullptr, 10));
		SecondaryType = matches["SecondaryType"].str();
		SecondaryId = static_cast<uint16_t>(std::strtol(matches["SecondaryId"].str().c_str(), nullptr, 10));
		if (SecondaryType.empty())
			TargetImcPath = matches["FullPathPrefix"].str() + matches["PrimaryCode"].str() + matches["PrimaryId"].str() + ".imc";
		else
			TargetImcPath = matches["FullPathPrefix"].str() + matches["SecondaryCode"].str() + matches["SecondaryId"].str() + ".imc";
		CharLowerA(&PrimaryType[0]);
		CharLowerA(&SecondaryType[0]);
		if (PrimaryType == "equipment") {
			auto slot = matches["Slot"].str();
			CharLowerA(&slot[0]);
			if (0 == slot.compare("met"))
				ItemType = TargetItemType::Equipment, SlotIndex = 0, EqpEntrySize = 3, EqpEntryOffset = 5, EstType = TargetEstType::Head;
			else if (0 == slot.compare("top"))
				ItemType = TargetItemType::Equipment, SlotIndex = 1, EqpEntrySize = 2, EqpEntryOffset = 0, EstType = TargetEstType::Body;
			else if (0 == slot.compare("glv"))
				ItemType = TargetItemType::Equipment, SlotIndex = 2, EqpEntrySize = 1, EqpEntryOffset = 3;
			else if (0 == slot.compare("dwn"))
				ItemType = TargetItemType::Equipment, SlotIndex = 3, EqpEntrySize = 1, EqpEntryOffset = 2;
			else if (0 == slot.compare("sho"))
				ItemType = TargetItemType::Equipment, SlotIndex = 4, EqpEntrySize = 1, EqpEntryOffset = 4;
			else if (0 == slot.compare("ear"))
				ItemType = TargetItemType::Accessory, SlotIndex = 0;
			else if (0 == slot.compare("nek"))
				ItemType = TargetItemType::Accessory, SlotIndex = 1;
			else if (0 == slot.compare("wrs"))
				ItemType = TargetItemType::Accessory, SlotIndex = 2;
			else if (0 == slot.compare("rir"))
				ItemType = TargetItemType::Accessory, SlotIndex = 3;
			else if (0 == slot.compare("ril"))
				ItemType = TargetItemType::Accessory, SlotIndex = 4;
		} else if (PrimaryType == "human") {
			if (SecondaryType == "hair")
				EstType = TargetEstType::Hair;
			else if (SecondaryType == "face")
				EstType = TargetEstType::Face;
		}

	} else if (srell::regex_search(TargetPath, matches, HousingMetaPathTest)) {
		PrimaryType = matches["PrimaryType"].str();
		PrimaryId = static_cast<uint16_t>(std::strtol(matches["PrimaryId"].str().c_str(), nullptr, 10));
		ItemType = TargetItemType::Housing;

	} else {
		throw NotItemMetadataError("Unsupported meta file");
	}

	if (srell::u8csmatch matches;
		srell::regex_search(SourcePath, matches, CharacterMetaPathTest)) {
		if (SecondaryType.empty())
			SourceImcPath = matches["FullPathPrefix"].str() + matches["PrimaryCode"].str() + matches["PrimaryId"].str() + ".imc";
		else
			SourceImcPath = matches["FullPathPrefix"].str() + matches["SecondaryCode"].str() + matches["SecondaryId"].str() + ".imc";

	} else {
		throw NotItemMetadataError("Unsupported meta file");
	}
}

void Sqex::ThirdParty::TexTools::ItemMetadata::ApplyImcEdits(std::function<Sqex::Imc::File& ()> reader) const {
	if (const auto imcedit = Get<Sqex::Imc::Entry>(MetaDataType::Imc); !imcedit.empty()) {
		auto& imc = reader();
		if (imc.Header().Type == Imc::Type::Unknown) {
			const auto& typeStr = SecondaryType.empty() ? PrimaryType : SecondaryType;
			imc.Header().Type = typeStr == "equipment" || typeStr == "accessory" ? Imc::Type::Set : Imc::Type::NonSet;
		}
		imc.Ensure(imcedit.size() - 1);
		for (size_t i = 0; i < imcedit.size(); ++i) {
			imc.Entry(i * imc.EntryCountPerSet() + SlotIndex) = imcedit[i];
		}
	}
}

void Sqex::ThirdParty::TexTools::ItemMetadata::ApplyEqdpEdits(std::function<Sqex::Eqdp::ExpandedFile& (TargetItemType, uint32_t)> reader) const {
	if (const auto eqdpedit = Get<EqdpEntry>(MetaDataType::Eqdp); !eqdpedit.empty()) {
		for (const auto& v : eqdpedit) {
			auto& eqdp = reader(ItemType, v.RaceCode);
			auto& target = eqdp.Set(PrimaryId);
			target &= ~(0b11 << (SlotIndex * 2));
			target |= v.Value << (SlotIndex * 2);
		}
	}
}

void Sqex::ThirdParty::TexTools::ItemMetadata::ApplyEqpEdits(Sqex::EqpGmp::ExpandedFile& eqp) const {
	if (const auto eqpedit = Get<uint8_t>(Sqex::ThirdParty::TexTools::ItemMetadata::MetaDataType::Eqp); !eqpedit.empty()) {
		if (eqpedit.size() != EqpEntrySize)
			throw Sqex::CorruptDataException(std::format("expected {}b for eqp; got {}b", EqpEntrySize, eqpedit.size()));
		std::copy_n(&eqpedit[0], EqpEntrySize, &eqp.ParameterBytes(PrimaryId)[EqpEntryOffset]);
	}
}

void Sqex::ThirdParty::TexTools::ItemMetadata::ApplyGmpEdits(Sqex::EqpGmp::ExpandedFile& gmp) const {
	if (const auto gmpedit = Get<uint8_t>(Sqex::ThirdParty::TexTools::ItemMetadata::MetaDataType::Gmp); !gmpedit.empty()) {
		if (gmpedit.size() != sizeof uint64_t)
			throw Sqex::CorruptDataException(std::format("gmp data must be 8 bytes; {} byte(s) given", gmpedit.size()));
		std::copy_n(&gmpedit[0], gmpedit.size(), &gmp.ParameterBytes(PrimaryId)[0]);
	}
}

void Sqex::ThirdParty::TexTools::ItemMetadata::ApplyEstEdits(Sqex::Est::File& est) const {
	if (const auto estedit = Get<Sqex::ThirdParty::TexTools::ItemMetadata::EstEntry>(Sqex::ThirdParty::TexTools::ItemMetadata::MetaDataType::Est); !estedit.empty()) {
		auto estpairs = est.ToPairs();
		for (const auto& v : estedit) {
			const auto key = Sqex::Est::EntryDescriptor{ .SetId = v.SetId, .RaceCode = v.RaceCode };
			if (v.SkelId == 0)
				estpairs.erase(key);
			else
				estpairs.insert_or_assign(key, v.SkelId);
		}
		est.Update(estpairs);
	}
}
