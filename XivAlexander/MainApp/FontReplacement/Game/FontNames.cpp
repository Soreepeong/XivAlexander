#include "pch.h"
#include "MainApp/FontReplacement/Game/FontNames.h"

#include <FontChanger.FixedSizeFont/fontdata_fixed_size_font.h>

#include "MainApp/FontReplacement/FontStructs.h"
#include "MainApp/FontReplacement/GameCode.h"
#include "MainApp/FontReplacement/GameUi.h"
#include "MainApp/FontReplacement/Presets.h"
#include "Utils/StringUtils.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;
namespace FontNames = FontReplacement::FontNames;

namespace {
	int s_entryCount;
	std::optional<std::vector<std::string>> s_gameNames;
	std::vector<std::string> s_lobbyNamesA;
	std::vector<std::string> s_lobbyNamesB;
	std::vector<std::string> s_families;

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

	float SizeFromFdtFileName(const std::string& name) {
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

	const std::vector<std::string>& LobbyNames(const FontReplacement::GameFontManager* manager, int count) {
		auto a = 0, b = 0;
		for (auto i = 0; i < count; i++) {
			const auto f = manager->Font(i);
			if (!IsLobby(f))
				continue;
			a += SizeFromFdtFileName(s_lobbyNamesA[i]) == f->Size() ? 1 : 0;
			b += SizeFromFdtFileName(s_lobbyNamesB[i]) == f->Size() ? 1 : 0;
		}
		return b > a ? s_lobbyNamesB : s_lobbyNamesA;
	}

	FontReplacement::GameFontManager* TryGetManager(int& count) {
		const auto manager = FontReplacement::GameFontManager::Instance();
		if (!s_gameNames || !manager || !manager->Fonts())
			return nullptr;
		count = (std::min)(static_cast<int>(manager->FontCount()), s_entryCount);
		return manager;
	}

	const std::string& NameAt(const FontReplacement::GameFontManager* manager, int index, int count, const std::vector<std::string>*& lobbyNames) {
		if (!IsLobby(manager->Font(index)))
			return (*s_gameNames)[index];
		if (!lobbyNames)
			lobbyNames = &LobbyNames(manager, count);
		return (*lobbyNames)[index];
	}
}

const std::vector<std::string>& FontNames::Families() {
	return s_families;
}

void FontNames::Initialize() {
	const auto tables = ResolveGameCode(Game::Resolved::Fonts::FontTables);
	GameUi::ResolveResourceHandles();
	s_entryCount = tables.Count;
	const auto read = [&](const void* table) { return Read(reinterpret_cast<uintptr_t>(table), tables.EntrySize, tables.FdtName); };
	s_lobbyNamesA = read(tables.LobbyA);
	s_lobbyNamesB = read(tables.LobbyB);
	s_gameNames = read(tables.Game);
	s_families.clear();
	for (const auto& name : *s_gameNames) {
		auto family = Presets::FamilyOf(name);
		if (!family.empty() && std::ranges::none_of(s_families, [&](const auto& f) { return Utils::EqualsI(f, family); }))
			s_families.push_back(std::move(family));
	}
}

std::vector<std::pair<std::string, float>> FontNames::FontNamesOf(std::string_view family) {
	std::vector<std::pair<std::string, float>> res;
	if (!s_gameNames)
		return res;
	for (const auto& name : *s_gameNames) {
		if (!Utils::EqualsI(Presets::FamilyOf(name), family) || std::ranges::any_of(res, [&](const auto& f) { return Utils::EqualsI(f.first, name); }))
			continue;
		if (const auto size = SizeFromFdtFileName(name); size > 0)
			res.emplace_back(name, size);
	}
	return res;
}

std::optional<std::string> FontNames::TryGetFontName(GameFont* font) {
	auto count = 0;
	const auto manager = TryGetManager(count);
	if (!manager)
		return std::nullopt;
	const auto index = manager->IndexOf(font);
	if (index < 0 || index >= count)
		return std::nullopt;
	const std::vector<std::string>* lobbyNames = nullptr;
	return NameAt(manager, index, count, lobbyNames);
}

FontReplacement::GameFont* FontNames::TryFindFont(std::string_view fontName) {
	auto count = 0;
	const auto manager = TryGetManager(count);
	if (!manager)
		return nullptr;
	const std::vector<std::string>* lobbyNames = nullptr;
	for (auto i = 0; i < count; i++) {
		if (Utils::EqualsI(NameAt(manager, i, count, lobbyNames), fontName))
			return manager->Font(i);
	}
	return nullptr;
}
