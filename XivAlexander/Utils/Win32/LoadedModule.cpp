#include "pch.h"
#include "Utils/Win32/LoadedModule.h"

#include "Utils/Win32/Process.h"

Utils::Win32::LoadedModule::LoadedModule(const wchar_t* pwszFileName, DWORD dwFlags, bool bRequire)
	: Closeable<HMODULE, FreeLibrary>(LoadLibraryExW(pwszFileName, nullptr, dwFlags), Null) {
	if (!m_object && bRequire)
		throw Error("LoadLibraryExW({}, nullptr, 0x{:X})", pwszFileName, dwFlags);
}

Utils::Win32::LoadedModule::LoadedModule(const std::filesystem::path& path, DWORD dwFlags, bool bRequire)
	: LoadedModule(path.c_str(), dwFlags, bRequire) {
}

Utils::Win32::LoadedModule::LoadedModule(LoadedModule&& r) noexcept
	: Closeable(std::move(r)) {
}

Utils::Win32::LoadedModule::LoadedModule(const LoadedModule& r)
	: Closeable(r.m_bOwnership&& r.m_object ? LoadLibraryW(Process::Current().PathOf(r.m_object).c_str()) : r.m_object,
		r.m_bOwnership) {
	if (r.m_object && !m_object)
		throw Error("LoadLibraryW");
}

Utils::Win32::LoadedModule& Utils::Win32::LoadedModule::operator=(LoadedModule&& r) noexcept {
	if (this == &r)
		return *this;

	Clear();
	m_object = r.m_object;
	m_bOwnership = r.m_bOwnership;
	r.Detach();
	return *this;
}

Utils::Win32::LoadedModule& Utils::Win32::LoadedModule::operator=(const LoadedModule& r) {
	if (!r.m_bOwnership || !r.m_object) {
		Clear();
		m_object = r.m_object;
		m_bOwnership = r.m_bOwnership;
	} else {
		*this = LoadMore(r);
	}
	return *this;
}

Utils::Win32::LoadedModule& Utils::Win32::LoadedModule::operator=(std::nullptr_t) {
	Clear();
	return *this;
}

Utils::Win32::LoadedModule::~LoadedModule() = default;

Utils::Win32::LoadedModule Utils::Win32::LoadedModule::LoadMore(const LoadedModule & module) {
	return LoadedModule(module.PathOf().c_str(), 0, true);
}

std::filesystem::path Utils::Win32::LoadedModule::PathOf() const {
	return Process::Current().PathOf(*this);
}

std::filesystem::path Utils::Win32::LoadedModule::BaseName() const {
	std::wstring result;
	result.resize(PATHCCH_MAX_CCH);
	result.resize(GetModuleBaseNameW(GetCurrentProcess(), m_object, &result[0], static_cast<DWORD>(result.size())));
	if (result.empty())
		throw Error("GetModuleBaseNameW");
	return result;
}

void Utils::Win32::LoadedModule::SetPinned() const {
	const auto pModuleHandleAsPsz = reinterpret_cast<LPCWSTR>(m_object);
	HMODULE dummy;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
		pModuleHandleAsPsz, &dummy)) {
		throw Error(
			"GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, 0x{:X})",
			reinterpret_cast<size_t>(pModuleHandleAsPsz));
	}
}

MODULEINFO Utils::Win32::LoadedModule::ModuleInfo() const {
	MODULEINFO res;
	if (!GetModuleInformation(GetCurrentProcess(), m_object, &res, sizeof res))
		throw Error("GetModuleInformation");
	return res;
}

const IMAGE_DOS_HEADER& Utils::Win32::LoadedModule::DosHeader() const {
	return *reinterpret_cast<const IMAGE_DOS_HEADER*>(m_object);
}

const IMAGE_NT_HEADERS& Utils::Win32::LoadedModule::NtHeaders() const {
	return *reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(m_object) + DosHeader().e_lfanew);
}

std::span<const IMAGE_SECTION_HEADER> Utils::Win32::LoadedModule::SectionHeaders() const {
	const auto& nt = NtHeaders();
	return std::span<const IMAGE_SECTION_HEADER>(IMAGE_FIRST_SECTION(&nt), nt.FileHeader.NumberOfSections);
}

std::span<const uint8_t> Utils::Win32::LoadedModule::DataDirectoryAt(size_t index) const {
	const auto& edh = NtHeaders().OptionalHeader.DataDirectory[index];
	if (!edh.Size || !edh.VirtualAddress)
		return {};
	
	return {reinterpret_cast<const uint8_t*>(m_object) + edh.VirtualAddress, edh.Size};
}

namespace {
	const IMAGE_RUNTIME_FUNCTION_ENTRY& PrimaryFunctionEntry(const uint8_t* base, const IMAGE_RUNTIME_FUNCTION_ENTRY& entry) {
		auto current = &entry;
		for (size_t depth = 0; depth < 32; depth++) {
			if (current->UnwindData & 1) {
				current = reinterpret_cast<const IMAGE_RUNTIME_FUNCTION_ENTRY*>(base + (current->UnwindData & ~1U));
				continue;
			}

			const auto info = base + current->UnwindData;
			if (!((info[0] >> 3) & UNW_FLAG_CHAININFO))
				break;

			const auto codeCount = static_cast<size_t>(info[2]);
			current = reinterpret_cast<const IMAGE_RUNTIME_FUNCTION_ENTRY*>(info + 4 + ((codeCount + 1) & ~static_cast<size_t>(1)) * 2);
		}
		return *current;
	}

	bool DecodeAt(const ZydisDecoder& decoder, const uint8_t* p, const uint8_t* end, ZydisDecodedInstruction& inst, ZydisDecodedOperand* operands) {
		return ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, p, static_cast<ZyanUSize>(end - p), &inst, operands));
	}

	const uint8_t* SkipPadding(const ZydisDecoder& decoder, const uint8_t* p, const uint8_t* end) {
		ZydisDecodedInstruction inst;
		ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
		while (p < end && DecodeAt(decoder, p, end, inst, operands) && (inst.mnemonic == ZYDIS_MNEMONIC_INT3 || inst.mnemonic == ZYDIS_MNEMONIC_NOP))
			p += inst.length;
		return p;
	}

	size_t LeafLength(const ZydisDecoder& decoder, const uint8_t* begin, const uint8_t* end) {
		auto reach = begin;
		for (auto p = begin; p < end;) {
			ZydisDecodedInstruction inst;
			ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
			if (!DecodeAt(decoder, p, end, inst, operands))
				return 0;

			const auto next = p + inst.length;
			const auto isBranch = inst.meta.category == ZYDIS_CATEGORY_COND_BR || inst.meta.category == ZYDIS_CATEGORY_UNCOND_BR;
			if (ZyanU64 target; isBranch
				&& operands[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE
				&& ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&inst, &operands[0], reinterpret_cast<ZyanU64>(p), &target))) {
				if (const auto t = reinterpret_cast<const uint8_t*>(target); t >= begin && t < end)
					reach = (std::max)(reach, t);
			}

			const auto stops = inst.meta.category == ZYDIS_CATEGORY_RET
				|| inst.meta.category == ZYDIS_CATEGORY_UNCOND_BR
				|| inst.mnemonic == ZYDIS_MNEMONIC_INT3
				|| inst.mnemonic == ZYDIS_MNEMONIC_UD2;
			if (stops && next > reach)
				return static_cast<size_t>(next - begin);

			p = next;
		}
		return static_cast<size_t>(end - begin);
	}

	std::span<const uint8_t> LeafAt(const uint8_t* gapBegin, const uint8_t* gapEnd, const uint8_t* ptr) {
		ZydisDecoder decoder;
		if (!ZYAN_SUCCESS(ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64)))
			return {};

		for (auto p = gapBegin; p < gapEnd;) {
			p = SkipPadding(decoder, p, gapEnd);
			if (p >= gapEnd || p > ptr)
				return {};

			const auto length = LeafLength(decoder, p, gapEnd);
			if (!length)
				return {};

			if (ptr < p + length)
				return {p, length};

			p += length;
		}
		return {};
	}
}

std::span<const uint8_t> Utils::Win32::LoadedModule::FunctionAt(const void* ptr) const {
	const auto base = reinterpret_cast<const uint8_t*>(m_object);
	const auto fns = xivres::util::span_cast<IMAGE_RUNTIME_FUNCTION_ENTRY>(DataDirectoryAt(IMAGE_DIRECTORY_ENTRY_EXCEPTION));
	const auto va = static_cast<uint32_t>(static_cast<const uint8_t*>(ptr) - base);

	const auto it = std::ranges::upper_bound(fns, va, {}, &IMAGE_RUNTIME_FUNCTION_ENTRY::BeginAddress);
	if (it != fns.begin()) {
		if (const auto& entry = *std::prev(it); entry.BeginAddress <= va && va < entry.EndAddress) {
			const auto& primary = PrimaryFunctionEntry(base, entry);
			return {base + primary.BeginAddress, primary.EndAddress - primary.BeginAddress};
		}
	}

	const auto section = std::ranges::find_if(SectionHeaders(), [va](const IMAGE_SECTION_HEADER& s) {
		return (s.Characteristics & IMAGE_SCN_MEM_EXECUTE) && s.VirtualAddress <= va && va < s.VirtualAddress + s.Misc.VirtualSize;
	});
	if (section == SectionHeaders().end())
		return {};

	auto gapBegin = section->VirtualAddress;
	auto gapEnd = section->VirtualAddress + section->Misc.VirtualSize;
	if (it != fns.begin())
		gapBegin = (std::max)(gapBegin, std::prev(it)->EndAddress);
	if (it != fns.end())
		gapEnd = (std::min)(gapEnd, it->BeginAddress);

	return LeafAt(base + gapBegin, base + gapEnd, static_cast<const uint8_t*>(ptr));
}

std::span<const uint8_t> Utils::Win32::LoadedModule::SectionFrom(const IMAGE_SECTION_HEADER& section) const {
	return std::span(reinterpret_cast<const uint8_t*>(m_object) + section.VirtualAddress, section.Misc.VirtualSize);
}

std::span<const uint8_t> Utils::Win32::LoadedModule::SectionFrom(std::string_view name) const {
	for (const auto& sectionHeader : SectionHeaders()) {
		if (name.size() > sizeof(sectionHeader.Name) || std::memcmp(sectionHeader.Name, name.data(), name.size()) != 0)
			continue;
		if (name.size() < sizeof(sectionHeader.Name) && sectionHeader.Name[name.size()] != '\0')
			continue;

		return SectionFrom(sectionHeader);
	}

	throw std::runtime_error(std::format("LoadedModule::SectionFrom: section \"{}\" not found", name));
}

std::span<const uint8_t> Utils::Win32::LoadedModule::SectionAt(size_t index) const {
	return SectionFrom(SectionHeaders()[index]);
}
