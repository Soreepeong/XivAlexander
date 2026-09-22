#pragma once

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include <xivres/packed_stream.hotswap.h>
#include <xivres/sqpack.generator.h>

namespace XivAlexander::Misc {
	class Logger;
}

namespace XivAlexander::Apps::MainApp::Features::Modding {
	class FutureReservations {
	public:
		using Streams = std::map<xivres::path_spec, std::shared_ptr<const xivres::packed_stream>, xivres::path_spec::AllHashComparator>;

		FutureReservations();
		~FutureReservations();

		/// Adds a pack's stand-ins to \p generator, and keeps them to lend out.
		void Reserve(xivres::sqpack::generator& generator);

		/// Makes \p unplaced the replacements waiting on a stand-in in the packs named by \p packKeys, returning the
		/// ones held there for anything no longer among them. Other packs are left as they are.
		void SetUnplaced(const std::set<std::string>& packKeys, Streams unplaced);

		/// \returns The path of the stand-in answering for \p pathSpec, lending one if it is waiting; empty if none.
		std::string Find(const xivres::path_spec& pathSpec) const;

		/// Lends a stand-in in the pack of \p requested to carry \p stream, which is what \p target has, as the game
		/// looks a path up only in the pack of the one it asked for. Everything sent to the same target from the same
		/// pack shares the stand-in.
		/// \returns The path of the stand-in; empty if the pack has none free that is large enough.
		std::string Lend(const xivres::path_spec& requested, const xivres::path_spec& target, const std::shared_ptr<const xivres::packed_stream>& stream) const;

		/// \returns How a pack is told apart here: its expansion and name, like "ffxiv/040000".
		static std::string PackKey(const xivres::path_spec& pathSpec);
		static std::string PackKey(const xivres::sqpack::generator& generator);

	private:
		using Slot = std::shared_ptr<xivres::hotswap_packed_stream>;
		using Pool = std::map<uint32_t, std::deque<Slot>>;

		std::string Install(const xivres::path_spec& pathSpec, const std::shared_ptr<const xivres::packed_stream>& stream) const;
		void Release(const xivres::path_spec& pathSpec) const;

		/// Takes the smallest free stand-in from \p pool that has room for \p size bytes, if any.
		static std::pair<uint32_t, Slot> Take(Pool& pool, uint64_t size);

		const std::shared_ptr<Misc::Logger> m_logger;

		mutable std::mutex m_mtx;
		mutable std::map<std::string, Pool> m_pools;
		mutable std::map<xivres::path_spec, std::pair<uint32_t, Slot>, xivres::path_spec::AllHashComparator> m_held;
		mutable Streams m_pending;

		/// Stand-ins lent through Lend, by the pack lending them and the target they carry.
		mutable std::map<std::string, std::pair<uint32_t, Slot>> m_lent;
	};
}
