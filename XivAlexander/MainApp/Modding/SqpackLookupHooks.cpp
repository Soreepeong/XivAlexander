#include "pch.h"
#include "MainApp/Modding/SqpackLookupHooks.h"

#include <deque>

#include <xivres/util.module_relative.h>
#include <xivres/util.on_dtor.h>

#include "MainApp/Modding/PathLogFilter.h"
#include "MainApp/Modding/PathRewriter.h"
#include "Game/SignatureDefinitions.h"
#include "Misc/Hooks.h"
#include "Misc/Logger.h"

namespace XivAlexander::Apps::MainApp::Features::Modding {
	struct SqpackLookupHooks::Implementation {
		const PathRewriter& Rewriter;
		const std::shared_ptr<Misc::Logger> Logger;
		PathLogFilter LogFilter;

		std::deque<Misc::Hooks::PointerFunctionOf<Game::Resolved::SqPackIndexLookupFn>> Hooks{};

		xivres::util::on_dtor::multi Cleanup;

		explicit Implementation(const PathRewriter& rewriter)
			: Rewriter(rewriter)
			, Logger(Misc::Logger::Acquire()) {

			std::vector<Game::Resolved::SqPackIndexLookupFn> found;
			if (Game::Resolved::SqPackIndexLookupFunctions.Resolve(found) != Game::Signatures::ResolveError::Ok)
				return;

			for (const auto fn : found) {
				auto& hook = Hooks.emplace_back("FFXIV::SqPackManager::TryGetOffsetFromIndex", fn);
				Cleanup += hook.SetHook([this, &hook, fn](void* manager, const char* path, uint32_t* outOffset, uint32_t* outDatIndex) {
					if (!path || !*path)
						return hook.bridge(manager, path, outOffset, outDatIndex);

					std::string description;
					const auto replacement = Rewriter.Rewrite(path, description);
					const auto found = hook.bridge(manager, replacement.empty() ? path : replacement.c_str(), outOffset, outDatIndex);

					if (const auto original = std::string(path); LogFilter.ShouldLog(original, !description.empty(), reinterpret_cast<size_t>(fn), original)) {
						const auto& shown = description.empty() ? original : description;
						if (found) {
							Logger->Format(LogCategory::GameResourceOverrider,
								"{} => dat{}+{:#x} (f={})", shown, *outDatIndex, uint64_t{*outOffset} * 128, xivres::util::module_relative(fn));
						} else {
							Logger->Format(LogCategory::GameResourceOverrider,
								"{} => not found (f={})", shown, xivres::util::module_relative(fn));
						}
					}

					return found;
				});
			}
		}

		~Implementation() {
			Cleanup.clear();
		}
	};

	SqpackLookupHooks::SqpackLookupHooks(const PathRewriter& rewriter)
		: m_pImpl(std::make_unique<Implementation>(rewriter)) {}

	SqpackLookupHooks::~SqpackLookupHooks() = default;
}
