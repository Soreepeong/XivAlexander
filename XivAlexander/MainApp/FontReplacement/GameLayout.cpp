#include "pch.h"
#include "MainApp/FontReplacement/GameLayout.h"

#include "MainApp/FontReplacement/Host.h"
#include "Utils/Win32/LoadedModule.h"
#include "resource.h"
#include "XivAlexander.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	std::recursive_mutex s_mutex;
	std::optional<xivres::game_layout> s_layout;

	// Problems recorded here rather than by the layout (CheckFixed), reported with its.
	std::vector<std::string> s_problems;

	std::string_view ReadSignatures() {
		const auto module = static_cast<HMODULE>(Dll::Module());
		const auto resource = FindResourceW(module, MAKEINTRESOURCEW(IDR_GAMEFONTSIGNATURES), RT_RCDATA);
		if (!resource)
			throw std::runtime_error("game_font_signatures.json isn't embedded.");
		const auto data = LockResource(LoadResource(module, resource));
		return {static_cast<const char*>(data), SizeofResource(module, resource)};
	}

	std::vector<uint8_t> ReadExecutable() {
		std::ifstream file(FontReplacement::Host::GameFileName(), std::ios::binary);
		if (!file)
			throw std::runtime_error("The game's executable can't be read.");
		return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
	}
}

xivres::game_layout& FontReplacement::GameLayout::Layout() {
	const auto lock = std::scoped_lock(s_mutex);
	if (!s_layout) {
		const auto exe = ReadExecutable();
		s_layout.emplace(ReadSignatures(), exe, Host::GameBaseAddress());
		// Only a part that needs one of these can't work; Resolve reports them then.
		for (const auto& message : s_layout->failures() | std::views::values)
			Host::Debug("{}", message);
		for (const auto& message : s_layout->conflicts() | std::views::values)
			Host::Debug("{}", message);
	}
	return *s_layout;
}

void FontReplacement::GameLayout::Resolve(std::string_view part, const std::function<void()>& fn) {
	const auto lock = std::scoped_lock(s_mutex);
	auto& layout = Layout();
	s_problems.clear();
	std::string error;
	try {
		layout.resolve(part, fn);
	} catch (const std::runtime_error& e) {
		error = e.what();
	}

	auto problems = std::move(s_problems);
	s_problems.clear();
	if (problems.empty() && error.empty())
		return;

	if (error.empty())
		error = std::format("{} can't work with this version of the game", part);
	for (const auto& problem : problems)
		error += "; " + problem;
	throw std::runtime_error(error);
}

int32_t FontReplacement::GameLayout::Get(const std::string& name) {
	const auto lock = std::scoped_lock(s_mutex);
	return Layout().get(name);
}

std::optional<int32_t> FontReplacement::GameLayout::TryGet(const std::string& name) {
	const auto lock = std::scoped_lock(s_mutex);
	return Layout().try_get(name);
}

std::string FontReplacement::GameLayout::GetString(const std::string& name) {
	const auto lock = std::scoped_lock(s_mutex);
	return Layout().get_string(name);
}

std::vector<int32_t> FontReplacement::GameLayout::GetList(const std::string& name) {
	const auto lock = std::scoped_lock(s_mutex);
	return Layout().get_list(name);
}

uintptr_t FontReplacement::GameLayout::Address(const std::string& signature, const std::string& rel32) {
	const auto lock = std::scoped_lock(s_mutex);
	return static_cast<uintptr_t>(Layout().address(signature, rel32));
}

uintptr_t FontReplacement::GameLayout::Target(const std::string& rel32) {
	const auto lock = std::scoped_lock(s_mutex);
	return static_cast<uintptr_t>(Layout().target(rel32));
}

const xivres::game_layout::match* FontReplacement::GameLayout::Match(const std::string& signature) {
	const auto lock = std::scoped_lock(s_mutex);
	return Layout().find(signature);
}

uintptr_t FontReplacement::GameLayout::Target(const xivres::game_layout::match& match, const std::string& rel32) {
	return match.Captures.contains(rel32) ? static_cast<uintptr_t>(match.target(rel32)) : 0;
}

void FontReplacement::GameLayout::CheckFixed(const std::string& prefix, size_t size, std::initializer_list<std::pair<const char*, size_t>> fields) {
	const auto lock = std::scoped_lock(s_mutex);
	if (const auto captured = TryGet(prefix); captured && static_cast<size_t>(*captured) != size)
		s_problems.push_back(std::format("{} is 0x{:X} bytes in the game, but 0x{:X} here", prefix, *captured, size));

	for (const auto& [field, offset] : fields) {
		const auto name = std::format("{}.{}", prefix, field);
		if (const auto captured = TryGet(name); captured && static_cast<size_t>(*captured) != offset)
			s_problems.push_back(std::format("{} is 0x{:X} in the game, but 0x{:X} here", name, *captured, offset));
	}
}
