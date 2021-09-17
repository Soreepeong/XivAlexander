#pragma once
#include "Sqex.h"
#include "Sqex_Excel.h"
#include "Sqex_Sqpack.h"

namespace Sqex::Excel {

	class ExlReader {
		int m_version = 0;
		std::map<std::string, int> m_nameToIdMap;
		std::map<int, std::string> m_idToNameMap;

	public:
		ExlReader(const RandomAccessStream& stream);

		const std::string& operator[](int id) const {
			return m_idToNameMap.at(id);
		}

		int operator[](const std::string& s) const {
			return m_nameToIdMap.at(s);
		}

		[[nodiscard]] auto begin() const {
			return m_nameToIdMap.begin();
		}

		[[nodiscard]] auto end() const {
			return m_nameToIdMap.end();
		}

		[[nodiscard]] auto rbegin() const {
			return m_nameToIdMap.rbegin();
		}

		[[nodiscard]] auto rend() const {
			return m_nameToIdMap.rend();
		}
	};

	class ExhReader;

	class ExhReader {
	public:
		std::string Name;
		Exh::Header Header;
		std::shared_ptr<std::vector<Exh::Column>> Columns;
		std::vector<Exh::Pagination> Pages;
		std::vector<Language> Languages;

		ExhReader(std::string name, const RandomAccessStream& stream, bool strict = false);

		[[nodiscard]] Sqpack::EntryPathSpec GetDataPathSpec(const Exh::Pagination& page, Language language) const;

		void Dump() const;
	};

	class ExdReader {
		const std::shared_ptr<const RandomAccessStream> m_stream;
		const size_t m_fixedDataSize;
		const Exh::Depth m_depth;
		std::vector<std::pair<uint32_t, uint32_t>> m_rowLocators;

	public:
		const Exd::Header Header;
		const std::shared_ptr<std::vector<Exh::Column>> ColumnDefinitions;

		ExdReader(const ExhReader& exh, std::shared_ptr<const RandomAccessStream> stream, bool strict = false);

	private:
		[[nodiscard]] ExdColumn TranslateColumn(const Exh::Column& columnDefinition, std::span<const char> fixedData, std::span<const char> fullData) const;

		[[nodiscard]] std::pair<Exd::RowHeader, std::vector<char>> ReadRowRaw(uint32_t index) const;

	public:
		[[nodiscard]] std::vector<ExdColumn> ReadDepth2(uint32_t index) const;

		[[nodiscard]] std::vector<std::vector<ExdColumn>> ReadDepth3(uint32_t index) const;

		[[nodiscard]] std::vector<uint32_t> GetIds() const;
	};
}
