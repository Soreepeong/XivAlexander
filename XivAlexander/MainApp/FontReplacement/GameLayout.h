#pragma once

#include <xivres/game_layout.h>

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// What the game's code says about the structures the font replacement uses (the plugin's GameLayout): every signature of
	// xivres's data/game_font_signatures.json found in the game's executable, and their captures merged by name. Loaded on
	// first use, which takes a few seconds: not on the game's thread.
	//
	// Each part resolves what it uses (Resolve), and fails alone if something of it isn't known. Structures that kept their
	// layout since 2020 stay C++ structures, checked against the captures (CheckFixed).
	class GameLayout {
	public:
		// Resolves what a part uses (fn calls Get and the like). Throws, with everything that isn't known, if anything isn't.
		static void Resolve(std::string_view part, const std::function<void()>& fn);

		// Gets a captured value, or an int constant; records a problem and returns -1 if it isn't known.
		static int32_t Get(const std::string& name);

		// Gets a captured value if any signature captured it (and they agree).
		static std::optional<int32_t> TryGet(const std::string& name);

		// Gets a string constant, or a constant list of ints; records a problem and returns an empty one if there is none.
		static std::string GetString(const std::string& name);
		static std::vector<int32_t> GetList(const std::string& name);

		// Gets what a signature resolves to, or with rel32, the target it captures; records a problem and returns 0 if the
		// signature wasn't found.
		static uintptr_t Address(const std::string& signature, const std::string& rel32 = {});

		// Gets the target of a rel32 that several signatures may capture, checking that they agree.
		static uintptr_t Target(const std::string& rel32);

		// Gets a signature's match, or nullptr (with a problem recorded) if it wasn't found.
		static const xivres::game_layout::match* Match(const std::string& signature);

		// Gets the target of a rel32 a match captured.
		static uintptr_t Target(const xivres::game_layout::match& match, const std::string& rel32);

		// Checks a C++ structure's size and field offsets against what was captured of them, under prefix (its size) and
		// prefix.Field; records a problem for each difference.
		static void CheckFixed(const std::string& prefix, size_t size, std::initializer_list<std::pair<const char*, size_t>> fields);

	private:
		static xivres::game_layout& Layout();
	};
}
