#include "Base.h"

#include "Json.h"

// Where things are, read as Config's InitConfigRepository and RuntimeConfigRepository would, without loading them.

namespace {
	constexpr size_t MaxConfigSize = 64 * 1024 * 1024;

#if XA_LOADER64
	constexpr GUID LocalAppDataFolderId{0xF1B32785, 0x6FBA, 0x4FCF, {0x9D, 0x55, 0x7B, 0x8E, 0x7F, 0x15, 0x70, 0x91}};
	constexpr GUID RoamingAppDataFolderId{0x3EB685DB, 0x65F9, 0x4CF6, {0xA0, 0x3A, 0xE3, 0xEF, 0x65, 0x72, 0x9F, 0x3D}};
#endif

#if XA_LOADER64
	SRWLOCK s_programDirectoryLock = SRWLOCK_INIT;
	wchar_t* s_programDirectory;

	// A path setting of config.xivalexinit.json next to this DLL; relative to this DLL's folder.
	Loader::Str ReadInitConfigPath(const char* key) {
		using namespace Loader;
		const auto path = Join(g_moduleDirectory, L"config.xivalexinit.json");
		Bytes file;
		if (!Exists(path.c_str()))
			return {};
		Json::Document doc;
		if (!ReadWholeFile(path.c_str(), file, MaxConfigSize) || !doc.Parse(file.Data, file.Size)) {
			DebugPrint(L"Cannot read {}", path);
			return {};
		}
		const auto value = Json::Find(doc.Root(), key);
		if (!value.IsString())
			return {};
		const auto text = value.DecodeWide();
		return text.empty() ? Str() : TranslatePath(text.c_str(), g_moduleDirectory);
	}
#endif
}

Loader::Str Loader::ProgramDirectory() {
#if XA_LOADER64
	AcquireSRWLockExclusive(&s_programDirectoryLock);
	if (!s_programDirectory) {
		Str resolved;
		if (Exists(Join(g_moduleDirectory, FeatureDllName).c_str()))
			resolved = g_moduleDirectory;
		else if (auto configured = ReadInitConfigPath(
#ifdef _DEBUG
			"XivAlexFolderPath_DEBUG"
#else
			"XivAlexFolderPath"
#endif
		); !configured.empty())
			resolved = static_cast<Str&&>(configured);
		else if (const auto localAppData = KnownFolder(LocalAppDataFolderId); !localAppData.empty())
			resolved = Join(localAppData, L"XivAlexander");
		if (!resolved.empty()) {
			s_programDirectory = static_cast<wchar_t*>(Alloc((resolved.size() + 1) * sizeof(wchar_t)));
			memcpy(s_programDirectory, resolved.c_str(), resolved.size() * sizeof(wchar_t));
		}
	}
	Str res(s_programDirectory ? s_programDirectory : L"");
	ReleaseSRWLockExclusive(&s_programDirectoryLock);
	return res;
#else
	return g_moduleDirectory;
#endif
}

#if XA_LOADER64

Loader::Str Loader::ConfigDirectory() {
	if (auto configured = ReadInitConfigPath("FixedConfigurationFolderPath"); !configured.empty())
		return configured;
	const auto roaming = KnownFolder(RoamingAppDataFolderId);
	return roaming.empty() ? Str() : Join(roaming, L"XivAlexander");
}

void Loader::PathList::Add(Str s) {
	const auto items = static_cast<Str*>(Alloc((Count + 1) * sizeof(Str)));
	// Str is trivially relocatable: its fields move over bitwise, and the old block is freed without destructors.
	if (Items)
		memcpy(static_cast<void*>(items), Items, Count * sizeof(Str));
	Free(Items);
	Items = items;
	new(&Items[Count++]) Str(static_cast<Str&&>(s));
}

void Loader::PathList::Reset() {
	for (size_t i = 0; i < Count; ++i)
		Items[i].~Str();
	Free(Items);
	Items = nullptr;
	Count = 0;
}

void Loader::ReadChainLoadDlls(const char* configName, const char* legacyConfigName, PathList& out) {
	const auto configDir = ConfigDirectory();
	if (configDir.empty())
		return;
	const auto configPath = Join(configDir, L"config.runtime.json");
	if (!Exists(configPath.c_str()))
		return;
	Bytes file;
	Json::Document doc;
	if (!ReadWholeFile(configPath.c_str(), file, MaxConfigSize) || !doc.Parse(file.Data, file.Size) || !doc.Root().IsObject()) {
		DebugPrint(L"Cannot read the ChainLoad setting from {}", configPath);
		return;
	}

	// The section of this game: its key matches the game's path without regard to case. Of several, nlohmann::json
	// (a std::map) yields the one first in byte order, and of duplicate keys it keeps the last.
	struct SectionSearch {
		Str Wanted;
		Json::Value Section;
		char* BestKey = nullptr;
		size_t BestKeyLength = 0;
	} search{ProcessImagePath(GetCurrentProcess())};
	Json::ForEachMember(doc.Root(), [](void* ctx, const Json::Value& key, const Json::Value& value) {
		auto& s = *static_cast<SectionSearch*>(ctx);
		const auto buf = static_cast<char*>(Alloc(key.RawLength()));
		size_t n;
		if (!key.DecodeUtf8(buf, n)) {
			Free(buf);
			return;
		}
		const auto wide = FromUtf8(buf, n);
		if (CompareStringOrdinal(wide.c_str(), static_cast<int>(wide.size()), s.Wanted.c_str(), static_cast<int>(s.Wanted.size()), TRUE) != CSTR_EQUAL) {
			Free(buf);
			return;
		}
		auto replace = !s.BestKey;
		if (!replace) {
			const auto common = n < s.BestKeyLength ? n : s.BestKeyLength;
			const auto cmp = memcmp(buf, s.BestKey, common);
			replace = cmp < 0 || (cmp == 0 && n <= s.BestKeyLength);
		}
		if (replace) {
			Free(s.BestKey);
			s.BestKey = buf;
			s.BestKeyLength = n;
			s.Section = value;
		} else {
			Free(buf);
		}
	}, &search);
	Free(search.BestKey);
	if (!search.Section.IsObject())
		return;

	Json::Value list;
	if (const auto group = Json::Find(search.Section, "ChainLoad"); group.IsObject())
		list = Json::Find(group, configName);
	if (!list.Exists())
		list = Json::Find(search.Section, legacyConfigName);
	if (!list.Exists())
		return;

	const auto add = [](void* ctx, const Json::Value& item) {
		if (!item.IsString())
			return;
		if (const auto value = item.DecodeWide(); !value.empty())
			static_cast<PathList*>(ctx)->Add(TranslatePath(value.c_str(), g_moduleDirectory));
	};
	if (list.IsArray())
		Json::ForEachElement(list, add, &out);
	else
		add(&out, list);
}

#endif
