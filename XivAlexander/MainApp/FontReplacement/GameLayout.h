#pragma once

#include <xivres/game_layout.h>

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Signatures of xivres's data/game_font_signatures.json found in the game's executable, captures merged by name; first use takes seconds: not on the game's thread.
	// Each part resolves what it uses (Resolve) and fails alone; structures unchanged since 2020 stay C++ structs, checked against the captures (CheckFixed).
	class GameLayout {
	public:
		// fn calls Get and the like; throws listing everything that isn't known, if anything isn't.
		static void Resolve(std::string_view part, const std::function<void()>& fn);

		// A captured value or an int constant; records a problem and returns -1 if it isn't known.
		static int32_t Get(const std::string& name);

		// Only if any signature captured it (and they agree).
		static std::optional<int32_t> TryGet(const std::string& name);

		// Records a problem and returns an empty one if there is none.
		static std::string GetString(const std::string& name);
		static std::vector<int32_t> GetList(const std::string& name);

		// With rel32, the target the signature captures; records a problem and returns 0 if the signature wasn't found.
		static uintptr_t Address(const std::string& signature, const std::string& rel32 = {});

		// Several signatures may capture the rel32; checks that they agree.
		static uintptr_t Target(const std::string& rel32);

		// nullptr (with a problem recorded) if it wasn't found.
		static const xivres::game_layout::match* Match(const std::string& signature);

		static uintptr_t Target(const xivres::game_layout::match& match, const std::string& rel32);

		// Checks size and field offsets against the captures named prefix (the size) and prefix.Field; records a problem for each difference.
		static void CheckFixed(const std::string& prefix, size_t size, std::initializer_list<std::pair<const char*, size_t>> fields);

	private:
		static xivres::game_layout& Layout();
	};
}
