#include "pch.h"

#include <XivAlexanderCommon/Sqex/Excel/Reader.h>
#include <XivAlexanderCommon/Sqex/Sound/Reader.h>
#include <XivAlexanderCommon/Sqex/Sqpack/Reader.h>
#include <XivAlexanderCommon/Utils/Win32/ThreadPool.h>

int main() {
	Utils::Win32::TpEnvironment tpenv(L"");
	const auto reader = Sqex::Sqpack::GameReader(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\)");
	const auto extractScd = [&](std::string path) {
		tpenv.SubmitWork([path = std::move(path), &reader]() {
			if (path.empty())
				return;

			const auto targetScd = std::filesystem::path(LR"(Z:\sqmusic\scd)") / path;
			const auto targetLogg{ (std::filesystem::path(LR"(Z:\sqmusic\logg)") / path).replace_extension(".logg") };

			create_directories(targetScd.parent_path());
			create_directories(targetLogg.parent_path());

			try {
				const auto sourceStream = reader[path];
				const auto scdReader = Sqex::Sound::ScdReader(sourceStream);
				if (!scdReader.GetSoundEntryCount())
					return;
				Utils::Win32::Handle::FromCreateFile(targetScd, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS)
					.Write(0, std::span<const uint8_t>(sourceStream->ReadStreamIntoVector<uint8_t>(0)));
				Utils::Win32::Handle::FromCreateFile(targetLogg, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS)
					.Write(0, std::span<const uint8_t>(scdReader.GetSoundEntry(0).GetOggFile()));
				std::cout << std::format("[OK] {}: {}\n", path, sourceStream->StreamSize());

			} catch (const std::exception& e) {
				std::cout << std::format("[ERR] {}: {}\n", path, e.what());
			}
		});
	};
	for (const auto exh = Sqex::Excel::ExhReader("BGM", *reader["exd/BGM.exh"]); const auto & page : exh.Pages) {
		const auto exd = Sqex::Excel::ExdReader(exh, reader[exh.GetDataPathSpec(page, Sqex::Language::Unspecified)]);
		for (const auto id : exd.GetIds())
			extractScd(exd.ReadDepth2(id)[0].String.Parsed());
	}
	for (const auto exh = Sqex::Excel::ExhReader("OrchestrionPath", *reader["exd/OrchestrionPath.exh"]); const auto & page : exh.Pages) {
		const auto exd = Sqex::Excel::ExdReader(exh, reader[exh.GetDataPathSpec(page, Sqex::Language::Unspecified)]);
		for (const auto id : exd.GetIds())
			extractScd(exd.ReadDepth2(id)[0].String.Parsed());
	}
	tpenv.WaitOutstanding();
	return 0;
}