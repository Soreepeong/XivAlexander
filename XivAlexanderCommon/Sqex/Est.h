#pragma once
#include <cstdint>
#include <map>
#include <span>
#include <vector>
#include "XivAlexanderCommon/Sqex.h"

namespace Sqex::Est {
	struct EntryDescriptor {
		uint16_t SetId;
		uint16_t RaceCode;

		bool operator<(const EntryDescriptor& r) const {
			return RaceCode == r.RaceCode ? SetId < r.SetId : RaceCode < r.RaceCode;
		}

		bool operator<=(const EntryDescriptor& r) const {
			return RaceCode == r.RaceCode ? SetId <= r.SetId : RaceCode <= r.RaceCode;
		}

		bool operator>(const EntryDescriptor& r) const {
			return RaceCode == r.RaceCode ? SetId > r.SetId : RaceCode > r.RaceCode;
		}

		bool operator>=(const EntryDescriptor& r) const {
			return RaceCode == r.RaceCode ? SetId >= r.SetId : RaceCode >= r.RaceCode;
		}

		bool operator==(const EntryDescriptor& r) const {
			return SetId == r.SetId && RaceCode == r.RaceCode;
		}

		bool operator!=(const EntryDescriptor& r) const {
			return SetId != r.SetId || RaceCode != r.RaceCode;
		}
	};

	class File {
		std::vector<uint8_t> m_data;

	public:
		File() : m_data(4) {}
		File(std::vector<uint8_t> data) : m_data(std::move(data)) {}
		File(const RandomAccessStream& stream) : m_data(stream.ReadStreamIntoVector<uint8_t>(0)) {}
		File(const std::map<EntryDescriptor, uint16_t>& pairs)
			: m_data(4) {
			Update(pairs);
		}
		File(File&& file) : m_data(std::move(file.m_data)) {
			file.m_data.resize(4);
		}
		File(const File&& file) : m_data(file.m_data) {}
		File& operator=(File&& file) {
			m_data = std::move(file.m_data);
			file.m_data.resize(4);
			return *this;
		}
		File& operator=(const File&& file) {
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

		EntryDescriptor& Descriptor(size_t index) {
			return reinterpret_cast<EntryDescriptor*>(&m_data[4])[index];
		}

		const EntryDescriptor& Descriptor(size_t index) const {
			return reinterpret_cast<const EntryDescriptor*>(&m_data[4])[index];
		}

		std::span<EntryDescriptor> Descriptors() {
			return { reinterpret_cast<EntryDescriptor*>(&m_data[4]), Count() };
		}

		std::span<const EntryDescriptor> Descriptors() const {
			return { reinterpret_cast<const EntryDescriptor*>(&m_data[4]), Count() };
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

		std::map<EntryDescriptor, uint16_t> ToPairs() const {
			std::map<EntryDescriptor, uint16_t> res;
			for (size_t i = 0, i_ = Count(); i < i_; ++i)
				res.emplace(Descriptor(i), SkelId(i));
			return res;
		}

		void Update(const std::map<EntryDescriptor, uint16_t>& pairs) {
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
