#ifndef _XIVRES_IMC_H_
#define _XIVRES_IMC_H_

#include <cstdint>
#include <span>
#include <vector>

#include "Internal/ByteOrder.h"

#include "Common.h"
#include "IStream.h"

namespace XivRes::Imc {
	enum class Type : uint16_t {
		Unknown = 0,
		NonSet = 1,
		Set = 31
	};

	struct Header {
		LE<uint16_t> SubsetCount;
		LE<Type> Type;
	};

	struct Entry {
		uint8_t Variant;
		uint8_t Unknown_0x001;
		LE<uint16_t> Mask;
		uint8_t Vfx;
		uint8_t Animation;
	};

	class File {
		std::vector<uint8_t> m_data;

	public:
		File()
			: m_data(sizeof Imc::Header) {
		}

		File(const IStream& stream)
			: m_data(ReadStreamIntoVector<uint8_t>(stream, 0)) {
			if (m_data.size() < sizeof Imc::Header) {
				m_data.clear();
				m_data.resize(sizeof Imc::Header);
			}
		}

		File(File&& file)
			: m_data(std::move(file.m_data)) {
			file.m_data.resize(sizeof Imc::Header);
		}

		File(const File& file)
			: m_data(file.m_data) {
		}

		File& operator=(File&& file) {
			m_data = std::move(file.m_data);
			file.m_data.resize(sizeof Imc::Header);
			return *this;
		}

		File& operator=(const File& file) {
			m_data = file.m_data;
			return *this;
		}

		const std::vector<uint8_t>& Data() const {
			return m_data;
		}

		size_t EntryCountPerSet() const {
			size_t entryCountPerSet = 0;
			for (size_t i = 0; i < 16; i++) {
				if (static_cast<uint16_t>(*Header().Type) & (1 << i))
					entryCountPerSet++;
			}
			return entryCountPerSet;
		}

		Imc::Header& Header() {
			return *reinterpret_cast<Imc::Header*>(m_data.data());
		}

		const Imc::Header& Header() const {
			return *reinterpret_cast<const Imc::Header*>(m_data.data());
		}

		Imc::Entry& Entry(size_t index) {
			return reinterpret_cast<Imc::Entry*>(m_data.data() + sizeof Imc::Header)[index];
		}

		const Imc::Entry& Entry(size_t index) const {
			return reinterpret_cast<const Imc::Entry*>(m_data.data() + sizeof Imc::Header)[index];
		}

		std::span<Imc::Entry> Entries() {
			return { reinterpret_cast<Imc::Entry*>(m_data.data() + sizeof Imc::Header), (m_data.size() - sizeof Imc::Header) / sizeof Imc::Entry };
		}

		std::span<const Imc::Entry> Entries() const {
			return { reinterpret_cast<const Imc::Entry*>(m_data.data() + sizeof Imc::Header), (m_data.size() - sizeof Imc::Header) / sizeof Imc::Entry };
		}

		void Resize(size_t subsetCount) {
			if (subsetCount > UINT16_MAX)
				throw std::range_error("up to 64k subsets are supported");
			const auto countPerSet = EntryCountPerSet();
			if (subsetCount > Header().SubsetCount && Header().SubsetCount > 0) {
				m_data.resize(sizeof Imc::Header + sizeof Imc::Entry * countPerSet * (size_t{ 1 } + Header().SubsetCount));
				m_data.reserve(sizeof Imc::Header + sizeof Imc::Entry * countPerSet * (1 + subsetCount));
				for (auto i = *Header().SubsetCount; i < subsetCount; ++i) {
					m_data.insert(m_data.end(), &m_data[sizeof Imc::Header], &m_data[sizeof Imc::Header + sizeof Imc::Entry * countPerSet]);
				}
			} else {
				m_data.resize(sizeof Imc::Header + sizeof Imc::Entry * countPerSet * (1 + subsetCount));
			}
			Header().SubsetCount = static_cast<uint16_t>(subsetCount);
		}

		void Ensure(size_t minSubsetCount) {
			if (minSubsetCount > Header().SubsetCount)
				Resize(minSubsetCount);
		}
	};
}

#endif
