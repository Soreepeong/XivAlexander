#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include "XivAlexanderCommon/Sqex.h"

namespace Sqex::Eqdp {
	struct Header {
		LE<uint16_t> Identifier;
		LE<uint16_t> BlockMemberCount;
		LE<uint16_t> BlockCount;
	};

	class File;
	class ExpandedFile;
	std::vector<uint8_t> ExpandCollapse(const File* file, bool expand);

	class File {
	protected:
		std::vector<uint8_t> m_data;

	public:
		File() : m_data(sizeof Eqdp::Header) {}
		File(std::vector<uint8_t> data) : m_data(std::move(data)) {}
		File(const RandomAccessStream& stream) : m_data(stream.ReadStreamIntoVector<uint8_t>(0)) {}
		File(File&& file) : m_data(std::move(file.m_data)) { file.m_data.resize(sizeof Eqdp::Header); }
		File(const File& file) : m_data(file.m_data) {}
		File(ExpandedFile& data);
		File& operator=(File&& file) {
			m_data = std::move(file.m_data);
			file.m_data.resize(sizeof Eqdp::Header);
			return *this;
		}
		File& operator=(const File& file) {
			m_data = file.m_data;
		}

		const std::vector<uint8_t>& Data() const {
			return m_data;
		}

		Eqdp::Header& Header() {
			return *reinterpret_cast<Eqdp::Header*>(m_data.data());
		}

		const Eqdp::Header& Header() const {
			return *reinterpret_cast<const Eqdp::Header*>(m_data.data());
		}

		std::span<uint16_t> Indices() {
			return { reinterpret_cast<uint16_t*>(m_data.data() + sizeof Eqdp::Header), Header().BlockCount };
		}

		std::span<const uint16_t> Indices() const {
			return { reinterpret_cast<const uint16_t*>(m_data.data() + sizeof Eqdp::Header), Header().BlockCount };
		}

		size_t BaseOffset() const {
			return sizeof Eqdp::Header + Indices().size_bytes();
		}

		std::span<uint16_t> Body() {
			return { reinterpret_cast<uint16_t*>(m_data.data() + BaseOffset()), static_cast<size_t>(Header().BlockCount * Header().BlockMemberCount) };
		}

		std::span<const uint16_t> Body() const {
			return { reinterpret_cast<const uint16_t*>(m_data.data() + BaseOffset()), static_cast<size_t>(Header().BlockCount * Header().BlockMemberCount) };
		}

		std::span<uint16_t> Block(size_t blockId) {
			const auto index = Indices()[blockId];
			if (index == UINT16_MAX)
				return {};

			return Body().subspan(index, Header().BlockMemberCount);
		}

		std::span<const uint16_t> Block(size_t blockId) const {
			const auto index = Indices()[blockId];
			if (index == UINT16_MAX)
				return {};

			return Body().subspan(index, Header().BlockMemberCount);
		}
	};

	class ExpandedFile : public File {
	public:
		ExpandedFile() : File() {}
		ExpandedFile(ExpandedFile&& file) : File(std::move(file.m_data)) { m_data.resize(sizeof Eqdp::Header); }
		ExpandedFile(const ExpandedFile& file) : File(file.m_data) {}
		ExpandedFile(const File& data) : File(ExpandCollapse(&data, true)) {}
		ExpandedFile& operator=(ExpandedFile&& file) {
			m_data = std::move(file.m_data);
			file.m_data.resize(sizeof Eqdp::Header);
			return *this;
		}
		ExpandedFile& operator=(const ExpandedFile& file) {
			m_data = file.m_data;
		}

		uint16_t& Set(size_t setId) {
			return Block(setId / Header().BlockMemberCount)[setId % Header().BlockMemberCount];
		}

		const uint16_t& Set(size_t setId) const {
			return Block(setId / Header().BlockMemberCount)[setId % Header().BlockMemberCount];
		}
	};

	inline File::File(ExpandedFile& data) : m_data(ExpandCollapse(dynamic_cast<File*>(&data), false)) {}
}