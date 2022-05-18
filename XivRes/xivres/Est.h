#ifndef _XIVRES_EST_H_
#define _XIVRES_EST_H_

#include <cstdint>
#include <map>
#include <span>
#include <vector>

#include "IStream.h"

namespace XivRes {
	struct EstEntryDescriptor {
		uint16_t SetId;
		uint16_t RaceCode;

		bool operator<(const EstEntryDescriptor& r) const {
			return RaceCode == r.RaceCode ? SetId < r.SetId : RaceCode < r.RaceCode;
		}

		bool operator<=(const EstEntryDescriptor& r) const {
			return RaceCode == r.RaceCode ? SetId <= r.SetId : RaceCode <= r.RaceCode;
		}

		bool operator>(const EstEntryDescriptor& r) const {
			return RaceCode == r.RaceCode ? SetId > r.SetId : RaceCode > r.RaceCode;
		}

		bool operator>=(const EstEntryDescriptor& r) const {
			return RaceCode == r.RaceCode ? SetId >= r.SetId : RaceCode >= r.RaceCode;
		}

		bool operator==(const EstEntryDescriptor& r) const {
			return SetId == r.SetId && RaceCode == r.RaceCode;
		}

		bool operator!=(const EstEntryDescriptor& r) const {
			return SetId != r.SetId || RaceCode != r.RaceCode;
		}
	};

	class EstFile {
		std::vector<uint8_t> m_data;

	public:
		EstFile()
			: m_data(4) {
		}

		EstFile(std::vector<uint8_t> data)
			: m_data(std::move(data)) {
		}

		EstFile(const IStream& stream)
			: m_data(ReadStreamIntoVector<uint8_t>(stream, 0)) {
		}

		EstFile(const std::map<EstEntryDescriptor, uint16_t>& pairs)
			: m_data(4) {
			Update(pairs);
		}

		EstFile(EstFile&& file)
			: m_data(std::move(file.m_data)) {
			file.m_data.resize(4);
		}

		EstFile(const EstFile& file)
			: m_data(file.m_data) {
		}

		EstFile& operator=(EstFile&& file) {
			m_data = std::move(file.m_data);
			file.m_data.resize(4);
			return *this;
		}

		EstFile& operator=(const EstFile& file) {
			m_data = file.m_data;
			return *this;
		}

		const std::vector<uint8_t>& Data() const {
			return m_data;
		}

		uint32_t& Count() {
			return *reinterpret_cast<uint32_t*>(&m_data[0]);
		}

		const uint32_t& Count() const {
			return *reinterpret_cast<const uint32_t*>(&m_data[0]);
		}

		EstEntryDescriptor& Descriptor(size_t index) {
			return reinterpret_cast<EstEntryDescriptor*>(&m_data[4])[index];
		}

		const EstEntryDescriptor& Descriptor(size_t index) const {
			return reinterpret_cast<const EstEntryDescriptor*>(&m_data[4])[index];
		}

		std::span<EstEntryDescriptor> Descriptors() {
			return { reinterpret_cast<EstEntryDescriptor*>(&m_data[4]), Count() };
		}

		std::span<const EstEntryDescriptor> Descriptors() const {
			return { reinterpret_cast<const EstEntryDescriptor*>(&m_data[4]), Count() };
		}

		uint16_t& SkelId(size_t index) {
			return reinterpret_cast<uint16_t*>(&m_data[4 + Descriptors().size_bytes()])[index];
		}

		const uint16_t& SkelId(size_t index) const {
			return reinterpret_cast<const uint16_t*>(&m_data[4 + Descriptors().size_bytes()])[index];
		}

		std::span<uint16_t> SkelIds() {
			return { reinterpret_cast<uint16_t*>(&m_data[4 + Descriptors().size_bytes()]), Count() };
		}

		std::span<const uint16_t> SkelIds() const {
			return { reinterpret_cast<const uint16_t*>(&m_data[4 + Descriptors().size_bytes()]), Count() };
		}

		std::map<EstEntryDescriptor, uint16_t> ToPairs() const {
			std::map<EstEntryDescriptor, uint16_t> res;
			for (size_t i = 0, i_ = Count(); i < i_; ++i)
				res.emplace(Descriptor(i), SkelId(i));
			return res;
		}

		void Update(const std::map<EstEntryDescriptor, uint16_t>& pairs) {
			m_data.resize(4 + pairs.size() * 6);
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

#endif
