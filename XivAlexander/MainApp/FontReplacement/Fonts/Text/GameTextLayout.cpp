#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Text/GameTextLayout.h"

#include <icu.h>

#include "MainApp/FontReplacement/Fonts/FontServices.h"
#include "MainApp/FontReplacement/Utilities/GameUtf8.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	constexpr uint8_t NewLineMacro = 0x10;
	constexpr uint8_t ItalicMacro = 0x1A;
}

FontReplacement::GameTextLayout::Cluster FontReplacement::GameTextLayout::Cluster::MovedBy(float start, float end) const {
	const auto pen = static_cast<float>(X0) + Origin + start;
	const auto x0 = StdRound(pen);
	auto res = *this;
	res.Key.reset();
	res.Origin = StdRound((pen - x0) * SubpixelSteps) / SubpixelSteps;
	res.X0 = static_cast<int>(x0);
	res.X1 = static_cast<int>(StdRound(static_cast<float>(X1) + end));
	return res;
}

void FontReplacement::GameTextLayout::ActiveRun::Set(const uint8_t* start, int length, int endBytes, const SizedFont* sized, ItalicMode italic, std::shared_ptr<const ShapedRun> shaped) {
	Start = start;
	End = start + length;
	Sized = sized;
	Italic = italic;
	Shaped = std::move(shaped);
	CompareLength = length + endBytes;
	Bytes.assign(start, start + CompareLength);
}

FontReplacement::ItalicMode FontReplacement::GameTextLayout::ActiveRun::ItalicAt(const uint8_t* p) const {
	if (!Shaped->ItalicBytes)
		return Italic;
	return (*Shaped->ItalicBytes)[p - Start] ? ItalicMode::Real : ItalicMode::Upright;
}

bool FontReplacement::GameTextLayout::ActiveRun::Matches() const {
	for (auto i = 0; i < CompareLength; i++) {
		if (Start[i] != Bytes[i])
			return false;
	}
	return true;
}

FontReplacement::GameTextLayout::GameTextLayout(FontServices& services, FontReplacer& replacer, GameGlyph* emptyGlyph)
	: m_replacer(replacer)
	, m_analyzer(services.TextAnalyzer())
	, m_emptyGlyph(emptyGlyph) {
}

FontReplacement::GameTextLayout::~GameTextLayout() = default;

FontReplacement::GameGlyph* FontReplacement::GameTextLayout::TryGetGlyph(SizedFont& sized, const uint8_t* p, ItalicMode italic) {
	for (auto i = m_active.size(); i-- > 0;) {
		const auto& run = *m_active[i];
		if (run.Sized == &sized && p >= run.Start && p < run.End && run.ItalicAt(p) == italic && run.Matches())
			return run.Shaped->Glyphs[p - run.Start];
	}

	// The oldest run is reused when all are taken; it leaves m_active first, so a throw from ShapeFrom leaves no empty slot.
	std::unique_ptr<ActiveRun> run;
	if (m_active.size() == MaxActiveRuns) {
		run = std::move(m_active.front());
		m_active.erase(m_active.begin());
	} else {
		run = std::make_unique<ActiveRun>();
	}

	if (!ShapeFrom(sized, p, italic, *run)) {
		// ShapeFrom leaves the run untouched when it doesn't shape.
		if (run->Shaped)
			m_active.insert(m_active.begin(), std::move(run));
		return nullptr;
	}

	m_active.push_back(std::move(run));
	return m_active.back()->Shaped->Glyphs[0];
}

void FontReplacement::GameTextLayout::Clear() {
	m_active.clear();
	m_runs.clear();
	m_cells.clear();
	m_spacers.clear();
	m_rasters.clear();
}

int FontReplacement::GameTextLayout::NextGrapheme(int t, int end) const {
	if (t >= end)
		return 0;

	thread_local std::unique_ptr<UBreakIterator, decltype(&ubrk_close)> s_iterator(nullptr, &ubrk_close);
	auto status = U_ZERO_ERROR;
	if (!s_iterator) {
		const auto iterator = ubrk_open(UBRK_CHARACTER, "", nullptr, 0, &status);
		if (U_FAILURE(status))
			return 1;
		s_iterator.reset(iterator);
	}

	status = U_ZERO_ERROR;
	ubrk_setText(s_iterator.get(), reinterpret_cast<const UChar*>(m_text.data() + t), end - t, &status);
	if (U_FAILURE(status))
		return 1;
	const auto next = ubrk_following(s_iterator.get(), 0);
	return next == UBRK_DONE || next <= 0 ? end - t : next;
}

bool FontReplacement::GameTextLayout::ShapeFrom(SizedFont& sized, const uint8_t* p, ItalicMode italic, ActiveRun& run) {
	// To the line end, as UTF-16, each code unit's byte offset kept (-1 for a low surrogate), and each character's byte whether it is in italics;
	// a run in a node's italics ends at an italic macro instead.
	auto count = 0;
	auto i = 0;
	auto inItalic = italic != ItalicMode::Upright;
	while (i < MaxRunBytes) {
		const auto b = p[i];
		if (b == 0 || b == 0x0A || b == 0x0D)
			break;

		if (b == GameUtf8::Stx) {
			// A macro: skipped whole; a line break macro ends the run, and so does an italic one in a node's italics.
			const auto macro = p[i + 1];
			if (macro == NewLineMacro || (macro == ItalicMacro && italic == ItalicMode::Images))
				break;
			const auto total = GameUtf8::MacroLength(p + i);
			if (total == 0 || i + total > MaxRunBytes)
				break;

			// <italic>: its argument is a plain integer (1 on, 0 off); anything else ends the run.
			if (macro == ItalicMacro) {
				int payload, n, on, onLength;
				if (!GameUtf8::TryReadInteger(p + i + 2, payload, n) || !GameUtf8::TryReadInteger(p + i + 2 + n, on, onLength) || onLength != payload)
					break;
				inItalic = on != 0;
			}

			i += total;
			continue;
		}

		m_byteItalic[i] = inItalic ? 1 : 0;

		// Stepped as the game does; the run ends before a sequence that would run into the end of the text.
		const auto length = GameUtf8::CharacterLength(p + i);
		if (length == 0 || i + length > MaxRunBytes)
			break;

		// Control characters and broken sequences shape as a replacement character, which keeps them per character.
		char32_t r = U'\xFFFD';
		const auto valid = b >= 0x20 && GameUtf8::TryDecode(p + i, length, r);
		if (!valid)
			r = U'\xFFFD';
		EnsureCapacity(count + 2);
		wchar_t units[2];
		const auto unitCount = xivres::util::unicode::encode(units, r);
		m_text[count] = units[0];
		m_textToByte[count] = valid ? i : -1;
		count++;
		if (unitCount == 2) {
			m_text[count] = units[1];
			m_textToByte[count] = -1;
			count++;
		}

		i += length;
	}

	if (i == 0)
		return false;

	auto hash = 0xCBF29CE484222325ull;
	for (auto j = 0; j < i; j++)
		hash = (hash ^ p[j]) * 0x100000001B3ull;

	// The byte that ended the run is compared too, as what follows the last character could join its cluster.
	const auto ended = i < MaxRunBytes;
	const auto key = std::make_tuple(hash, i, static_cast<const SizedFont*>(&sized), italic);
	auto it = m_runs.find(key);
	if (it == m_runs.end()) {
		auto shaped = std::make_shared<ShapedRun>();
		shaped->Glyphs.resize(i);
		if (italic != ItalicMode::Images)
			shaped->ItalicBytes.emplace(m_byteItalic, m_byteItalic + i);
		if (count != 0)
			Shape(sized, p, count, shaped->Glyphs, italic);
		if (m_runs.size() >= MaxCachedRuns)
			m_runs.clear();
		it = m_runs.emplace(key, std::move(shaped)).first;
	}

	run.Set(p, i, ended ? 1 : 0, &sized, italic, it->second);
	return true;
}

void FontReplacement::GameTextLayout::EnsureCapacity(int n) {
	if (static_cast<size_t>(n) <= m_text.size())
		return;
	const auto size = (std::max)(static_cast<size_t>(n), m_text.size() * 2);
	m_text.resize(size);
	m_textToByte.resize(size);
	m_textSource.resize(size);
	m_textItalic.resize(size);
}
