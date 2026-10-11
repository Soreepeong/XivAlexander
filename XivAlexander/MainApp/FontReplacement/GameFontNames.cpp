#include "pch.h"
#include "MainApp/FontReplacement/GameFontNames.h"

#include <FontChanger.FixedSizeFont/fontdata_fixed_size_font.h>

#include "MainApp/FontReplacement/FontStructs.h"
#include "MainApp/FontReplacement/GameLayout.h"
#include "MainApp/FontReplacement/GameUi.h"
#include "Utils/StringUtils.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;
namespace GameFontNames = FontReplacement::GameFontNames;

namespace {
	int s_entryCount;
	std::optional<std::vector<std::string>> s_gameNames;
	std::vector<std::string> s_lobbyNamesA;
	std::vector<std::string> s_lobbyNamesB;
	std::vector<std::string> s_families;

	// Reads a table's FDT names as face names: without the extension, nor the lobby suffix.
	std::vector<std::string> Read(uintptr_t table, int entrySize, int fdtNameOffset) {
		std::vector<std::string> names(s_entryCount);
		for (auto i = 0; i < s_entryCount; i++) {
			const auto p = *reinterpret_cast<const char* const*>(table + static_cast<size_t>(i) * entrySize + fdtNameOffset);
			std::string name = p ? p : "";
			if (Utils::EndsWithI(name, ".fdt"))
				name.resize(name.size() - 4);
			if (Utils::EndsWithI(name, "_lobby"))
				name.resize(name.size() - 6);
			names[i] = std::move(name);
		}
		return names;
	}

	// From FontChanger.FixedSizeFont's data/game_fonts.json; 0 if it isn't a game font.
	float SizeOf(const std::string& name) {
		const auto def = FontChanger::FixedSizeFont::find_fontdata_definition(name);
		return def ? def->Size : 0.f;
	}

	bool IsLobby(FontReplacement::GameFont* font) {
		const auto handle = font->GetTextureResourceHandle(0);
		if (!handle)
			return false;
		auto name = FontReplacement::GameUi::GetFileName(handle);
		std::ranges::transform(name, name.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
		return name.find("font_lobby") != std::string::npos;
	}

	// The lobby table whose sizes match the loaded lobby fonts.
	const std::vector<std::string>& LobbyNames(const FontReplacement::GameFontManager* manager, int count) {
		auto a = 0, b = 0;
		for (auto i = 0; i < count; i++) {
			const auto f = manager->Font(i);
			if (!IsLobby(f))
				continue;
			a += SizeOf(s_lobbyNamesA[i]) == f->Size() ? 1 : 0;
			b += SizeOf(s_lobbyNamesB[i]) == f->Size() ? 1 : 0;
		}
		return b > a ? s_lobbyNamesB : s_lobbyNamesA;
	}
}

std::string GameFontNames::FamilyOf(std::string_view faceName) {
	if (const auto def = FontChanger::FixedSizeFont::find_fontdata_definition(faceName))
		return def->Family;
	const auto underscore = faceName.rfind('_');
	return std::string(underscore == std::string_view::npos || underscore == 0 ? faceName : faceName.substr(0, underscore));
}

const std::vector<std::string>& GameFontNames::Families() {
	return s_families;
}

void GameFontNames::Initialize() {
	uintptr_t lobbyA = 0, lobbyB = 0, game = 0;
	int entrySize = 0, fdtName = 0;
	GameLayout::Resolve("Naming the game's fonts", [&] {
		s_entryCount = GameLayout::Get("FontTable.Count");

		// Before 6.30, entries are indexed as i * 3 * 8.
		entrySize = GameLayout::Get("FontTableEntry");
		fdtName = GameLayout::Get("FontTableEntry.FdtName");
		lobbyA = GameLayout::Address("FontTables", "LobbyTableA");
		lobbyB = GameLayout::Address("FontTables", "LobbyTableB");
		game = GameLayout::Address("FontTables", "GameTable");
		GameUi::ResolveResourceHandles();
	});

	s_lobbyNamesA = Read(lobbyA, entrySize, fdtName);
	s_lobbyNamesB = Read(lobbyB, entrySize, fdtName);
	s_gameNames = Read(game, entrySize, fdtName);
	s_families.clear();
	for (const auto& name : *s_gameNames) {
		auto family = FamilyOf(name);
		if (!family.empty() && std::ranges::none_of(s_families, [&](const auto& f) { return Utils::EqualsI(f, family); }))
			s_families.push_back(std::move(family));
	}
}

std::vector<std::pair<std::string, float>> GameFontNames::FacesOf(std::string_view family) {
	std::vector<std::pair<std::string, float>> res;
	if (!s_gameNames)
		return res;
	for (const auto& name : *s_gameNames) {
		if (!Utils::EqualsI(FamilyOf(name), family) || std::ranges::any_of(res, [&](const auto& f) { return Utils::EqualsI(f.first, name); }))
			continue;
		if (const auto size = SizeOf(name); size > 0)
			res.emplace_back(name, size);
	}
	return res;
}

std::optional<std::string> GameFontNames::GetFaceName(GameFont* font) {
	const auto manager = GameFontManager::Instance();
	if (!s_gameNames || !manager || !manager->Fonts())
		return std::nullopt;
	const auto index = manager->IndexOf(font);
	const auto count = (std::min)(static_cast<int>(manager->FontCount()), s_entryCount);
	if (index < 0 || index >= count)
		return std::nullopt;
	return IsLobby(font) ? LobbyNames(manager, count)[index] : (*s_gameNames)[index];
}

FontReplacement::GameFont* GameFontNames::FindFont(std::string_view faceName) {
	const auto manager = GameFontManager::Instance();
	if (!s_gameNames || !manager || !manager->Fonts())
		return nullptr;
	const auto count = (std::min)(static_cast<int>(manager->FontCount()), s_entryCount);
	const std::vector<std::string>* lobbyNames = nullptr;
	for (auto i = 0; i < count; i++) {
		const auto font = manager->Font(i);
		const auto lobby = IsLobby(font);
		if (lobby && !lobbyNames)
			lobbyNames = &LobbyNames(manager, count);
		if (Utils::EqualsI((lobby ? *lobbyNames : *s_gameNames)[i], faceName))
			return font;
	}
	return nullptr;
}
