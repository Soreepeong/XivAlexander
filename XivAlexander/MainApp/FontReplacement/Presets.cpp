#include "pch.h"
#include "MainApp/FontReplacement/Presets.h"

#include <FontChanger.FixedSizeFont/fontdata_fixed_size_font.h>

#include "Config.h"
#include "Utils/StringUtils.h"
#include "Utils/Win32.h"
#include "resource.h"

namespace Presets = XivAlexander::Apps::MainApp::FontReplacement::Presets;

namespace {
	// Comments are allowed: FontChanger writes none, but people may.
	FontChanger::Structs::MultiFontSet ReadPresetFile(const std::filesystem::path& path) {
		std::ifstream file(path);
		if (!file)
			throw std::runtime_error(XivAlexander::Config::Acquire()->Runtime.FormatStringResUtf8(IDS_FONTPRESET_ERROR_CANNOTREAD, path.wstring()));
		return nlohmann::json::parse(file, nullptr, true, true).get<FontChanger::Structs::MultiFontSet>();
	}

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
		for (const auto& font : fontSet->Fonts) {
			for (const auto& element : font->Elements) {
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

	// Glyph image folders keep their places relative to the preset, so that its relative paths still resolve.
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

Presets::Fonts Presets::Load(const std::filesystem::path& path) {
	auto set = ReadPresetFile(path);

	const auto directory = absolute(path).parent_path();
	Fonts fonts;
	for (const auto& fontSet : set.FontSets) {
		for (auto& font : fontSet->Fonts) {
			if (font->Name.empty() || fonts.contains(font->Name))
				continue;
			for (auto& element : font->Elements) {
				auto& images = element->RendererSpecific.GlyphImages;
				if (images.IsEmbedded() || images.Path.empty())
					continue;
				if (const auto p = std::filesystem::path(xivres::util::unicode::convert<std::wstring>(images.Path)); p.is_relative())
					images.Path = xivres::util::unicode::convert<std::string>((directory / p).lexically_normal().wstring());
			}
			// The name is copied first: the font is moved from.
			auto name = font->Name;
			fonts.emplace(std::move(name), std::make_shared<FontChanger::Structs::Font>(std::move(*font)));
		}
	}

	if (fonts.empty())
		throw std::runtime_error(XivAlexander::Config::Acquire()->Runtime.FormatStringResUtf8(IDS_FONTPRESET_ERROR_NOFONTS));
	return fonts;
}

void Presets::Combine(Fonts& into, const Fonts& from) {
	for (const auto& [name, font] : from)
		into.insert_or_assign(name, font);
}

void Presets::Overlay(Fonts& into, const Fonts& from) {
	using FontChanger::FixedSizeFont::codepoint_merge_mode;
	using FontChanger::Structs::ElementSourceEnum;

	for (const auto& [name, top] : from) {
		const auto it = into.find(name);
		if (it == into.end() || it->second->Elements.empty()) {
			into.insert_or_assign(name, top);
			continue;
		}
		if (top->Elements.empty())
			continue;

		auto merged = std::make_shared<FontChanger::Structs::Font>(*it->second);
		const auto bottomSize = merged->Elements.front()->Size;
		const auto topSize = top->Elements.front()->Size;
		const auto factor = bottomSize > 0 && topSize > 0 ? bottomSize / topSize : 1.f;
		const auto append = [&](const FontChanger::Structs::FontElement& element, codepoint_merge_mode mode) {
			auto& copy = *merged->Elements.emplace_back(std::make_unique<FontChanger::Structs::FontElement>(element));
			copy.MergeMode = mode;
			if (factor != 1.f) {
				copy.OnFontCreateParametersChange();
				copy.Scale(factor);
			}
		};

		// The AddNew elements before the top's game font drew over the game's glyphs; as AddAll in reverse, they keep that precedence over the bottom font.
		const auto& elements = top->Elements;
		const auto firstGame = std::ranges::find_if(elements, [](const auto& e) { return e->Source == ElementSourceEnum::Game; });
		const auto leading = static_cast<size_t>(firstGame - elements.begin());
		for (auto i = leading; i-- > 0;) {
			if (elements[i]->MergeMode == codepoint_merge_mode::AddNew)
				append(*elements[i], codepoint_merge_mode::AddAll);
		}

		// A font made from a system font then has its game element (AddNew) fill only gaps, and its Replace elements draw over where their font has the glyph.
		for (size_t i = 0; i < elements.size(); i++) {
			if (i >= leading || elements[i]->MergeMode != codepoint_merge_mode::AddNew)
				append(*elements[i], elements[i]->MergeMode);
		}

		merged->OnElementChange();
		into.insert_or_assign(name, std::move(merged));
	}
}

std::string Presets::FamilyOf(std::string_view fontName) {
	if (const auto def = FontChanger::FixedSizeFont::find_fontdata_definition(fontName))
		return def->Family;
	const auto underscore = fontName.rfind('_');
	return std::string(underscore == std::string_view::npos || underscore == 0 ? fontName : fontName.substr(0, underscore));
}

Presets::Fonts Presets::FontsOfFamily(const Fonts& fonts, std::string_view family) {
	Fonts res;
	for (const auto& [name, font] : fonts) {
		if (Utils::EqualsI(FamilyOf(name), family))
			res.emplace(name, font);
	}
	return res;
}

std::vector<std::filesystem::path> Presets::GlyphImageFolders(const Fonts& fonts) {
	std::vector<std::filesystem::path> res;
	for (const auto& font : fonts | std::views::values) {
		for (const auto& element : font->Elements) {
			const auto& images = element->RendererSpecific.GlyphImages;
			if (element->Source != FontChanger::Structs::ElementSourceEnum::GlyphImages || images.IsEmbedded() || images.Path.empty())
				continue;
			auto folder = std::filesystem::path(xivres::util::unicode::convert<std::wstring>(images.Path));
			if (std::ranges::find(res, folder) == res.end())
				res.push_back(std::move(folder));
		}
	}
	return res;
}
