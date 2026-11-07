#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// A cache whose entries go once unused for a number of frames. Each Sweep looks at a bounded number of them.
	template<typename TKey, typename TValue>
	class AgedMap {
	public:
		// Frames an entry is kept after its last use, once used in a frame after the one it was added in. One used only in that frame is kept
		// NewFrames, so that text that changes every frame doesn't fill the cache.
		static constexpr uint64_t KeepFrames = 600;
		static constexpr uint64_t NewFrames = 8;

		// Looked at per Sweep, besides twice the entries added since the one before.
		static constexpr size_t SweepEntries = 32;

		struct Entry {
			TValue Value;
			uint64_t Added;
			uint64_t Used;
		};

	private:
		std::map<TKey, Entry> m_entries;
		std::optional<TKey> m_next;
		size_t m_added = 0;

	public:
		[[nodiscard]] const std::map<TKey, Entry>& Entries() const { return m_entries; }

		// Marks the entry used in frame. nullptr if there is none.
		TValue* Find(const TKey& key, uint64_t frame) {
			const auto it = m_entries.find(key);
			if (it == m_entries.end())
				return nullptr;
			it->second.Used = frame;
			return &it->second.Value;
		}

		// The key must not be in the map yet.
		TValue& Add(const TKey& key, TValue value, uint64_t frame) {
			m_added++;
			return m_entries.emplace(key, Entry{std::move(value), frame, frame}).first->second.Value;
		}

		// drop is called for each value before it goes, and must not change the map.
		template<typename TDrop>
		void Clear(TDrop&& drop) {
			for (auto& entry : m_entries | std::views::values)
				drop(entry.Value);
			m_entries.clear();
			m_next.reset();
			m_added = 0;
		}

		// Entries unused long enough go if canDrop holds for them. drop is as for Clear.
		template<typename TCanDrop, typename TDrop>
		void Sweep(uint64_t frame, TCanDrop&& canDrop, TDrop&& drop) {
			auto budget = (std::min)(SweepEntries + 2 * std::exchange(m_added, 0), m_entries.size());
			auto it = m_next ? m_entries.lower_bound(*m_next) : m_entries.begin();
			for (; budget > 0 && !m_entries.empty(); budget--) {
				if (it == m_entries.end())
					it = m_entries.begin();
				auto& e = it->second;
				if (frame - e.Used > (e.Used > e.Added ? KeepFrames : NewFrames) && canDrop(e.Value)) {
					drop(e.Value);
					it = m_entries.erase(it);
				} else {
					++it;
				}
			}

			if (it == m_entries.end())
				m_next.reset();
			else
				m_next = it->first;
		}

		template<typename TDrop>
		void Sweep(uint64_t frame, TDrop&& drop) {
			Sweep(frame, [](const TValue&) { return true; }, std::forward<TDrop>(drop));
		}
	};
}
