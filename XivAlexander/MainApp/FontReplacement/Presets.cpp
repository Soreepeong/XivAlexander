#include "pch.h"
#include "MainApp/FontReplacement/Presets.h"

#include "Config.h"
#include "MainApp/FontReplacement/GameFontNames.h"
#include "Utils/Win32.h"
#include "resource.h"

namespace Presets = XivAlexander::Apps::MainApp::FontReplacement::Presets;
namespace GameFontNames = XivAlexander::Apps::MainApp::FontReplacement::GameFontNames;

namespace {
	// Comments are allowed: FontChanger writes none, but people may.
	FontChanger::Structs::MultiFontSet ReadPresetFile(const std::filesystem::path& path) {
		std::ifstream file(path);
		if (!file)
			throw std::runtime_error(XivAlexander::Config::Acquire()->Runtime.FormatStringResUtf8(IDS_FONTPRESET_ERROR_CANNOTREAD, path.wstring()));
		return nlohmann::json::parse(file, nullptr, true, true).get<FontChanger::Structs::MultiFontSet>();
	}

	// Gets the first of name, "stem (2).ext", "stem (3).ext", ... that isn't in the folder.
	std::filesystem::path UnusedName(const std::filesystem::path& folder, const std::filesystem::path& name) {
		std::error_code ec;
		if (!exists(folder / name, ec))
			return name;
		for (auto i = 2;; ++i) {
			auto candidate = std::filesystem::path(std::format(L"{} ({}){}", name.stem().wstring(), i, name.extension().wstring()));
			if (!exists(folder / candidate, ec))
				return candidate;
		}
	}
}

std::filesystem::path Presets::Folder(Config& config) {
	return Utils::Win32::EnsureDirectory(config.Init.ResolveConfigStorageDirectoryPath() / L"FontPresets");
}

std::filesystem::path Presets::Resolve(const std::filesystem::path& folder, const std::string& preset) {
	// An absolute path replaces the folder.
	return folder / std::filesystem::path(xivres::util::unicode::convert<std::wstring>(preset));
}

std::vector<std::filesystem::path> Presets::RelativeGlyphImageFolders(const std::filesystem::path& path) {
	std::vector<std::filesystem::path> res;
	for (const auto& fontSet : ReadPresetFile(path).FontSets) {
		for (const auto& face : fontSet->Faces) {
			for (const auto& element : face->Elements) {
				if (!element->UsesProjectDirectory())
					continue;
				auto folder = std::filesystem::path(xivres::util::unicode::convert<std::wstring>(element->RendererSpecific.GlyphImages.Path)).lexically_normal();
				if (std::ranges::find(res, folder) == res.end())
					res.push_back(std::move(folder));
			}
		}
	}
	return res;
}

bool Presets::IsCopyableGlyphImageFolder(const std::filesystem::path& relative) {
	const auto normal = relative.lexically_normal();
	if (normal.empty() || normal.is_absolute() || normal.has_root_name() || normal == L".")
		return false;
	return *normal.begin() != L"..";
}

std::filesystem::path Presets::Import(const std::filesystem::path& path, const std::filesystem::path& folder, const std::vector<std::filesystem::path>& glyphImageFolders) {
	if (glyphImageFolders.empty()) {
		const auto name = UnusedName(folder, path.filename());
		copy_file(path, folder / name);
		return name;
	}

	for (const auto& relative : glyphImageFolders) {
		if (!IsCopyableGlyphImageFolder(relative))
			throw std::runtime_error(XivAlexander::Config::Acquire()->Runtime.FormatStringResUtf8(IDS_FONTPRESET_ERROR_GLYPHFOLDEROUTSIDE, relative.wstring()));
	}

	// A folder of the preset's name, with the preset and its glyph images as they were around it.
	const auto directoryName = UnusedName(folder, path.stem());
	const auto directory = folder / directoryName;
	create_directories(directory);
	copy_file(path, directory / path.filename());
	const auto source = path.parent_path();
	for (const auto& relative : glyphImageFolders) {
		const auto target = directory / relative.lexically_normal();
		create_directories(target);
		copy(source / relative, target, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
	}
	return directoryName / path.filename();
}

Presets::Faces Presets::Load(const std::filesystem::path& path) {
	auto set = ReadPresetFile(path);

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
		throw std::runtime_error(XivAlexander::Config::Acquire()->Runtime.FormatStringResUtf8(IDS_FONTPRESET_ERROR_NOFACES));
	return faces;
}

void Presets::Combine(Faces& into, const Faces& from) {
	for (const auto& [name, face] : from)
		into.insert_or_assign(name, face);
}

Presets::Faces Presets::FacesOfFamily(const Faces& faces, std::string_view family) {
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
