#include "pch.h"

#include <XivAlexanderCommon/Sqex_Sqpack_Creator.h>
#include <XivAlexanderCommon/Sqex_Sqpack_EntryRawStream.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>
#include <XivAlexanderCommon/Sqex_ThirdParty_TexTools.h>

namespace Sqex {
	enum class ImcType : uint16_t {
		Unknown = 0,
		NonSet = 1,
		Set = 31
	};

	struct ImcHeader {
		LE<uint16_t> SubsetCount;
		LE<ImcType> Type;
	};

	struct ImcEntry {
		uint8_t Variant;
		uint8_t Unknown_0x001;
		LE<uint16_t> Mask;
		uint8_t Vfx;
		uint8_t Animation;
	};

	class ImcFile {
	public:
		std::vector<uint8_t> Data;

		ImcFile(const RandomAccessStream& stream)
			: Data(stream.ReadStreamIntoVector<uint8_t>(0)) {
		}

		size_t EntryCountPerSet() const {
			size_t entryCountPerSet = 0;
			for (size_t i = 0; i < 16; i++) {
				if (static_cast<uint16_t>(Header().Type.Value()) & (1 << i))
					entryCountPerSet++;
			}
			return entryCountPerSet;
		}

		ImcHeader& Header() {
			return *reinterpret_cast<ImcHeader*>(Data.data());
		}

		const ImcHeader& Header() const {
			return *reinterpret_cast<const ImcHeader*>(Data.data());
		}

		std::span<ImcEntry> AllEntries() {
			return { reinterpret_cast<ImcEntry*>(&Data[sizeof Header()]), (Data.size() - sizeof Header()) / sizeof ImcEntry };
		}

		std::span<const ImcEntry> AllEntries() const {
			return { reinterpret_cast<const ImcEntry*>(&Data[sizeof Header()]), (Data.size() - sizeof Header()) / sizeof ImcEntry };
		}
	};

	struct EqdpHeader {
		LE<uint16_t> Identifier;
		LE<uint16_t> BlockSize;
		LE<uint16_t> BlockCount;
	};

	class EqdpFile {
		bool m_expanded = false;

	public:
		std::vector<uint8_t> Data;

		EqdpFile(const RandomAccessStream& stream)
			: Data(stream.ReadStreamIntoVector<uint8_t>(0)) {
		}

		EqdpHeader& Header() {
			return *reinterpret_cast<EqdpHeader*>(Data.data());
		}

		const EqdpHeader& Header() const {
			return *reinterpret_cast<const EqdpHeader*>(Data.data());
		}

		size_t BaseOffset() const {
			return sizeof EqdpHeader + sizeof uint16_t + Header().BlockCount;
		}

		std::span<uint16_t> Offsets() {
			return { reinterpret_cast<uint16_t*>(&Data[sizeof Header()]), Header().BlockCount };
		}

		std::span<const uint16_t> Offsets() const {
			return { reinterpret_cast<const uint16_t*>(&Data[sizeof Header()]), Header().BlockCount };
		}

		std::span<uint16_t> Block(size_t blockId) {
			const auto offset = Offsets()[blockId];
			if (offset == UINT16_MAX)
				return {};

			return { reinterpret_cast<uint16_t*>(&Data[BaseOffset() + offset]), Header().BlockSize / sizeof uint16_t };
		}

		std::span<const uint16_t> Block(size_t blockId) const {
			const auto offset = Offsets()[blockId];
			if (offset == UINT16_MAX)
				return {};

			return { reinterpret_cast<const uint16_t*>(&Data[BaseOffset() + offset]), Header().BlockSize / sizeof uint16_t };
		}

		uint16_t& Set(size_t setId) {
			return Block(setId / Header().BlockSize)[setId % (Header().BlockSize / 2)];
		}

		const uint16_t& Set(size_t setId) const {
			return Block(setId / Header().BlockSize)[setId % (Header().BlockSize / 2)];
		}

		void ExpandCollapseAll(bool expand) {
			if (m_expanded == expand)
				return;

			std::vector<uint8_t> newData;
			newData.reserve(BaseOffset() + size_t{1} * Header().BlockCount * Header().BlockSize);
			
			newData.resize(BaseOffset());
			std::copy_n(Data.begin(), sizeof EqdpHeader, newData.begin());
			const auto newOffsets = std::span(reinterpret_cast<uint16_t*>(&newData[sizeof EqdpHeader]), Header().BlockCount);

			const auto offsets = Offsets();
			for (size_t i = 0; i < offsets.size(); ++i) {
				if (expand) {
					newOffsets[i] = static_cast<uint16_t>(newData.size() - BaseOffset());
					newData.resize(newData.size() + Header().BlockSize);
					if (offsets[i] == UINT16_MAX)
						continue;
					std::copy_n(&Data[BaseOffset() + offsets[i]], Header().BlockSize, &newData[BaseOffset() + newOffsets[i]]);

				} else {
					auto isAllZeros = true;
					for (size_t j = BaseOffset() + offsets[i], j_ = j + Header().BlockSize; isAllZeros && j < j_; j++) {
						isAllZeros = Data[j] == 0;
					}
					if (isAllZeros) {
						newOffsets[i] = UINT16_MAX;
					} else {
						newOffsets[i] = static_cast<uint16_t>(newData.size() - BaseOffset());
						newData.resize(newData.size() + Header().BlockSize);
						std::copy_n(&Data[BaseOffset() + offsets[i]], Header().BlockSize, &newData[BaseOffset() + newOffsets[i]]);
					}
				}
			}
			newData.resize(Sqex::Align<size_t>(newData.size(), 512).Alloc);
			Data.swap(newData);
			m_expanded = expand;
		}
	};

	class EqpGmpFile {
		bool m_expanded = false;

	public:
		static constexpr size_t CountPerBlock = 160;

		std::vector<uint64_t> Data;

		EqpGmpFile(const RandomAccessStream& stream)
			: Data(stream.ReadStreamIntoVector<uint64_t>(0)) {
		}

		uint64_t& BlockBits() {
			return Data[0];
		}

		const uint64_t& BlockBits() const {
			return Data[0];
		}

		std::span<uint64_t> Block(size_t index) {
			size_t populatedIndex = 0;
			for (size_t i = 0; i < index; i++) {
				if (BlockBits() & (size_t{ 1 } << i))
					populatedIndex++;
			}
			return std::span(Data).subspan(CountPerBlock * populatedIndex, CountPerBlock);
		}

		std::span<const uint64_t> Block(size_t index) const {
			size_t populatedIndex = 0;
			for (size_t i = 0; i < index; i++) {
				if (BlockBits() & (size_t{ 1 } << i))
					populatedIndex++;
			}
			return std::span(Data).subspan(CountPerBlock * populatedIndex, CountPerBlock);
		}

		void ExpandCollapseAll(bool expand) {
			if (m_expanded == expand)
				return;
			
			std::vector<uint64_t> newData;
			newData.reserve(CountPerBlock * 64);

			uint64_t populatedBits = 0;

			size_t sourceIndex = 0, targetIndex = 0;
			for (size_t i = 0; i < 64; i++) {
				if (BlockBits() & (size_t{ 1 } << i)) {
					const auto currentSourceIndex = sourceIndex;
					sourceIndex++;

					if (!expand) {
						bool isAllZeros = true;
						for (size_t j = currentSourceIndex * CountPerBlock, j_ = j + CountPerBlock; isAllZeros && j < j_; ++j) {
							isAllZeros = Data[j] == 0;
						}
						if (isAllZeros)
							continue;
					}
					populatedBits |= uint64_t{ 1 } << i;
					newData.resize(newData.size() + CountPerBlock);
					std::copy_n(&Data[currentSourceIndex * CountPerBlock], CountPerBlock, &newData[targetIndex * CountPerBlock]);
					targetIndex++;
				} else {
					if (expand) {
						populatedBits |= uint64_t{ 1 } << i;
						newData.resize(newData.size() + CountPerBlock);
						targetIndex++;
					}
				}
			}
			newData[0] = populatedBits;

			m_expanded = expand;
			Data.swap(newData);
		}
	};

	struct EstEntryDescriptor {
		uint16_t SetId;
		uint16_t RaceCode;

		bool operator<(const EstEntryDescriptor& r) const {
			return RaceCode == r.RaceCode ? SetId < r.SetId : RaceCode < r.RaceCode;
		}

		bool operator>(const EstEntryDescriptor& r) const {
			return RaceCode == r.RaceCode ? SetId > r.SetId : RaceCode > r.RaceCode;
		}

		bool operator==(const EstEntryDescriptor& r) const {
			return SetId == r.SetId && RaceCode == r.RaceCode;
		}

		bool operator!=(const EstEntryDescriptor& r) const {
			return SetId != r.SetId || RaceCode != r.RaceCode;
		}
	};

	class EstFile {
	public:
		std::vector<uint8_t> Data;

		EstFile(const RandomAccessStream& stream)
			: Data(stream.ReadStreamIntoVector<uint8_t>(0)) {
		}

		uint32_t& Count() {
			return *reinterpret_cast<uint32_t*>(&Data[0]);
		}

		const uint32_t& Count() const {
			return *reinterpret_cast<const uint32_t*>(&Data[0]);
		}

		EstEntryDescriptor& Descriptor(size_t index) {
			return reinterpret_cast<EstEntryDescriptor*>(&Data[4])[index];
		}

		const EstEntryDescriptor& Descriptor(size_t index) const {
			return reinterpret_cast<const EstEntryDescriptor*>(&Data[4])[index];
		}

		std::span<EstEntryDescriptor> Descriptors() {
			return { reinterpret_cast<EstEntryDescriptor*>(&Data[4]), Count() };
		}

		std::span<const EstEntryDescriptor> Descriptors() const {
			return { reinterpret_cast<const EstEntryDescriptor*>(&Data[4]), Count() };
		}

		uint16_t& SkelId(size_t index) {
			return reinterpret_cast<uint16_t*>(&Data[4 + Descriptors().size_bytes()])[index];
		}

		const uint16_t& SkelId(size_t index) const {
			return reinterpret_cast<const uint16_t*>(&Data[4 + Descriptors().size_bytes()])[index];
		}

		std::span<uint16_t> SkelIds() {
			return { reinterpret_cast<uint16_t*>(&Data[4 + Descriptors().size_bytes()]), Count() };
		}

		std::span<const uint16_t> SkelIds() const {
			return { reinterpret_cast<const uint16_t*>(&Data[4 + Descriptors().size_bytes()]), Count() };
		}

		std::map<Sqex::EstEntryDescriptor, uint16_t> ToPairs() const {
			std::map<Sqex::EstEntryDescriptor, uint16_t> res;
			for (size_t i = 0, i_ = Count(); i < i_; ++i)
				res.emplace(Descriptor(i), SkelId(i));
			return res;
		}

		void Update(const std::map<Sqex::EstEntryDescriptor, uint16_t>& pairs) {
			Data.resize(4 + pairs.size() * 6);
			Count() = static_cast<uint32_t>(pairs.size());
			
			size_t i = 0;
			for (const auto& [descriptor, skelId] : pairs) {
				Descriptor(i) = descriptor;
				SkelId(i) = skelId;
				i++;
			}
		}
	};
}

namespace Sqex::ThirdParty::TexTools {
	class ItemMetadata {
	public:
		static constexpr uint32_t Version_Value = 2;
		static const srell::u8cregex CharacterMetaPathTest;
		static const srell::u8cregex HousingMetaPathTest;

		enum class MetaDataType : uint32_t {
			Invalid,
			Imc,
			Eqdp,
			Eqp,
			Est,
			Gmp,
		};

		enum class TargetEstType {
			Invalid,
			Face,
			Hair,
			Head,
			Body,
		};

		enum class TargetItemType {
			Invalid,
			Equipment,
			Accessory,
			Housing,
		};

		class NotItemMetadataError : public std::runtime_error{
			using std::runtime_error::runtime_error;
		};

#pragma pack(push, 1)
		struct MetaDataHeader {
			LE<uint32_t> EntryCount;
			LE<uint32_t> HeaderSize;
			LE<uint32_t> FirstEntryLocatorOffset;
		};

		struct MetaDataEntryLocator {
			LE<MetaDataType> Type;
			LE<uint32_t> Offset;
			LE<uint32_t> Size;
		};

		struct EqdpEntry {
			uint32_t RaceCode;
			uint8_t Value : 2;
			uint8_t Padding : 6;
		};
		static_assert(sizeof EqdpEntry == 5);

		struct GmpEntry {
			uint32_t Enabled : 1;
			uint32_t Animated : 1;
			uint32_t RotationA : 10;
			uint32_t RotationB : 10;
			uint32_t RotationC : 10;
			uint8_t UnknownLow : 4;
			uint8_t UnknownHigh : 4;
		};
		static_assert(sizeof GmpEntry == 5);

		struct EstEntry {
			uint16_t RaceCode;
			uint16_t SetId;
			uint16_t SkelId;
		};
#pragma pack(pop)

		const std::vector<uint8_t> Data;
		const uint32_t& Version;
		const std::string_view Path;
		const MetaDataHeader& Header;
		const std::span<const MetaDataEntryLocator> AllEntries;
		
		TargetItemType ItemType = TargetItemType::Invalid;
		TargetEstType EstType = TargetEstType::Invalid;
		std::string PrimaryType;
		std::string SecondaryType;
		std::string FullPathPrefix;
		uint16_t PrimaryId = 0;
		uint16_t SecondaryId = 0;
		size_t SlotIndex = 0;
		size_t EqpEntrySize = 0;
		size_t EqpEntryOffset = 0;

		ItemMetadata(const RandomAccessStream& stream)
			: Data(stream.ReadStreamIntoVector<uint8_t>(0))
			, Version(*reinterpret_cast<const uint32_t*>(&Data[0]))
			, Path(reinterpret_cast<const char*>(&Data[sizeof Version]))
			, Header(*reinterpret_cast<const MetaDataHeader*>(Path.data() + Path.size() + 1))
			, AllEntries(reinterpret_cast<const MetaDataEntryLocator*>(&Data[Header.FirstEntryLocatorOffset]), Header.EntryCount) {

			const auto pathStr = std::string(Path);
			if (srell::u8csmatch matches;
				srell::regex_search(pathStr, matches, CharacterMetaPathTest)) {
				PrimaryType = matches["PrimaryType"].str();
				PrimaryId = static_cast<uint16_t>(std::strtol(matches["PrimaryId"].str().c_str(), nullptr, 10));
				SecondaryType = matches["SecondaryType"].str();
				SecondaryId = static_cast<uint16_t>(std::strtol(matches["SecondaryId"].str().c_str(), nullptr, 10));
				FullPathPrefix = matches["FullPathPrefix"].str();
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

			} else if (srell::regex_search(pathStr, matches, HousingMetaPathTest)) {
				PrimaryType = matches["PrimaryType"].str();
				PrimaryId = static_cast<uint16_t>(std::strtol(matches["PrimaryId"].str().c_str(), nullptr, 10));
				ItemType = TargetItemType::Housing;

			} else {
				throw NotItemMetadataError("Unsupported meta file");
			}
		}

		template<typename T>
		std::span<const T> Get(MetaDataType type) const {
			for (const auto& entry : AllEntries) {
				if (entry.Type != type)
					continue;
				const auto spanBytes = std::span(Data).subspan(entry.Offset, entry.Size);
				return { reinterpret_cast<const T*>(spanBytes.data()), spanBytes.size_bytes() / sizeof T };
			}
			return {};
		}

		std::string ImcFilename() const {
			return std::format("{}.imc", FullPathPrefix);
		}
	};
}

const srell::u8cregex Sqex::ThirdParty::TexTools::ItemMetadata::CharacterMetaPathTest(
	"^(?<FullPathPrefix>chara"
	"/(?<PrimaryType>[a-z]+)"
	"/[a-z](?<PrimaryId>[0-9]+)"
	"(?:/obj"
	"/(?<SecondaryType>[a-z]+)"
	"/[a-z](?<SecondaryId>[0-9]+))?"
	"/.*?"
	")(?:_(?<Slot>[a-z]{3}))?\\.meta$"
	, srell::u8cregex::icase);
const srell::u8cregex Sqex::ThirdParty::TexTools::ItemMetadata::HousingMetaPathTest(
	"^(?<FullPathPrefix>bgcommon"
	"/hou"
	"/(?<PrimaryType>[a-z]+)"
	"/general"
	"/(?<PrimaryId>[0-9]+)"
	"/.*?"
	")\\.meta$"
	, srell::u8cregex::icase);

int main() {
	const Sqex::Sqpack::Reader reader(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\040000.win32.index)");

	auto estHead = Sqex::EstFile(*reader["chara/xls/charadb/extra_met.est"]);
	auto estBody = Sqex::EstFile(*reader["chara/xls/charadb/extra_top.est"]);
	auto estHair = Sqex::EstFile(*reader["chara/xls/charadb/hairskeletontemplate.est"]);
	auto estFace = Sqex::EstFile(*reader["chara/xls/charadb/faceskeletontemplate.est"]);

	auto eqp = Sqex::EqpGmpFile(*reader["chara/xls/equipmentparameter/equipmentparameter.eqp"]);
	eqp.ExpandCollapseAll(true);

	std::map<std::string, std::unique_ptr<Sqex::ImcFile>> imcs;

	std::map<uint32_t, std::unique_ptr<Sqex::EqdpFile>> eqdpEquip, eqdpAcc;

	auto gmp = Sqex::EqpGmpFile(*reader["chara/xls/equipmentparameter/gimmickparameter.gmp"]);
	gmp.ExpandCollapseAll(true);

	const auto ttmpl = Sqex::ThirdParty::TexTools::TTMPL::FromStream(Sqex::FileRandomAccessStream(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\TexToolsMods\Mod_Odin set with cape\TTMPL.mpl)"));
	const auto ttmpd = std::make_shared<Sqex::FileRandomAccessStream>(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\TexToolsMods\Mod_Odin set with cape\TTMPD.mpd)");
	for (const auto& ttmpEntry : ttmpl.SimpleModsList) {
		if (!ttmpEntry.FullPath.ends_with(".meta"))
			continue;
		try {
			const auto metadata = Sqex::ThirdParty::TexTools::ItemMetadata(Sqex::Sqpack::EntryRawStream(std::make_shared<Sqex::Sqpack::RandomAccessStreamAsEntryProviderView>(ttmpEntry.FullPath, ttmpd, ttmpEntry.ModOffset, ttmpEntry.ModSize)));
			if (const auto imcedit = metadata.Get<Sqex::ImcEntry>(Sqex::ThirdParty::TexTools::ItemMetadata::MetaDataType::Imc); !imcedit.empty()) {
				Sqex::ImcFile* imc = nullptr;
				if (const auto it = imcs.find(metadata.ImcFilename()); it == imcs.end()) {
					auto ptr = std::make_unique<Sqex::ImcFile>(*reader["chara/equipment/e0100/e0100.imc"]);
					imc = ptr.get();
					imcs.emplace(metadata.ImcFilename(), std::move(ptr));
				} else
					imc = it->second.get();
				for (size_t i = 0; i < imcedit.size(); ++i) {
					auto& target = imc->AllEntries()[i * imc->EntryCountPerSet() + metadata.SlotIndex];
					target = imcedit[i];
				}
			}
			if (const auto eqdpedit = metadata.Get<Sqex::ThirdParty::TexTools::ItemMetadata::EqdpEntry>(Sqex::ThirdParty::TexTools::ItemMetadata::MetaDataType::Eqdp); !eqdpedit.empty()) {
				for (const auto& v : eqdpedit) {
					Sqex::EqdpFile* eqdp = nullptr;
					if (metadata.ItemType == Sqex::ThirdParty::TexTools::ItemMetadata::TargetItemType::Equipment) {
						if (const auto it = eqdpEquip.find(v.RaceCode); it != eqdpEquip.end()) {
							eqdp = it->second.get();
						} else {
							eqdp = (eqdpEquip[v.RaceCode] = std::make_unique<Sqex::EqdpFile>(*reader[std::format("chara/xls/charadb/equipmentdeformerparameter/c{:04}.eqdp", v.RaceCode)])).get();
							eqdp->ExpandCollapseAll(true);
						}
					} else if (metadata.ItemType == Sqex::ThirdParty::TexTools::ItemMetadata::TargetItemType::Accessory) {
						if (const auto it = eqdpAcc.find(v.RaceCode); it != eqdpAcc.end()) {
							eqdp = it->second.get();
						} else {
							eqdp = (eqdpAcc[v.RaceCode] = std::make_unique<Sqex::EqdpFile>(*reader[std::format("chara/xls/charadb/accessorydeformerparameter/c{:04}.eqdp", v.RaceCode)])).get();
							eqdp->ExpandCollapseAll(true);
						}
					} else {
						throw std::runtime_error("Eqdp not applicable for non-equipment/accessory");
					}
					auto& target = eqdp->Set(metadata.PrimaryId);
					target &= ~(0b11 << (metadata.SlotIndex * 2));
					target |= v.Value << (metadata.SlotIndex * 2);
				}
			}
			if (const auto eqpedit = metadata.Get<uint8_t>(Sqex::ThirdParty::TexTools::ItemMetadata::MetaDataType::Eqp); !eqpedit.empty()) {
				const auto bytes = std::span(reinterpret_cast<uint8_t*>(&eqp.Data[metadata.PrimaryId]), 8);
				std::copy_n(&eqpedit[0], metadata.EqpEntrySize, &bytes[metadata.EqpEntryOffset]);
			}
			if (const auto gmpedit = metadata.Get<uint8_t>(Sqex::ThirdParty::TexTools::ItemMetadata::MetaDataType::Gmp); !gmpedit.empty()) {
				const auto bytes = std::span(reinterpret_cast<uint8_t*>(&eqp.Data[metadata.PrimaryId]), 8);
				std::copy_n(&gmpedit[0], gmpedit.size(), &bytes[0]);
			}
			if (const auto estedit = metadata.Get<Sqex::ThirdParty::TexTools::ItemMetadata::EstEntry>(Sqex::ThirdParty::TexTools::ItemMetadata::MetaDataType::Est); !estedit.empty()) {
				Sqex::EstFile* est = nullptr;
				if (metadata.EstType == Sqex::ThirdParty::TexTools::ItemMetadata::TargetEstType::Body)
					est = &estBody;
				else if (metadata.EstType == Sqex::ThirdParty::TexTools::ItemMetadata::TargetEstType::Face)
					est = &estFace;
				else if (metadata.EstType == Sqex::ThirdParty::TexTools::ItemMetadata::TargetEstType::Hair)
					est = &estHair;
				else if (metadata.EstType == Sqex::ThirdParty::TexTools::ItemMetadata::TargetEstType::Head)
					est = &estHead;
				else
					throw std::runtime_error("Est not applicable");

				auto estpairs = est->ToPairs();
				for (const auto& v : estedit) {
					const auto key = Sqex::EstEntryDescriptor{ .SetId = v.SetId, .RaceCode = v.RaceCode };
					if (v.SkelId == 0)
						estpairs.erase(key);
					else
						estpairs.insert_or_assign(key, v.SkelId);
				}
				est->Update(estpairs);
			}
		} catch (const Sqex::ThirdParty::TexTools::ItemMetadata::NotItemMetadataError&) {
			continue;
		}
	}

	Utils::Win32::Handle::FromCreateFile(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\chara/xls/charadb/extra_met.est)", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(estHead.Data));
	Utils::Win32::Handle::FromCreateFile(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\chara/xls/charadb/extra_top.est)", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(estBody.Data));
	Utils::Win32::Handle::FromCreateFile(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\chara/xls/charadb/hairskeletontemplate.est)", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(estHair.Data));
	Utils::Win32::Handle::FromCreateFile(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\chara/xls/charadb/faceskeletontemplate.est)", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(estFace.Data));

	eqp.ExpandCollapseAll(false);
	Utils::Win32::Handle::FromCreateFile(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\chara/xls/equipmentparameter/equipmentparameter.eqp)", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(eqp.Data));

	gmp.ExpandCollapseAll(false);
	Utils::Win32::Handle::FromCreateFile(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\chara/xls/equipmentparameter/gimmickparameter.eqp)", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(gmp.Data));

	for (const auto& [race, d] : eqdpEquip) {
		d->ExpandCollapseAll(false);
		Utils::Win32::Handle::FromCreateFile(std::format(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\chara/xls/charadb/equipmentdeformerparameter/c{:04}.eqdp)", race), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(d->Data));
	}
	for (const auto& [race, d] : eqdpAcc) {
		d->ExpandCollapseAll(false);
		Utils::Win32::Handle::FromCreateFile(std::format(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\chara/xls/charadb/accessorydeformerparameter/c{:04}.eqdp)", race), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(d->Data));
	}
	for (const auto& [filename, d] : imcs) {
		Utils::Win32::Handle::FromCreateFile(std::format(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\{})", filename), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS).Write(0, std::span(d->Data));
	}
	return 0;
}