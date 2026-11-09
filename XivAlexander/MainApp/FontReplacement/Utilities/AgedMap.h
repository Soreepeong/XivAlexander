#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// A cache that, while it holds more than its capacity, lets go of the entries used least recently. Those are what the game is least likely
	// to lay out again, such as a chat log's oldest lines or damage numbers gone from the screen. Each Sweep lets go of a bounded number.
	template<typename TKey, typename TValue>
	class AgedMap {
	public:
		static constexpr size_t DefaultCapacity = 8192;

		// Let go of per Sweep at most, besides twice the entries added since the one before.
		static constexpr size_t SweepEntries = 32;

	private:
		struct Entry {
			TKey Key;
			TValue Value;

			// The frame it was last used in.
			uint64_t Used;
		};

		// Entries kept however long ago they were used.
		size_t m_capacity = DefaultCapacity;

		// Most recently used first.
		std::list<Entry> m_order;
		std::map<TKey, typename std::list<Entry>::iterator> m_entries;
		size_t m_added = 0;

	public:
		AgedMap() = default;

		explicit AgedMap(size_t capacity)
			: m_capacity(capacity) {}

		AgedMap(AgedMap&&) = default;
		AgedMap& operator=(AgedMap&&) = default;
		AgedMap(const AgedMap&) = delete;
		AgedMap& operator=(const AgedMap&) = delete;

		[[nodiscard]] size_t Size() const { return m_entries.size(); }

		[[nodiscard]] auto Keys() const { return m_order | std::views::transform([](const Entry& e) -> const TKey& { return e.Key; }); }

		// Marks the entry used in frame. nullptr if there is none.
		TValue* Find(const TKey& key, uint64_t frame) {
			const auto it = m_entries.find(key);
			if (it == m_entries.end())
				return nullptr;
			m_order.splice(m_order.begin(), m_order, it->second);
			it->second->Used = frame;
			return &it->second->Value;
		}

		// The key must not be in the map yet.
		TValue& Add(const TKey& key, TValue value, uint64_t frame) {
			m_order.push_front(Entry{key, std::move(value), frame});
			m_entries.emplace(key, m_order.begin());
			m_added++;
			return m_order.front().Value;
		}

		// drop is called for each value before it goes, and must not change the map.
		template<typename TDrop>
		void Clear(TDrop&& drop) {
			for (auto& entry : m_order)
				drop(entry.Value);
			m_entries.clear();
			m_order.clear();
			m_added = 0;
		}

		// Entries used least recently, before frame, go if canDrop holds for them. One that can't is treated as used. drop is as for Clear.
		template<typename TCanDrop, typename TDrop>
		void Sweep(uint64_t frame, TCanDrop&& canDrop, TDrop&& drop) {
			auto budget = SweepEntries + 2 * std::exchange(m_added, 0);
			for (; budget > 0 && m_entries.size() > m_capacity; budget--) {
				auto& e = m_order.back();
				if (e.Used == frame)
					break;
				if (canDrop(e.Value)) {
					drop(e.Value);
					m_entries.erase(e.Key);
					m_order.pop_back();
				} else {
					m_order.splice(m_order.begin(), m_order, std::prev(m_order.end()));
				}
			}
		}

		template<typename TDrop>
		void Sweep(uint64_t frame, TDrop&& drop) {
			Sweep(frame, [](const TValue&) { return true; }, std::forward<TDrop>(drop));
		}
	};
}
