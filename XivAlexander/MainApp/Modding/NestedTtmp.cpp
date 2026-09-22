#include "pch.h"
#include "MainApp/Modding/NestedTtmp.h"

namespace XivAlexander::Apps::MainApp::Features::Modding {
	void NestedTtmp::Traverse(bool traverseEnabledOnly, const std::function<void(NestedTtmp&)>& cb) {
		if (traverseEnabledOnly && !Enabled)
			return;
		cb(*this);
		if (Children)
			for (auto& t : *Children)
				t->Traverse(traverseEnabledOnly, cb);
	}

	void NestedTtmp::Traverse(bool traverseEnabledOnly, const std::function<void(const NestedTtmp&)>& cb) const {
		if (traverseEnabledOnly && !Enabled)
			return;
		cb(*this);
		if (Children)
			for (const auto& t : *Children)
				const_cast<const NestedTtmp*>(t.get())->Traverse(traverseEnabledOnly, cb);
	}

	NestedTtmp::TraverseCallbackResult NestedTtmp::TraverseInterruptible(bool traverseEnabledOnly, const std::function<TraverseCallbackResult(NestedTtmp&)>& cb) {
		if (traverseEnabledOnly && !Enabled)
			return Continue;
		if (const auto res = cb(*this); res != Continue)
			return res;
		if (Children) {
			for (auto it = Children->begin(); it != Children->end();) {
				switch ((*it)->TraverseInterruptible(traverseEnabledOnly, cb)) {
					case Break:
						return Break;
					case Delete:
						it = Children->erase(it);
						break;
					default:
						++it;
				}
			}
			return Continue;
		}
		return Continue;
	}

	NestedTtmp::TraverseCallbackResult NestedTtmp::TraverseInterruptible(bool traverseEnabledOnly, const std::function<TraverseCallbackResult(const NestedTtmp&)>& cb) const {
		if (traverseEnabledOnly && !Enabled)
			return Continue;
		if (const auto res = cb(*this); res != Continue)
			return res;
		if (Children) {
			for (const auto& t : *Children) {
				switch (const_cast<const NestedTtmp*>(t.get())->TraverseInterruptible(traverseEnabledOnly, cb)) {
					case Break:
						return Break;
					case Delete:
						throw std::runtime_error("Cannot delete from const");
				}
			}
			return Continue;
		}
		return Continue;
	}

	size_t NestedTtmp::Count() const {
		size_t i = Ttmp.has_value() ? 1 : 0;
		Traverse(false, [&i](const auto&) { ++i; });
		return i;
	}

	void NestedTtmp::Sort() {
		std::sort(Children->begin(), Children->end(), [](const auto& l, const auto& r) {
			if (l->Index == r->Index)
				return l->Path.wstring() < r->Path.wstring();
			return l->Index < r->Index;
		});
	}

	std::shared_ptr<NestedTtmp> NestedTtmp::Find(const std::filesystem::path& path) {
		if (IsGroup())
			for (auto& child : *Children)
				if (equivalent(child->Path, path))
					return child;
		return nullptr;
	}

	void NestedTtmp::RemoveEmptyChildren() {
		if (!Children)
			return;
		for (auto it = Children->begin(); it != Children->end();) {
			auto& t = **it;
			t.RemoveEmptyChildren();
			if (!t.Ttmp && (!t.Children || t.Children->empty())) {
				t.Parent = nullptr;
				it = Children->erase(it);
			} else
				++it;
		}
	}
}
