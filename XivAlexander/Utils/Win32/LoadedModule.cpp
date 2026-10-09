#include "pch.h"
#include "Utils/Win32/LoadedModule.h"

#include <xivres/pe_image.h>

#include "Utils/Win32/Process.h"

Utils::Win32::LoadedModule::LoadedModule(const wchar_t* pwszFileName, DWORD dwFlags, bool bRequire)
	: Closeable(LoadLibraryExW(pwszFileName, nullptr, dwFlags), Null) {
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

Utils::Win32::LoadedModule& Utils::Win32::LoadedModule::operator=(std::nullptr_t) noexcept {
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
	result.resize(GetModuleBaseNameW(GetCurrentProcess(), m_object, result.data(), static_cast<DWORD>(result.size())));
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
	const auto image = xivres::pe_image::from_loaded(base);
	const auto rva = static_cast<uint32_t>(static_cast<const uint8_t*>(ptr) - base);
	if (const auto fn = image.function_containing(rva))
		return {base + fn->BeginAddress, fn->EndAddress - fn->BeginAddress};

	if (const auto gap = image.code_gap_around(rva))
		return LeafAt(base + gap->first, base + gap->second, static_cast<const uint8_t*>(ptr));
	return {};
}

std::span<const uint8_t> Utils::Win32::LoadedModule::SectionFrom(const IMAGE_SECTION_HEADER& section) const {
	return std::span(reinterpret_cast<const uint8_t*>(m_object) + section.VirtualAddress, section.Misc.VirtualSize);
}

std::span<const uint8_t> Utils::Win32::LoadedModule::SectionFrom(std::string_view name) const {
	const auto image = xivres::pe_image::from_loaded(m_object);
	if (const auto section = image.find_section(name))
		return image.section_data(*section);
	throw std::runtime_error(std::format("LoadedModule::SectionFrom: section \"{}\" not found", name));
}

std::span<const uint8_t> Utils::Win32::LoadedModule::SectionAt(size_t index) const {
	return SectionFrom(SectionHeaders()[index]);
}
