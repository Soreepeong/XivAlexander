#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "MainApp/Modding/TtmpSet.h"

namespace XivAlexander::Apps::MainApp::Features::Modding {
	struct NestedTtmp {
		uint64_t Index{ UINT64_MAX };
		std::filesystem::path Path;
		std::shared_ptr<NestedTtmp> Parent;
		bool Enabled = true;

		std::optional<std::vector<std::shared_ptr<NestedTtmp>>> Children;
		std::optional<TtmpSet> Ttmp;
		std::optional<std::filesystem::path> RenameTo;

		bool IsGroup() const {
			return Children.has_value();
		}

		enum TraverseCallbackResult {
			Continue,
			Break,
			Delete,
		};

		void Traverse(bool traverseEnabledOnly, const std::function<void(NestedTtmp&)>& cb);
		void Traverse(bool traverseEnabledOnly, const std::function<void(const NestedTtmp&)>& cb) const;
		TraverseCallbackResult TraverseInterruptible(bool traverseEnabledOnly, const std::function<TraverseCallbackResult(NestedTtmp&)>& cb);
		TraverseCallbackResult TraverseInterruptible(bool traverseEnabledOnly, const std::function<TraverseCallbackResult(const NestedTtmp&)>& cb) const;
		size_t Count() const;
		void Sort();
		void RemoveEmptyChildren();
		std::shared_ptr<NestedTtmp> Find(const std::filesystem::path& path);
	};
}
