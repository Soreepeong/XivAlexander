#include "pch.h"
#include "XivAlexanderCommon/Sqex/Sqpack/EntryProvider.h"

Sqex::Sqpack::EntryProvider::EntryProvider(EntryPathSpec pathSpec)
	: m_pathSpec(std::move(pathSpec)) {
}

bool Sqex::Sqpack::EntryProvider::UpdatePathSpec(const EntryPathSpec& r) {
	if (m_pathSpec != r)
		return false;
	if (!m_pathSpec.HasOriginal())
		m_pathSpec.FullPath = r.FullPath;
	if (!m_pathSpec.HasFullPathHash())
		m_pathSpec.FullPathHash = r.FullPathHash;
	if (!m_pathSpec.HasComponentHash()) {
		m_pathSpec.PathHash = r.PathHash;
		m_pathSpec.NameHash = r.NameHash;
	}
	return true;
}

const Sqex::Sqpack::EntryPathSpec& Sqex::Sqpack::EntryProvider::PathSpec() const {
	return m_pathSpec;
}
