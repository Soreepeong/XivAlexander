#include "pch.h"
#include "Game/ComplexSignature.h"

#include <xivres/util.module_relative.h>

#include "Game/ResolveContext.h"

#include "Misc/Logger.h"

using XivAlexander::Game::Signatures::ComplexSignatureBase;
using XivAlexander::Game::Signatures::MatchCount;
using XivAlexander::Game::Signatures::RegexSignature;
using XivAlexander::Game::Signatures::ResolveContext;
using XivAlexander::Game::Signatures::ResolveError;
using XivAlexander::Game::Signatures::ResolveFailure;
using XivAlexander::Game::Signatures::ScanResult;

namespace {
	std::mutex& RegistryMutex() {
		static std::mutex s_mtx;
		return s_mtx;
	}

	std::vector<const ComplexSignatureBase*>& Registry() {
		static std::vector<const ComplexSignatureBase*> s_registry;
		return s_registry;
	}

	thread_local std::vector<const ComplexSignatureBase*> t_resolving;

	class ResolvingGuard {
	public:
		explicit ResolvingGuard(const ComplexSignatureBase& signature) {
			if (std::ranges::find(t_resolving, &signature) != t_resolving.end())
				ResolveContext::Fail(ResolveError::Dependency, "depends on itself");
			t_resolving.push_back(&signature);
		}

		ResolvingGuard(const ResolvingGuard&) = delete;
		ResolvingGuard& operator=(const ResolvingGuard&) = delete;

		~ResolvingGuard() {
			t_resolving.pop_back();
		}
	};
}

std::string XivAlexander::Game::Signatures::to_string(const void* pointer) {
	return xivres::util::module_relative(pointer).to_string();
}

std::string XivAlexander::Game::Signatures::to_string(Address address) {
	return to_string(address.Get());
}

std::span<const uint8_t> ResolveContext::Image() const {
	return {reinterpret_cast<const uint8_t*>(*m_module), m_module.NtHeaders().OptionalHeader.SizeOfImage};
}

std::span<const uint8_t> ResolveContext::Section(std::string_view name) const {
	try {
		return m_module.SectionFrom(name);
	} catch (const std::exception&) {
		Fail(ResolveError::NotFound, std::format("section {} not found", name));
	}
}

bool ResolveContext::InSection(const void* p, std::string_view section) const {
	const auto s = Section(section);
	return p >= s.data() && p < s.data() + s.size();
}

const void* ResolveContext::RequireInSection(const void* p, std::string_view section, std::string_view what) const {
	if (!InSection(p, section))
		Fail(ResolveError::Invalid, std::format("{} at {:p} is outside {}", what, p, section));
	return p;
}

std::span<const uint8_t> ResolveContext::FunctionContaining(const void* p, std::string_view what) const {
	const auto fn = m_module.FunctionAt(p);
	if (fn.empty())
		Fail(ResolveError::NotFound, std::format("{}: no function contains {}", what, xivres::util::module_relative(p)));
	return fn;
}

std::span<const uint8_t> ResolveContext::FunctionStartingAt(const void* p, std::string_view what) const {
	const auto fn = FunctionStartingAt(p);
	if (fn.empty())
		Fail(ResolveError::Invalid, std::format("{}: no function starts at {}", what, xivres::util::module_relative(p)));
	return fn;
}

std::span<const uint8_t> ResolveContext::FunctionStartingAt(const void* p) const {
	if (const auto fn = m_module.FunctionAt(p); fn.data() == p)
		return fn;
	return {};
}

ScanResult ResolveContext::Unique(const RegexSignature& signature, std::span<const uint8_t> data, std::string_view what) const {
	auto [result, count] = signature.LookupUnique(data);
	if (count != MatchCount::One)
		Fail(count == MatchCount::None ? ResolveError::NotFound : ResolveError::Ambiguous, std::format("{} {}", what, ToString(count)));
	return std::move(result);
}

ScanResult ResolveContext::First(const RegexSignature& signature, std::span<const uint8_t> data, std::string_view what) const {
	auto result = Find(signature, data);
	if (!result)
		Fail(ResolveError::NotFound, std::format("{} not found", what));
	return std::move(*result);
}

std::optional<ScanResult> ResolveContext::Find(const RegexSignature& signature, std::span<const uint8_t> data) const {
	for (const auto& result : signature.Lookup(data))
		return result;
	return std::nullopt;
}

std::vector<ScanResult> ResolveContext::All(const RegexSignature& signature, std::span<const uint8_t> data) const {
	std::vector<ScanResult> results;
	for (const auto& result : signature.Lookup(data))
		results.push_back(result);
	return results;
}

ScanResult ResolveContext::MatchAt(const RegexSignature& signature, std::span<const uint8_t> data, std::string_view what) const {
	auto result = TryMatchAt(signature, data);
	if (!result)
		Fail(ResolveError::Mismatch, std::format("{} at {} does not match", what, xivres::util::module_relative(data.data())));
	return std::move(*result);
}

std::optional<ScanResult> ResolveContext::TryMatchAt(const RegexSignature& signature, std::span<const uint8_t> data) const {
	if (ScanResult result; signature.MatchAt(data, result))
		return result;
	return std::nullopt;
}

std::vector<const void*> ResolveContext::Vtable(const void* const* vtable, size_t maxSlots, std::string_view what) const {
	RequireInSection(vtable, ".rdata", what);

	std::vector<const void*> slots;
	for (size_t i = 0; i < maxSlots && InSection(vtable[i], ".text"); i++)
		slots.push_back(vtable[i]);
	if (slots.empty())
		Fail(ResolveError::Invalid, std::format("{} at {} has no slots pointing into the code", what, xivres::util::module_relative(vtable)));
	return slots;
}

void ResolveContext::Fail(ResolveError code, std::string detail) {
	throw ResolveFailure(code, std::move(detail));
}

ComplexSignatureBase::ComplexSignatureBase(std::string_view name)
	: m_name(name) {
	std::lock_guard lock(RegistryMutex());
	Registry().push_back(this);
}

ComplexSignatureBase::~ComplexSignatureBase() {
	std::lock_guard lock(RegistryMutex());
	std::erase(Registry(), this);
}

void ComplexSignatureBase::LogResolved(const Utils::Win32::LoadedModule& module, const std::string& description) const {
	const auto logger = XivAlexander::Misc::Logger::Acquire();
	if (*module == *Utils::Win32::LoadedModule::MainModule())
		logger->Format<LogLevel::Info>(LogCategory::Signatures, "{}: {}", m_name, description);
	else
		logger->Format<LogLevel::Info>(LogCategory::Signatures, "{} in image {:p}: {}", m_name, static_cast<const void*>(*module), description);
}

void ComplexSignatureBase::LogFailed(const Utils::Win32::LoadedModule& module, const ResolveStatus& status) const {
	const auto logger = XivAlexander::Misc::Logger::Acquire();
	if (*module == *Utils::Win32::LoadedModule::MainModule())
		logger->Format<LogLevel::Warning>(LogCategory::Signatures, "{}", status.Detail);
	else
		logger->Format<LogLevel::Warning>(LogCategory::Signatures, "{} (image {:p})", status.Detail, static_cast<const void*>(*module));
}

std::vector<const ComplexSignatureBase*> ComplexSignatureBase::All() {
	std::lock_guard lock(RegistryMutex());
	return Registry();
}

XivAlexander::Game::Signatures::ResolveStatus ComplexSignatureBase::RunResolver(const Utils::Win32::LoadedModule& module, InvokeFn invoke, void* out) const {
	try {
		const ResolvingGuard guard(*this);
		ResolveContext ctx(module);
		invoke(*this, ctx, out);
		return {};
	} catch (const ResolveFailure& e) {
		return {e.Status.Code, std::format("{}: {}", m_name, e.Status.Detail)};
	} catch (const std::exception& e) {
		return {ResolveError::Invalid, std::format("{}: {}", m_name, e.what())};
	}
}
