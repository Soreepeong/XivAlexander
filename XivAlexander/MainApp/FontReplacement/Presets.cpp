#include "pch.h"
#include "MainApp/FontReplacement/Presets.h"

#include "MainApp/FontReplacement/GameFontNames.h"

namespace Presets = XivAlexander::Apps::MainApp::FontReplacement::Presets;
namespace GameFontNames = XivAlexander::Apps::MainApp::FontReplacement::GameFontNames;

Presets::Faces Presets::Load(const std::filesystem::path& path) {
	std::ifstream file(path);
	if (!file)
		throw std::runtime_error(std::format("{} can't be read.", xivres::util::unicode::convert<std::string>(path.wstring())));
	const auto json = nlohmann::json::parse(file, nullptr, true, true);
	auto set = json.get<FontChanger::Structs::MultiFontSet>();

	// Relative paths in the preset are from its folder.
	const auto directory = absolute(path).parent_path();
	Faces faces;
	for (const auto& fontSet : set.FontSets) {
		for (auto& face : fontSet->Faces) {
			if (face->Name.empty() || faces.contains(face->Name))
				continue;
			for (auto& element : face->Elements) {
				auto& images = element->RendererSpecific.GlyphImages;
				if (images.IsEmbedded() || images.Path.empty())
					continue;
				if (const auto p = std::filesystem::path(xivres::util::unicode::convert<std::wstring>(images.Path)); p.is_relative())
					images.Path = xivres::util::unicode::convert<std::string>((directory / p).lexically_normal().wstring());
			}
			// The name is copied first: the face is moved from.
			auto name = face->Name;
			faces.emplace(std::move(name), std::make_shared<FontChanger::Structs::Face>(std::move(*face)));
		}
	}

	if (faces.empty())
		throw std::runtime_error("The preset has no faces.");
	return faces;
}

void Presets::Combine(Faces& into, const Faces& from) {
	for (const auto& [name, face] : from)
		into.insert_or_assign(name, face);
}

Presets::Faces Presets::OnlyFamily(const Faces& faces, std::string_view family) {
	Faces res;
	for (const auto& [name, face] : faces) {
		if (EqualsIgnoringCase(GameFontNames::FamilyOf(name), family))
			res.emplace(name, face);
	}
	return res;
}

std::vector<std::filesystem::path> Presets::GlyphImageFolders(const Faces& faces) {
	std::vector<std::filesystem::path> res;
	for (const auto& face : faces | std::views::values) {
		for (const auto& element : face->Elements) {
			const auto& images = element->RendererSpecific.GlyphImages;
			if (element->Renderer != FontChanger::Structs::RendererEnum::GlyphImages || images.IsEmbedded() || images.Path.empty())
				continue;
			auto folder = std::filesystem::path(xivres::util::unicode::convert<std::wstring>(images.Path));
			if (std::ranges::find(res, folder) == res.end())
				res.push_back(std::move(folder));
		}
	}
	return res;
}
