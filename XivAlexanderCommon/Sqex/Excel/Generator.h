#pragma once
#include "XivAlexanderCommon/Sqex/Excel.h"
#include "XivAlexanderCommon/Sqex/Sqpack.h"

namespace Sqex::Excel {
	class Depth2ExhExdCreator {
	public:
		const std::string Name;
		const std::vector<Exh::Column> Columns;
		const Exh::ExhFlag Flags;
		const uint32_t FixedDataSize;
		std::map<uint32_t, std::map<Language, std::vector<ExdColumn>>> Data;
		std::set<uint32_t> DivideAtIds;
		std::vector<Language> Languages;
		std::vector<Language> FillMissingLanguageFrom;

		Depth2ExhExdCreator(std::string name, std::vector<Exh::Column> columns, const Exh::ExhFlag& flag);

		void AddLanguage(Language language);

		const std::vector<ExdColumn>& GetRow(uint32_t id, Language language) const;
		void SetRow(uint32_t id, Language language, std::vector<ExdColumn> row, bool replace = true);

	private:
		std::pair<Sqpack::EntryPathSpec, std::vector<char>> Flush(uint32_t startId, std::map<uint32_t, std::vector<char>> rows, Language language);

	public:
		std::map<Sqpack::EntryPathSpec, std::vector<char>, Sqpack::EntryPathSpec::FullPathComparator> Compile(size_t divideUnit = SIZE_MAX);
	};
}
