#pragma once

#include "MainApp/FontReplacement/Fonts/FrameState.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// What the maps share, and what maps sharing a capacity sweep with.
	namespace AgedMaps {
		constexpr size_t DefaultCapacity = 8192;

		// The share of the capacity a sweep keeps.
		constexpr float KeptShare = 0.75f;

		// Entries last used before the returned frame go to bring used down to KeptShare of capacity. Never this frame's, as their glyphs may
		// still be handed out.
		[[nodiscard]] inline uint64_t Threshold(std::vector<uint64_t>& used, size_t capacity, uint64_t frame) {
			const auto kept = static_cast<size_t>(static_cast<float>(capacity) * KeptShare);
			if (used.size() <= kept)
				return 0;
			const auto excess = used.size() - kept;
			if (excess >= used.size())
				return frame;
			std::ranges::nth_element(used, used.begin() + static_cast<ptrdiff_t>(excess));
			return (std::min)(used[excess], frame);
		}

		// Above the capacity, and above what is left by a quarter of it, as what is left couldn't go.
		[[nodiscard]] inline size_t NextSweepAt(size_t count, size_t capacity) {
			return (std::max)(capacity, count + capacity / 4);
		}
	}

	// A cache that, once it holds more than its capacity, lets go of the entries used least recently.
	// Those are what the game is least likely to lay out again, such as a chat log's oldest lines or damage numbers gone from the screen.
	// A sweep goes down to AgedMaps::KeptShare of the capacity, so that it scans the entries only now and then.
	template<typename TKey, typename TValue>
	class AgedMap {
		struct Entry {
			TValue Value;

			// The frame it was last used in.
			uint64_t Used;
		};

		const FrameState* m_state;

		// Entries kept however long ago they were used.
		size_t m_capacity;

		// Where the next sweep starts, so that entries that can't go aren't scanned every frame.
		size_t m_sweepAt;

		std::map<TKey, Entry> m_entries;

	public:
		explicit AgedMap(const FrameState& state, size_t capacity = AgedMaps::DefaultCapacity)
			: m_state(&state)
			, m_capacity(capacity)
			, m_sweepAt(capacity) {}

		AgedMap(AgedMap&&) = default;
		AgedMap& operator=(AgedMap&&) = default;
		AgedMap(const AgedMap&) = delete;
		AgedMap& operator=(const AgedMap&) = delete;

		[[nodiscard]] size_t Size() const { return m_entries.size(); }

		// Marks the entry used in this frame. nullptr if there is none.
		TValue* Find(const TKey& key) {
			const auto it = m_entries.find(key);
			if (it == m_entries.end())
				return nullptr;
			it->second.Used = m_state->Frame();
			return &it->second.Value;
		}

		// The key must not be in the map yet.
		TValue& Add(const TKey& key, TValue value) {
			return m_entries.emplace(key, Entry{std::move(value), m_state->Frame()}).first->second.Value;
		}

		// Calls drop for the value if the key was in the map.
		template<typename TDrop>
		void Remove(const TKey& key, TDrop&& drop) {
			const auto it = m_entries.find(key);
			if (it == m_entries.end())
				return;
			auto value = std::move(it->second.Value);
			m_entries.erase(it);
			drop(value);
		}

		// Calls drop for each value, then empties the map.
		template<typename TDrop>
		void Clear(TDrop&& drop) {
			for (auto& e : m_entries | std::views::values)
				drop(e.Value);
			m_entries.clear();
			m_sweepAt = m_capacity;
		}

		// Entries used least recently go, if canDrop holds for them, calling drop for each.
		template<typename TCanDrop, typename TDrop>
		void Sweep(TCanDrop&& canDrop, TDrop&& drop) {
			if (m_entries.size() <= m_sweepAt)
				return;

			std::vector<uint64_t> used;
			used.reserve(m_entries.size());
			CollectUsed(used);
			DropUsedBefore(AgedMaps::Threshold(used, m_capacity, m_state->Frame()), canDrop, drop);
			m_sweepAt = AgedMaps::NextSweepAt(m_entries.size(), m_capacity);
		}

		template<typename TDrop>
		void Sweep(TDrop&& drop) {
			Sweep([](const TValue&) { return true; }, std::forward<TDrop>(drop));
		}

		// Adds when each entry was last used to used, for maps sharing a capacity.
		void CollectUsed(std::vector<uint64_t>& used) const {
			for (const auto& e : m_entries | std::views::values)
				used.push_back(e.Used);
		}

		// Entries last used before frame go, if canDrop holds for them, calling drop for each.
		template<typename TCanDrop, typename TDrop>
		void DropUsedBefore(uint64_t frame, TCanDrop&& canDrop, TDrop&& drop) {
			for (auto it = m_entries.begin(); it != m_entries.end();) {
				if (it->second.Used >= frame || !canDrop(it->second.Value)) {
					++it;
					continue;
				}
				drop(it->second.Value);
				it = m_entries.erase(it);
			}
		}
	};
}
