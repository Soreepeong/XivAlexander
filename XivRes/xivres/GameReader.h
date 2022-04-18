#pragma once

#include "SqpackReader.h"

namespace XivRes {
	class ExcelReader;

	class GameReader {
		const std::filesystem::path m_gamePath;
		mutable std::map<uint32_t, std::optional<SqpackReader>> m_readers;
		mutable std::mutex m_populateMtx;

	public:
		GameReader(std::filesystem::path gamePath)
			: m_gamePath(std::move(gamePath)) {
			for (const auto& iter : std::filesystem::recursive_directory_iterator(m_gamePath / "sqpack")) {
				if (iter.is_directory() || !iter.path().wstring().ends_with(L".win32.index"))
					continue;

				auto packFileName = std::filesystem::path{ iter.path() }.replace_extension("").replace_extension("").string();
				if (packFileName.size() < 6)
					continue;

				packFileName.resize(6);

				const auto packFileId = std::strtol(&packFileName[0], nullptr, 16);
				m_readers.emplace(packFileId, std::optional<SqpackReader>());
			}
		}

		[[nodiscard]] std::shared_ptr<PackedFileStream> GetPackedFileStream(const SqpackPathSpec& pathSpec) const {
			return GetSqpackReader(pathSpec).GetPackedFileStream(pathSpec);
		}

		[[nodiscard]] std::shared_ptr<PackedFileUnpackingStream> GetFileStream(const SqpackPathSpec& pathSpec, std::span<uint8_t> obfuscatedHeaderRewrite = {}) const {
			return std::make_shared<PackedFileUnpackingStream>(GetSqpackReader(pathSpec).GetPackedFileStream(pathSpec), obfuscatedHeaderRewrite);
		}

		[[nodiscard]] const SqpackReader& GetSqpackReader(const SqpackPathSpec& rawPathSpec) const {
			return GetSqpackReader(rawPathSpec.PackNameValue());
		}

		[[nodiscard]] const SqpackReader& GetSqpackReader(uint32_t packId) const {
			auto& item = m_readers[packId];
			if (item)
				return *item;

			const auto lock = std::lock_guard(m_populateMtx);
			if (item)
				return *item;

			const auto expacId = (packId >> 8) & 0xFF;
			if (expacId == 0)
				return item.emplace(SqpackReader::FromPath(m_gamePath / std::format("sqpack/ffxiv/{:0>6x}.win32.index", packId)));
			else
				return item.emplace(SqpackReader::FromPath(m_gamePath / std::format("sqpack/ex{}/{:0>6x}.win32.index", expacId, packId)));
		}

		[[nodiscard]] const SqpackReader& GetSqpackReader(uint8_t categoryId, uint8_t expacId, uint8_t partId) const {
			return GetSqpackReader((categoryId << 16) | (expacId << 8) | partId);
		}

		[[nodiscard]] inline ExcelReader GetExcelReader(const std::string& name) const;

		void PreloadAllSqpackFiles() const {
			const auto lock = std::lock_guard(m_populateMtx);
			for (const auto& key : m_readers | std::views::keys)
				void(GetSqpackReader(key));
		}
	};
}

#include "ExcelReader.h"

[[nodiscard]] inline XivRes::ExcelReader XivRes::GameReader::GetExcelReader(const std::string& name) const {
	return ExcelReader(&GetSqpackReader(0x0a0000), name);
}
