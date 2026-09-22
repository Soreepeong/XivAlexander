#include "pch.h"
#include "MainApp/Modding/FutureReservations.h"

#include "Misc/Logger.h"

namespace XivAlexander::Apps::MainApp::Features::Modding {
	namespace {
		constexpr std::pair<uint32_t, size_t> ReservationSizes[]{
			{16 * 1048576, 2048},
			{128 * 1048576, 256},
			{0x7FFFFFFF, 8},
		};
	}

	std::string FutureReservations::PackKey(const xivres::path_spec& pathSpec) {
		return std::format("{}/{}", pathSpec.exname(), pathSpec.packname());
	}

	std::string FutureReservations::PackKey(const xivres::sqpack::generator& generator) {
		return std::format("{}/{}", generator.DatExpac, generator.DatName);
	}

	FutureReservations::FutureReservations()
		: m_logger(Misc::Logger::Acquire()) {}

	FutureReservations::~FutureReservations() = default;

	void FutureReservations::Reserve(xivres::sqpack::generator& generator) {
		Pool pool;
		const auto spec = xivres::sqpack_spec::from_filename_int(std::strtoul(generator.DatName.c_str(), nullptr, 16));
		const auto requiredPrefix = spec.required_prefix();

		size_t counter = 0;
		for (const auto& [space, count] : ReservationSizes) {
			auto& slots = pool[space];

			Slot stream;
			for (size_t i = 0; i < count; i++, counter++) {
				const xivres::path_spec pathSpec(std::format("{}x/{:x}.xivalex", requiredPrefix, counter));
				if (!stream)
					stream = std::make_shared<xivres::hotswap_packed_stream>(pathSpec, space);

				if (generator.add(stream, false).Added.empty()) {
					i--;
				} else {
					slots.emplace_back(std::move(stream));
					stream = nullptr;
				}
			}
		}

		const auto lock = std::lock_guard(m_mtx);
		m_pools.emplace(PackKey(generator), std::move(pool));
	}

	std::string FutureReservations::Install(const xivres::path_spec& pathSpec, const std::shared_ptr<const xivres::packed_stream>& stream) const {
		const auto poolIt = m_pools.find(PackKey(pathSpec));
		if (poolIt == m_pools.end())
			return {};

		const auto wanted = static_cast<uint64_t>(stream->size());

		if (const auto held = m_held.find(pathSpec); held != m_held.end()) {
			auto& [space, slot] = held->second;
			if (space >= wanted) {
				// still fits in the reserved space
				slot->swap_stream(stream);
				return slot->path_spec().text();
			}

			// use a future reservation
			slot->swap_stream(nullptr);
			poolIt->second[space].emplace_back(std::move(slot));
			m_held.erase(held);
		}

		auto [space, slot] = Take(poolIt->second, wanted);
		if (!slot)
			return {};

		slot->swap_stream(stream);
		auto path = slot->path_spec().text();
		m_held.insert_or_assign(pathSpec, std::make_pair(space, std::move(slot)));
		return path;
	}

	std::pair<uint32_t, FutureReservations::Slot> FutureReservations::Take(Pool& pool, uint64_t size) {
		for (auto& [space, slots] : pool) {
			if (space < size || slots.empty())
				continue;

			auto slot = std::move(slots.back());
			slots.pop_back();
			return {space, std::move(slot)};
		}
		return {0, nullptr};
	}

	std::string FutureReservations::Lend(const xivres::path_spec& requested, const xivres::path_spec& target, const std::shared_ptr<const xivres::packed_stream>& stream) const {
		const auto lock = std::lock_guard(m_mtx);

		const auto packKey = PackKey(requested);
		const auto key = std::format("{}:{:08x}/{:08x}/{:08x}", packKey, target.path_hash(), target.name_hash(), target.full_path_hash());
		if (const auto it = m_lent.find(key); it != m_lent.end())
			return it->second.second->path_spec().text();

		const auto poolIt = m_pools.find(packKey);
		if (poolIt == m_pools.end())
			return {};

		auto [space, slot] = Take(poolIt->second, static_cast<uint64_t>(stream->size()));
		if (!slot) {
			m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
				"{}: needs {} bytes in {} and no stand-in is large enough or free", target, stream->size(), packKey);
			return {};
		}

		slot->swap_stream(stream);
		auto path = slot->path_spec().text();
		m_logger->Format(LogCategory::VirtualSqPacks, "{} lent to {} as {}", target, packKey, path);
		m_lent.emplace(key, std::make_pair(space, std::move(slot)));
		return path;
	}

	void FutureReservations::Release(const xivres::path_spec& pathSpec) const {
		const auto held = m_held.find(pathSpec);
		if (held == m_held.end())
			return;

		auto& [space, slot] = held->second;
		slot->swap_stream(nullptr);
		if (const auto poolIt = m_pools.find(PackKey(pathSpec)); poolIt != m_pools.end())
			poolIt->second[space].emplace_back(std::move(slot));
		m_held.erase(held);
	}

	void FutureReservations::SetUnplaced(const std::set<std::string>& packKeys, Streams unplaced) {
		const auto lock = std::lock_guard(m_mtx);

		std::vector<xivres::path_spec> stale;
		for (const auto& pathSpec : m_held | std::views::keys) {
			if (packKeys.contains(PackKey(pathSpec)) && !unplaced.contains(pathSpec))
				stale.emplace_back(pathSpec);
		}
		for (const auto& pathSpec : stale)
			Release(pathSpec);

		for (const auto& [pathSpec, stream] : unplaced) {
			if (m_held.contains(pathSpec))
				Install(pathSpec, stream);
		}

		if (!unplaced.empty()) {
			m_logger->Format(LogCategory::VirtualSqPacks,
				"{} replacement(s) do not fit where they belong and wait on a stand-in; first is {} at {} bytes",
				unplaced.size(), unplaced.begin()->first, unplaced.begin()->second->size());
		}

		std::erase_if(m_pending, [&packKeys](const auto& item) { return packKeys.contains(PackKey(item.first)); });
		m_pending.merge(unplaced);
	}

	std::string FutureReservations::Find(const xivres::path_spec& pathSpec) const {
		const auto lock = std::lock_guard(m_mtx);

		if (const auto it = m_held.find(pathSpec); it != m_held.end())
			return it->second.second->path_spec().text();

		const auto pending = m_pending.find(pathSpec);
		if (pending == m_pending.end())
			return {};

		auto path = Install(pathSpec, pending->second);
		if (path.empty()) {
			m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
				"{}: needs {} bytes and no stand-in is large enough or free", pathSpec, pending->second->size());
			m_pending.erase(pending);
			return {};
		}

		m_logger->Format(LogCategory::VirtualSqPacks, "{} => {}", pathSpec, path);
		return path;
	}
}
