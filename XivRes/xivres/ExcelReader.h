#ifndef _XIVRES_EXCELREADER_H_
#define _XIVRES_EXCELREADER_H_

#include <map>
#include <ranges>

#include "Internal/String.h"

#include "Common.h"
#include "GameReader.h"
#include "Excel.h"
#include "IStream.h"
#include "SqpackReader.h"
#include "PackedFileUnpackingStream.h"

namespace XivRes {
	class ExlReader {
		int m_version = 0;
		std::map<std::string, int> m_nameToIdMap;
		std::map<int, std::string> m_idToNameMap;

	public:
		ExlReader(const IStream& stream) {
			std::string data(static_cast<size_t>(stream.StreamSize()), '\0');
			ReadStream(stream, 0, std::span(data));
			std::istringstream in(std::move(data));

			std::string line;
			for (size_t i = 0; std::getline(in, line); ++i) {
				line = Internal::StringTrim(line);
				if (line.empty())
					continue;  // skip empty lines
				auto split = Internal::StringSplit<std::string>(line, ",", 1);
				if (split.size() < 2)
					throw CorruptDataException("Could not find delimiter character ','");

				char* end;
				const auto id = std::strtol(split[1].data(), &end, 10);
				if (end != &split[1].back() + 1)
					throw CorruptDataException("Bad numeric specification");
				if (!i) {
					if (split[0] != "EXLT")
						throw CorruptDataException("Bad header");
					m_version = id;
				} else {
					if (id != -1)
						m_idToNameMap.emplace(id, split[0]);
					m_nameToIdMap.emplace(std::move(split[0]), id);
				}
			}
		}

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

	class ExhReader {
	private:
		std::string m_name;
		ExhHeader m_header;
		std::vector<ExhColHeader> m_columns;
		std::vector<ExhPagination> m_pages;
		std::vector<Language> m_languages;

	public:
		ExhReader(std::string name, const IStream& stream, bool strict = false)
			: m_name(std::move(name))
			, m_header(ReadStream<ExhHeader>(stream, 0))
			, m_columns(ReadStreamIntoVector<ExhColHeader>(stream, sizeof m_header, m_header.ColumnCount))
			, m_pages(ReadStreamIntoVector<ExhPagination>(stream, sizeof m_header + std::span(m_columns).size_bytes(), m_header.PageCount))
			, m_languages(ReadStreamIntoVector<Language>(stream, sizeof m_header + std::span(m_columns).size_bytes() + std::span(m_pages).size_bytes(), m_header.LanguageCount)) {

			if (strict) {
				const auto dataLength = static_cast<size_t>(stream.StreamSize());
				const auto expectedLength = sizeof m_header + std::span(m_columns).size_bytes() + std::span(m_pages).size_bytes() + std::span(m_languages).size_bytes();
				if (dataLength > expectedLength)
					throw CorruptDataException(std::format("Extra {} byte(s) found", dataLength - expectedLength));
				if (m_header.Version != ExhHeader::Version_Value)
					throw CorruptDataException(std::format("Unsupported version {}", *m_header.Version));
			}
		}

		const std::string& Name() const {
			return m_name;
		}

		const ExhHeader& Header() const {
			return m_header;
		}

		const std::vector<ExhColHeader>& Columns() const {
			return m_columns;
		}

		const std::vector<ExhPagination>& Pages() const {
			return m_pages;
		}

		const std::vector<Language>& Languages() const {
			return m_languages;
		}

		size_t GetPageIndex(uint32_t rowId) const {
			auto it = std::lower_bound(m_pages.begin(), m_pages.end(), rowId, [](const ExhPagination& l, uint32_t r) { return l.StartId + l.RowCountWithSkip <= r; });
			if (it == m_pages.end())
				throw std::out_of_range("RowId not in range");

			return std::distance(m_pages.begin(), it);
		}

		const ExhPagination& GetPage(uint32_t rowId) const {
			return m_pages[GetPageIndex(rowId)];
		}

		[[nodiscard]] SqpackPathSpec GetDataPathSpec(const ExhPagination& page, Language language) const {
			const auto* languageCode = "";
			switch (language) {
				case Language::Unspecified:
					break;
				case Language::Japanese:
					languageCode = "_ja";
					break;
				case Language::English:
					languageCode = "_en";
					break;
				case Language::German:
					languageCode = "_de";
					break;
				case Language::French:
					languageCode = "_fr";
					break;
				case Language::ChineseSimplified:
					languageCode = "_chs";
					break;
				case Language::ChineseTraditional:
					languageCode = "_cht";
					break;
				case Language::Korean:
					languageCode = "_ko";
					break;
				default:
					throw std::invalid_argument("Invalid language");
			}
			return std::format("exd/{}_{}{}.exd", m_name, *page.StartId, languageCode);
		}
	};

	class ExdRow {
		const std::span<const char> m_fixedData;
		const std::span<const char> m_fullData;
		mutable std::vector<std::optional<ExcelCell>> m_cells;

	public:
		const std::vector<ExhColHeader>& Columns;

		ExdRow(std::span<const char> fixedData, std::span<const char> fullData, const std::vector<ExhColHeader>& columns)
			: m_fixedData(fixedData)
			, m_fullData(fullData)
			, m_cells(columns.size())
			, Columns(columns) {}

		ExcelCell& operator[](size_t index) {
			return ResolveCell(index);
		}

		const ExcelCell& operator[](size_t index) const {
			return ResolveCell(index);
		}

		ExcelCell& at(size_t index) {
			if (index >= Columns.size())
				throw std::out_of_range("Index out of range");
			return ResolveCell(index);
		}

		const ExcelCell& at(size_t index) const {
			if (index >= Columns.size())
				throw std::out_of_range("Index out of range");
			return ResolveCell(index);
		}

		size_t size() const {
			return m_cells.size();
		}

		template<typename TParent, typename T, bool reversed>
		class base_iterator {
		public:
			using iterator = base_iterator<TParent, T, reversed>;
			using iterator_category = std::random_access_iterator_tag;
			using value_type = T;
			using difference_type = ptrdiff_t;
			using pointer = T*;
			using reference = T&;

		private:
			TParent* m_parent;
			difference_type m_index;

		public:
			base_iterator(TParent* parent, difference_type index)
				: m_parent(parent)
				, m_index(CheckBoundaryOrThrow(index)) {}

			base_iterator(const iterator& r)
				: m_parent(r.m_parent)
				, m_index(r.m_index) {}

			~base_iterator() = default;

			iterator& operator=(const iterator& r) {
				m_parent = r.m_parent;
				m_index = r.m_index;
			}

			iterator& operator++() { //prefix increment
				m_index = CheckBoundaryOrThrow(m_index + 1);
				return *this;
			}

			reference operator*() {
				return m_parent->ResolveCell(reversed ? m_parent->size() - 1 - m_index : m_index);
			}

			friend void swap(iterator& lhs, iterator& rhs) {
				std::swap(lhs.m_parent, rhs.m_parent);
				std::swap(lhs.m_index, rhs.m_index);
			}

			iterator operator++(int) { //postfix increment
				const auto index = m_index;
				m_index = CheckBoundaryOrThrow(m_index + 1);
				return { m_parent, index };
			}

			pointer operator->() const {
				return &m_parent->ResolveCell(reversed ? m_parent->size() - 1 - m_index : m_index);
			}

			friend bool operator==(const iterator& l, const iterator& r) {
				return l.m_parent == r.m_parent && l.m_index == r.m_index;
			}

			friend bool operator!=(const iterator& l, const iterator& r) {
				return l.m_parent != r.m_parent || l.m_index != r.m_index;
			}

			reference operator*() const {
				return m_parent->ResolveCell(reversed ? m_parent->size() - 1 - m_index : m_index);
			}

			iterator& operator--() { //prefix decrement
				m_index = CheckBoundaryOrThrow(m_index - 1);
				return *this;
			}

			iterator operator--(int) { //postfix decrement
				const auto index = m_index;
				m_index = CheckBoundaryOrThrow(m_index - 1);
				return { m_parent, index };
			}

			friend bool operator<(const iterator& l, const iterator& r) {
				return l.m_index < r.m_index;
			}

			friend bool operator>(const iterator& l, const iterator& r) {
				return l.m_index > r.m_index;
			}

			friend bool operator<=(const iterator& l, const iterator& r) {
				return l.m_index <= r.m_index;
			}

			friend bool operator>=(const iterator& l, const iterator& r) {
				return l.m_index >= r.m_index;
			}

			iterator& operator+=(difference_type n) {
				m_index = CheckBoundaryOrThrow(m_index + n);
				return *this;
			}

			friend iterator operator+(const iterator& l, difference_type r) {
				return iterator(l.m_parent, l.m_index + r);
			}

			friend iterator operator+(difference_type l, const iterator& r) {
				return iterator(r.m_parent, r.m_index + l);
			}

			iterator& operator-=(difference_type n) {
				m_index = CheckBoundaryOrThrow(m_index - n);
				return *this;
			}

			friend iterator operator-(const iterator& l, difference_type r) {
				return iterator(l.m_parent, l.m_index - r);
			}

			friend difference_type operator-(const iterator& l, const iterator& r) {
				return r.m_index - l.m_index;
			}

			reference operator[](difference_type n) const {
				const auto index = CheckBoundaryOrThrow(m_index + n);
				return m_parent->ResolveCell(reversed ? m_parent->size() - 1 - index : index);
			}

		private:
			size_t CheckBoundaryOrThrow(size_t n) const {
				if (n > m_parent->size())
					throw std::out_of_range("Reached end of iterator.");
				if (n < 0)
					throw std::out_of_range("Reached beginning of iterator.");
				return n;
			}
		};

		using iterator = base_iterator<ExdRow, ExcelCell, false>;
		using const_iterator = base_iterator<const ExdRow, const ExcelCell, false>;
		using reverse_iterator = base_iterator<ExdRow, ExcelCell, true>;
		using const_reverse_iterator = base_iterator<const ExdRow, const ExcelCell, true>;

		iterator begin() {
			return iterator(this, 0);
		}

		const_iterator begin() const {
			return const_iterator(this, 0);
		}

		const_iterator cbegin() const {
			return const_iterator(this, 0);
		}

		iterator end() {
			return iterator(this, Columns.size());
		}

		const_iterator end() const {
			return const_iterator(this, Columns.size());
		}

		const_iterator cend() const {
			return const_iterator(this, Columns.size());
		}

		reverse_iterator rbegin() {
			return reverse_iterator(this, 0);
		}

		const_reverse_iterator rbegin() const {
			return const_reverse_iterator(this, 0);
		}

		const_reverse_iterator crbegin() const {
			return const_reverse_iterator(this, 0);
		}

		reverse_iterator rend() {
			return reverse_iterator(this, Columns.size());
		}

		const_iterator rend() const {
			const_reverse_iterator const_reverse_iterator(this, Columns.size());
		}

		const_reverse_iterator crend() const {
			return const_reverse_iterator(this, Columns.size());
		}

	private:
		ExcelCell& ResolveCell(size_t index) const {
			if (m_cells[index])
				return *m_cells[index];

			const auto& columnDefinition = Columns[index];
			auto& column = m_cells[index].emplace();
			column.Type = columnDefinition.Type;
			switch (column.Type) {
				case ExcelCellType::String:
				{
					BE<uint32_t> stringOffset;
					std::copy_n(&m_fixedData[columnDefinition.Offset], 4, reinterpret_cast<char*>(&stringOffset));
					column.String.SetEscaped(&m_fullData[m_fixedData.size() + stringOffset]);
					break;
				}

				case ExcelCellType::Bool:
				case ExcelCellType::Int8:
				case ExcelCellType::UInt8:
					column.ValidSize = 1;
					break;

				case ExcelCellType::Int16:
				case ExcelCellType::UInt16:
					column.ValidSize = 2;
					break;

				case ExcelCellType::Int32:
				case ExcelCellType::UInt32:
				case ExcelCellType::Float32:
					column.ValidSize = 4;
					break;

				case ExcelCellType::Int64:
				case ExcelCellType::UInt64:
					column.ValidSize = 8;
					break;

				case ExcelCellType::PackedBool0:
				case ExcelCellType::PackedBool1:
				case ExcelCellType::PackedBool2:
				case ExcelCellType::PackedBool3:
				case ExcelCellType::PackedBool4:
				case ExcelCellType::PackedBool5:
				case ExcelCellType::PackedBool6:
				case ExcelCellType::PackedBool7:
					column.boolean = m_fixedData[columnDefinition.Offset] & (1 << (static_cast<int>(column.Type) - static_cast<int>(ExcelCellType::PackedBool0)));
					break;

				default:
					throw CorruptDataException(std::format("Invald column type {}", static_cast<uint32_t>(column.Type)));
			}
			if (column.ValidSize) {
				std::copy_n(&m_fixedData[columnDefinition.Offset], column.ValidSize, &column.Buffer[0]);
				std::reverse(&column.Buffer[0], &column.Buffer[column.ValidSize]);
			}
			return column;
		}
	};

	class ExdRowBuffer {
		uint32_t m_rowId;
		ExdRowHeader m_rowHeader;
		std::vector<char> m_buffer;
		std::vector<ExdRow> m_rows;

	public:
		ExdRowBuffer()
			: m_rowId(UINT32_MAX) {}

		ExdRowBuffer(uint32_t rowId, const ExhReader& exh, const IStream& stream, std::streamoff offset)
			: m_rowId(rowId)
			, m_rowHeader(ReadStream<ExdRowHeader>(stream, offset))
			, m_buffer(ReadStreamIntoVector<char>(stream, offset + sizeof m_rowHeader, m_rowHeader.DataSize)) {

			m_rows.reserve(m_rowHeader.SubRowCount);

			if (exh.Header().Depth == ExcelDepth::Level2) {
				const auto fixedData = std::span(m_buffer).subspan(0, exh.Header().FixedDataSize);

				for (size_t i = 0, i_ = m_rowHeader.SubRowCount; i < i_; i++)
					m_rows.emplace_back(fixedData, std::span(m_buffer), exh.Columns());

			} else if (exh.Header().Depth == ExcelDepth::Level3) {
				for (size_t i = 0, i_ = m_rowHeader.SubRowCount; i < i_; ++i) {
					const auto baseOffset = i * (size_t() + 2 + exh.Header().FixedDataSize);
					const auto fixedData = std::span(m_buffer).subspan(2 + baseOffset, exh.Header().FixedDataSize);

					m_rows.emplace_back(fixedData, std::span(m_buffer), exh.Columns());
				}

			} else {
				throw CorruptDataException("Invalid excel depth");
			}
		}

		[[nodiscard]] ExdRow& operator[](size_t index) {
			return m_rows.at(index);
		}

		[[nodiscard]] const ExdRow& operator[](size_t index) const {
			return m_rows.at(index);
		}

		[[nodiscard]] uint32_t RowId() const {
			return m_rowId;
		}

		size_t size() const {
			return m_rows.size();
		}

		template<typename TParent, typename T, bool reversed>
		class base_iterator {
		public:
			using iterator = base_iterator<TParent, T, reversed>;
			using iterator_category = std::random_access_iterator_tag;
			using value_type = T;
			using difference_type = ptrdiff_t;
			using pointer = T*;
			using reference = T&;

		private:
			TParent* m_parent;
			difference_type m_index;

		public:
			base_iterator(TParent* parent, difference_type index)
				: m_parent(parent)
				, m_index(CheckBoundaryOrThrow(index)) {}

			base_iterator(const iterator& r)
				: m_parent(r.m_parent)
				, m_index(r.m_index) {}

			~base_iterator() = default;

			iterator& operator=(const iterator& r) {
				m_parent = r.m_parent;
				m_index = r.m_index;
			}

			iterator& operator++() { //prefix increment
				m_index = CheckBoundaryOrThrow(m_index + 1);
				return *this;
			}

			reference operator*() {
				return (*m_parent)[reversed ? m_parent->size() - 1 - m_index : m_index];
			}

			friend void swap(iterator& lhs, iterator& rhs) {
				std::swap(lhs.m_parent, rhs.m_parent);
				std::swap(lhs.m_index, rhs.m_index);
			}

			iterator operator++(int) { //postfix increment
				const auto index = m_index;
				m_index = CheckBoundaryOrThrow(m_index + 1);
				return { m_parent, index };
			}

			pointer operator->() const {
				return &(*m_parent)[reversed ? m_parent->size() - 1 - m_index : m_index];
			}

			friend bool operator==(const iterator& l, const iterator& r) {
				return l.m_parent == r.m_parent && l.m_index == r.m_index;
			}

			friend bool operator!=(const iterator& l, const iterator& r) {
				return l.m_parent != r.m_parent || l.m_index != r.m_index;
			}

			reference operator*() const {
				return (*m_parent)[reversed ? m_parent->size() - 1 - m_index : m_index];
			}

			iterator& operator--() { //prefix decrement
				m_index = CheckBoundaryOrThrow(m_index - 1);
				return *this;
			}

			iterator operator--(int) { //postfix decrement
				const auto index = m_index;
				m_index = CheckBoundaryOrThrow(m_index - 1);
				return { m_parent, index };
			}

			friend bool operator<(const iterator& l, const iterator& r) {
				return l.m_index < r.m_index;
			}

			friend bool operator>(const iterator& l, const iterator& r) {
				return l.m_index > r.m_index;
			}

			friend bool operator<=(const iterator& l, const iterator& r) {
				return l.m_index <= r.m_index;
			}

			friend bool operator>=(const iterator& l, const iterator& r) {
				return l.m_index >= r.m_index;
			}

			iterator& operator+=(difference_type n) {
				m_index = CheckBoundaryOrThrow(m_index + n);
				return *this;
			}

			friend iterator operator+(const iterator& l, difference_type r) {
				return iterator(l.m_parent, l.m_index + r);
			}

			friend iterator operator+(difference_type l, const iterator& r) {
				return iterator(r.m_parent, r.m_index + l);
			}

			iterator& operator-=(difference_type n) {
				m_index = CheckBoundaryOrThrow(m_index - n);
				return *this;
			}

			friend iterator operator-(const iterator& l, difference_type r) {
				return iterator(l.m_parent, l.m_index - r);
			}

			friend difference_type operator-(const iterator& l, const iterator& r) {
				return r.m_index - l.m_index;
			}

			reference operator[](difference_type n) const {
				const auto index = CheckBoundaryOrThrow(m_index + n);
				return (*m_parent)[reversed ? m_parent->size() - 1 - index : index];
			}

		private:
			size_t CheckBoundaryOrThrow(size_t n) const {
				if (n > m_parent->size())
					throw std::out_of_range("Reached end of iterator.");
				if (n < 0)
					throw std::out_of_range("Reached beginning of iterator.");
				return n;
			}
		};

		using iterator = base_iterator<ExdRowBuffer, ExdRow, false>;
		using const_iterator = base_iterator<const ExdRowBuffer, const ExdRow, false>;
		using reverse_iterator = base_iterator<ExdRowBuffer, ExdRow, true>;
		using const_reverse_iterator = base_iterator<const ExdRowBuffer, const ExdRow, true>;

		iterator begin() {
			return iterator(this, 0);
		}

		const_iterator begin() const {
			return const_iterator(this, 0);
		}

		const_iterator cbegin() const {
			return const_iterator(this, 0);
		}

		iterator end() {
			return iterator(this, m_rows.size());
		}

		const_iterator end() const {
			return const_iterator(this, m_rows.size());
		}

		const_iterator cend() const {
			return const_iterator(this, m_rows.size());
		}

		reverse_iterator rbegin() {
			return reverse_iterator(this, 0);
		}

		const_reverse_iterator rbegin() const {
			return const_reverse_iterator(this, 0);
		}

		const_reverse_iterator crbegin() const {
			return const_reverse_iterator(this, 0);
		}

		reverse_iterator rend() {
			return reverse_iterator(this, m_rows.size());
		}

		const_iterator rend() const {
			const_reverse_iterator const_reverse_iterator(this, m_rows.size());
		}

		const_reverse_iterator crend() const {
			return const_reverse_iterator(this, m_rows.size());
		}
	};

	class ExdReader {
	public:
		const std::shared_ptr<const IStream> Stream;
		const ExhReader& Exh;
		const ExdHeader Header;

	private:
		std::vector<uint32_t> m_rowIds;
		std::vector<uint32_t> m_offsets;

		mutable std::vector<std::optional<ExdRowBuffer>> m_rowBuffers;
		mutable std::mutex m_populateMtx;

	public:
		ExdReader(const ExhReader& exh, std::shared_ptr<const IStream> stream)
			: Stream(std::move(stream))
			, Exh(exh)
			, Header(ReadStream<ExdHeader>(*Stream, 0)) {

			const auto count = Header.IndexSize / sizeof ExdRowLocator;
			m_rowIds.reserve(count);
			m_offsets.reserve(count);
			m_rowBuffers.resize(count);

			std::vector<std::pair<uint32_t, uint32_t>> locators;
			locators.reserve(count);
			for (const auto& locator : ReadStreamIntoVector<ExdRowLocator>(*Stream, sizeof Header, count))
				locators.emplace_back(std::make_pair(*locator.RowId, *locator.Offset));
			std::ranges::sort(locators);

			for (const auto& locator : locators) {
				m_rowIds.emplace_back(locator.first);
				m_offsets.emplace_back(locator.second);
			}
		}

		[[nodiscard]] const ExdRowBuffer& operator[](uint32_t rowId) const {
			const auto it = std::ranges::lower_bound(m_rowIds, rowId);
			if (it == m_rowIds.end() || *it != rowId)
				throw std::out_of_range("index out of range");

			const auto index = it - m_rowIds.begin();
			if (!m_rowBuffers[index]) {
				const auto lock = std::lock_guard(m_populateMtx);
				if (!m_rowBuffers[index])
					m_rowBuffers[index].emplace(rowId, Exh, *Stream, m_offsets[index]);
			}

			return *m_rowBuffers[index];
		}

		[[nodiscard]] const std::vector<uint32_t>& RowIds() const {
			return m_rowIds;
		}

		size_t size() const {
			return m_rowIds.size();
		}

		template<typename TParent, typename T, bool reversed>
		class base_iterator {
		public:
			using iterator = base_iterator<TParent, T, reversed>;
			using iterator_category = std::random_access_iterator_tag;
			using value_type = T;
			using difference_type = ptrdiff_t;
			using pointer = T*;
			using reference = T&;

		private:
			TParent* m_parent;
			difference_type m_index;

		public:
			base_iterator(TParent* parent, difference_type index)
				: m_parent(parent)
				, m_index(CheckBoundaryOrThrow(index)) {}

			base_iterator(const iterator& r)
				: m_parent(r.m_parent)
				, m_index(r.m_index) {}

			~base_iterator() = default;

			iterator& operator=(const iterator& r) {
				m_parent = r.m_parent;
				m_index = r.m_index;
			}

			iterator& operator++() { //prefix increment
				m_index = CheckBoundaryOrThrow(m_index + 1);
				return *this;
			}

			reference operator*() {
				return (*m_parent)[m_parent->m_rowIds[reversed ? m_parent->size() - 1 - m_index : m_index]];
			}

			friend void swap(iterator& lhs, iterator& rhs) {
				std::swap(lhs.m_parent, rhs.m_parent);
				std::swap(lhs.m_index, rhs.m_index);
			}

			iterator operator++(int) { //postfix increment
				const auto index = m_index;
				m_index = CheckBoundaryOrThrow(m_index + 1);
				return { m_parent, index };
			}

			pointer operator->() const {
				return &(*m_parent)[m_parent->m_rowIds[reversed ? m_parent->size() - 1 - m_index : m_index]];
			}

			friend bool operator==(const iterator& l, const iterator& r) {
				return l.m_parent == r.m_parent && l.m_index == r.m_index;
			}

			friend bool operator!=(const iterator& l, const iterator& r) {
				return l.m_parent != r.m_parent || l.m_index != r.m_index;
			}

			reference operator*() const {
				return (*m_parent)[m_parent->m_rowIds[reversed ? m_parent->size() - 1 - m_index : m_index]];
			}

			iterator& operator--() { //prefix decrement
				m_index = CheckBoundaryOrThrow(m_index - 1);
				return *this;
			}

			iterator operator--(int) { //postfix decrement
				const auto index = m_index;
				m_index = CheckBoundaryOrThrow(m_index - 1);
				return { m_parent, index };
			}

		private:
			size_t CheckBoundaryOrThrow(size_t n) const {
				if (n > m_parent->size())
					throw std::out_of_range("Reached end of iterator.");
				if (n < 0)
					throw std::out_of_range("Reached beginning of iterator.");
				return n;
			}
		};

		using iterator = base_iterator<const ExdReader, const ExdRowBuffer, false>;
		using reverse_iterator = base_iterator<const ExdReader, const ExdRowBuffer, true>;

		iterator begin() const {
			return iterator(this, 0);
		}

		iterator cbegin() const {
			return iterator(this, 0);
		}

		iterator end() const {
			return iterator(this, m_rowIds.size());
		}

		iterator cend() const {
			return iterator(this, m_rowIds.size());
		}

		reverse_iterator rbegin() const {
			return reverse_iterator(this, 0);
		}

		reverse_iterator crbegin() const {
			return reverse_iterator(this, 0);
		}

		reverse_iterator rend() const {
			return reverse_iterator(this, m_rowIds.size());
		}

		reverse_iterator crend() const {
			return reverse_iterator(this, m_rowIds.size());
		}
	};

	class ExcelReader {
		const SqpackReader* m_sqpackReader;
		std::optional<ExhReader> m_exhReader;
		Language m_language;
		mutable std::vector<std::unique_ptr<ExdReader>> m_exdReaders;
		mutable std::mutex m_populateMtx;

	public:
		ExcelReader()
			: m_sqpackReader(nullptr)
			, m_language(Language::Unspecified) {}

		ExcelReader(const ExcelReader& r)
			: m_sqpackReader(r.m_sqpackReader)
			, m_exhReader(r.m_exhReader)
			, m_language(r.m_language)
			, m_exdReaders(m_exhReader ? m_exhReader->Pages().size() : 0) {}

		ExcelReader(ExcelReader&& r)
			: m_sqpackReader(r.m_sqpackReader)
			, m_exhReader(std::move(r.m_exhReader))
			, m_language(r.m_language)
			, m_exdReaders(std::move(r.m_exdReaders)) {
			r.Clear();
		}

		ExcelReader(const SqpackReader* sqpackReader, const std::string& name)
			: m_sqpackReader(sqpackReader)
			, m_exhReader(std::in_place, name, sqpackReader->GetPackedFileStream(std::format("exd/{}.exh", name))->GetUnpackedStream())
			, m_language(m_exhReader->Languages().front())
			, m_exdReaders(m_exhReader->Pages().size()) {}

		ExcelReader& operator=(const ExcelReader& r) {
			if (this == &r)
				return *this;

			m_sqpackReader = r.m_sqpackReader;
			m_exhReader = r.m_exhReader;
			m_language = r.m_language;
			m_exdReaders.clear();
			if (m_exhReader)
				m_exdReaders.resize(m_exhReader->Pages().size());
			return *this;
		}

		ExcelReader& operator=(ExcelReader&& r) {
			if (this == &r)
				return *this;

			m_sqpackReader = r.m_sqpackReader;
			m_exhReader = std::move(r.m_exhReader);
			m_language = r.m_language;
			m_exdReaders = std::move(r.m_exdReaders);
			r.Clear();
			return *this;
		}

		void Clear() {
			m_sqpackReader = nullptr;
			m_exhReader.reset();
			m_language = Language::Unspecified;
			m_exdReaders.clear();
		}

		const XivRes::Language& GetLanguage() const {
			return m_language;
		}

		ExcelReader NewReaderWithLanguage(Language language) {
			if (m_language == language)
				return *this;

			return ExcelReader(*this).WithLanguage(language);
		}

		ExcelReader& WithLanguage(Language language) {
			if (m_language == language)
				return *this;

			const auto lock = std::lock_guard(m_populateMtx);
			m_language = language;
			for (auto& v : m_exdReaders)
				v.reset();
			return *this;
		}

		const ExhReader& Exh() const {
			return *m_exhReader;
		}

		const ExdReader& Page(size_t index) const {
			if (!m_exdReaders[index]) {
				const auto lock = std::lock_guard(m_populateMtx);
				if (!m_exdReaders[index])
					m_exdReaders[index] = std::make_unique<ExdReader>(*m_exhReader, m_sqpackReader->GetPackedFileStream(m_exhReader->GetDataPathSpec(m_exhReader->Pages().at(index), m_language))->GetUnpackedStreamPtr());
			}

			return *m_exdReaders[index];
		}

		[[nodiscard]] const ExdRowBuffer& operator[](uint32_t rowId) const {
			return Page(m_exhReader->GetPageIndex(rowId))[rowId];
		}
	};
}

[[nodiscard]] XivRes::ExcelReader XivRes::GameReader::GetExcelReader(const std::string& name) const {
	return ExcelReader(&GetSqpackReader(0x0a0000), name);
}

#endif
