#pragma once

#include <string>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>

#include "XivAlexanderCommon/Sqex.h"
#include "XivAlexanderCommon/Sqex/Eqdp.h"
#include "XivAlexanderCommon/Sqex/EqpGmp.h"
#include "XivAlexanderCommon/Sqex/Est.h"
#include "XivAlexanderCommon/Sqex/Imc.h"

namespace Sqex::ThirdParty::TexTools {

	struct ModPackEntry {
		std::string Name;
		std::string Author;
		std::string Version;
		std::string Url;
	};
	void to_json(nlohmann::json&, const ModPackEntry&);
	void from_json(const nlohmann::json&, ModPackEntry&);

	struct ModEntry {
		std::string Name;
		std::string Category;
		std::string FullPath;
		uint64_t ModOffset{};
		uint64_t ModSize{};
		std::string DatFile;
		bool IsDefault{};
		std::optional<ModPackEntry> ModPack;

		std::string ToExpacDatPath() const;
		bool IsMetadata() const;
	};
	void to_json(nlohmann::json&, const ModEntry&);
	void from_json(const nlohmann::json&, ModEntry&);

	namespace ModPackPage {
		struct Option {
			std::string Name;
			std::string Description;
			std::string ImagePath;
			std::vector<ModEntry> ModsJsons;
			std::string GroupName;
			std::string SelectionType;
			bool IsChecked;
		};
		void to_json(nlohmann::json&, const Option&);
		void from_json(const nlohmann::json&, Option&);

		struct ModGroup {
			std::string GroupName;
			std::string SelectionType;
			std::vector<Option> OptionList;
		};
		void to_json(nlohmann::json&, const ModGroup&);
		void from_json(const nlohmann::json&, ModGroup&);

		struct Page {
			int PageIndex{};
			std::vector<ModGroup> ModGroups;
		};
		void to_json(nlohmann::json&, const Page&);
		void from_json(const nlohmann::json&, Page&);

	}

	struct TTMPL {
		std::string MinimumFrameworkVersion;
		std::string FormatVersion;
		std::string Name;
		std::string Author;
		std::string Version;
		std::string Description;
		std::string Url;
		std::vector<ModPackPage::Page> ModPackPages;
		std::vector<ModEntry> SimpleModsList;

		static TTMPL FromStream(const RandomAccessStream& stream);

		enum TraverseCallbackResult {
			Continue,
			Break,
		};

		void ForEachEntry(std::function<void(Sqex::ThirdParty::TexTools::ModEntry&)> cb);
		void ForEachEntry(std::function<void(const Sqex::ThirdParty::TexTools::ModEntry&)> cb) const;
		TraverseCallbackResult ForEachEntryInterruptible(std::function<TraverseCallbackResult(Sqex::ThirdParty::TexTools::ModEntry&)> cb);
		TraverseCallbackResult ForEachEntryInterruptible(std::function<TraverseCallbackResult(const Sqex::ThirdParty::TexTools::ModEntry&)> cb) const;
	};
	void to_json(nlohmann::json&, const TTMPL&);
	void from_json(const nlohmann::json&, TTMPL&);

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

		class NotItemMetadataError : public std::runtime_error {
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
		const std::string TargetPath;
		const std::string SourcePath;
		const MetaDataHeader& Header;
		const std::span<const MetaDataEntryLocator> AllEntries;

		TargetItemType ItemType = TargetItemType::Invalid;
		TargetEstType EstType = TargetEstType::Invalid;
		std::string PrimaryType;
		std::string SecondaryType;
		std::string TargetImcPath;
		std::string SourceImcPath;
		uint16_t PrimaryId = 0;
		uint16_t SecondaryId = 0;
		size_t SlotIndex = 0;
		size_t EqpEntrySize = 0;
		size_t EqpEntryOffset = 0;

		ItemMetadata(std::string gamePath, const RandomAccessStream& stream);

		template<typename T>
		std::span<const T> Get(MetaDataType type) const {
			for (const auto& entry : AllEntries) {
				if (entry.Type != type)
					continue;
				const auto spanBytes = std::span(Data).subspan(entry.Offset, entry.Size);
				return { reinterpret_cast<const T*>(spanBytes.data()), spanBytes.size_bytes() / sizeof(T) };
			}
			return {};
		}

		static std::string EqdpPath(TargetItemType type, uint32_t race) {
			switch (type) {
				case TargetItemType::Equipment:
					return std::format("chara/xls/charadb/equipmentdeformerparameter/c{:04}.eqdp", race);
				case TargetItemType::Accessory:
					return std::format("chara/xls/charadb/accessorydeformerparameter/c{:04}.eqdp", race);
				default:
					throw std::invalid_argument("only equipment and accessory have valid eqdp");
			}
		}

		static constexpr auto EqpPath = "chara/xls/equipmentparameter/equipmentparameter.eqp";
		static constexpr auto GmpPath = "chara/xls/equipmentparameter/gimmickparameter.gmp";

		static const char* EstPath(TargetEstType type) {
			switch (type) {
				case TargetEstType::Face:
					return "chara/xls/charadb/faceskeletontemplate.est";
				case TargetEstType::Hair:
					return "chara/xls/charadb/hairskeletontemplate.est";
				case TargetEstType::Head:
					return "chara/xls/charadb/extra_met.est";
				case TargetEstType::Body:
					return "chara/xls/charadb/extra_top.est";
				default:
					return nullptr;
			}
		}

		void ApplyImcEdits(std::function<Sqex::Imc::File&()> reader) const;
		void ApplyEqdpEdits(std::function<Sqex::Eqdp::ExpandedFile& (TargetItemType, uint32_t)> reader) const;
		void ApplyEqpEdits(Sqex::EqpGmp::ExpandedFile& eqp) const;
		void ApplyGmpEdits(Sqex::EqpGmp::ExpandedFile& gmp) const;
		void ApplyEstEdits(Sqex::Est::File& est) const;
	};
}