#include "pch.h"
#include "Sqex_FontCsv_Creator.h"
#include "Utils_Win32_ThreadPool.h"

struct Sqex::FontCsv::Creator::Implementation {
	struct CharacterPlan {
		mutable GlyphMeasurement m_bbox;

		char32_t Character;
		std::shared_ptr<const SeCompatibleDrawableFont<uint8_t>> Font;

		const GlyphMeasurement& GetBbox() const {
			if (m_bbox.empty)
				m_bbox = Font->Measure(0, 0, Character);
			return m_bbox;
		}
	};
	std::map<char32_t, CharacterPlan> m_characters;
	std::map<std::tuple<const SeCompatibleDrawableFont<uint8_t>*, char32_t, char32_t>, int> m_kernings;
};

Sqex::FontCsv::Creator::Creator()
	: m_pImpl(std::make_unique<Implementation>()) {
}

Sqex::FontCsv::Creator::~Creator() = default;

void Sqex::FontCsv::Creator::AddCharacter(char32_t codePoint, std::shared_ptr<const SeCompatibleDrawableFont<uint8_t>> font, bool replace, bool extendRange) {
	if (!font->HasCharacter(codePoint))
		return;

	auto plan = Implementation::CharacterPlan{
		.Character = codePoint,
		.Font = std::move(font),
	};

	if (!extendRange) {
		if (m_pImpl->m_characters.find(codePoint) == m_pImpl->m_characters.end())
			return;
	}

	if (replace)
		m_pImpl->m_characters.insert_or_assign(codePoint, std::move(plan));
	else
		m_pImpl->m_characters.emplace(codePoint, std::move(plan));
}

void Sqex::FontCsv::Creator::AddCharacter(const std::shared_ptr<const SeCompatibleDrawableFont<uint8_t>>&font, bool replace, bool extendRange) {
	for (const auto c : font->GetAllCharacters())
		AddCharacter(c, font, replace, extendRange);
}

void Sqex::FontCsv::Creator::AddKerning(const std::shared_ptr<const SeCompatibleDrawableFont<uint8_t>>&font, char32_t left, char32_t right, int distance, bool replace) {
	if (m_pImpl->m_characters.find(left) == m_pImpl->m_characters.end())
		return;
	if (m_pImpl->m_characters.find(right) == m_pImpl->m_characters.end())
		return;

	if (replace)
		m_pImpl->m_kernings.insert_or_assign(std::make_tuple(font.get(), left, right), distance);
	else
		m_pImpl->m_kernings.emplace(std::make_tuple(font.get(), left, right), distance);
}

void Sqex::FontCsv::Creator::AddKerning(const std::shared_ptr<const SeCompatibleDrawableFont<uint8_t>>&font, bool replace) {
	for (const auto& [pair, distance] : font->GetKerningTable())
		AddKerning(font, pair.first, pair.second, static_cast<int>(distance), replace);
}

void Sqex::FontCsv::Creator::AddFont(const std::shared_ptr<const SeCompatibleDrawableFont<uint8_t>>& font, bool replace, bool extendRange) {
	for (const auto c : font->GetAllCharacters())
		AddCharacter(c, font, replace, extendRange);
	for (const auto& [pair, distance] : font->GetKerningTable())
		AddKerning(font, pair.first, pair.second, static_cast<int>(distance), replace);
}

Sqex::FontCsv::Creator::RenderTarget::RenderTarget(uint16_t textureWidth, uint16_t textureHeight, uint16_t glyphGap)
	: m_textureWidth(textureWidth)
	, m_textureHeight(textureHeight)
	, m_glyphGap(glyphGap)
	, m_currentX(glyphGap)
	, m_currentY(glyphGap)
	, m_currentLineHeight(0) {
}

Sqex::FontCsv::Creator::RenderTarget::~RenderTarget() = default;

void Sqex::FontCsv::Creator::RenderTarget::Finalize() {
	auto mipmaps = std::move(m_mipmaps);
	while (mipmaps.size() % 4)
		mipmaps.push_back(std::make_shared<Texture::MemoryBackedMipmap>(
			mipmaps[0]->Width(), mipmaps[0]->Height(), Texture::CompressionType::L8_1,
			std::vector<uint8_t>(static_cast<size_t>(mipmaps[0]->Width()) * mipmaps[0]->Height())));

	for (size_t i = 0; i < mipmaps.size() / 4; ++i) {
		m_mipmaps.push_back(std::make_shared<Texture::MemoryBackedMipmap>(
			mipmaps[0]->Width(), mipmaps[0]->Height(), Texture::CompressionType::RGBA4444,
			std::vector<uint8_t>(sizeof Texture::RGBA4444 * mipmaps[0]->Width() * mipmaps[0]->Height())));

		const auto target = m_mipmaps.back()->View<Texture::RGBA4444>();
		const auto b = mipmaps[i * 4 + 0]->View<uint8_t>();
		const auto g = mipmaps[i * 4 + 1]->View<uint8_t>();
		const auto r = mipmaps[i * 4 + 2]->View<uint8_t>();
		const auto a = mipmaps[i * 4 + 3]->View<uint8_t>();
		for (size_t j = 0; j < target.size(); ++j)
			target[j].SetFrom(r[j] >> 4, g[j] >> 4, b[j] >> 4, a[j] >> 4);
	}
}

std::vector<std::shared_ptr<const Sqex::Texture::MipmapStream>> Sqex::FontCsv::Creator::RenderTarget::AsMipmapStreamVector() const {
	std::vector<std::shared_ptr<const Texture::MipmapStream>> res;
	for (const auto& i : m_mipmaps)
		res.emplace_back(i);
	return res;
}

std::vector<std::shared_ptr<Sqex::Texture::ModifiableTextureStream>> Sqex::FontCsv::Creator::RenderTarget::AsTextureStreamVector() const {
	std::vector<std::shared_ptr<Texture::ModifiableTextureStream>> res;
	for (const auto& i : m_mipmaps) {
		auto texture = std::make_shared<Texture::ModifiableTextureStream>(i->Type(), i->Width(), i->Height());
		texture->AppendMipmap(i);
		res.emplace_back(std::move(texture));
	}
	return res;
}

Sqex::FontCsv::Creator::RenderTarget::AllocatedSpace Sqex::FontCsv::Creator::RenderTarget::Draw(char32_t c, const SeCompatibleDrawableFont<uint8_t>*font, SSIZE_T drawOffsetX, SSIZE_T drawOffsetY, uint8_t boundingWidth, uint8_t boundingHeight) {
	AllocatedSpace space;
	decltype(m_mipmaps.back().get()) mipmap;
	{
		const auto lock = std::lock_guard(m_mtx);
		const auto [it, isNewEntry] = m_drawnGlyphs.emplace(std::make_tuple(c, font), AllocatedSpace{});
		if (!isNewEntry)
			return it->second;

		auto newTargetRequired = false;
		if (m_mipmaps.empty())
			newTargetRequired = true;
		else {
			if (static_cast<size_t>(0) + m_currentX + boundingWidth + m_glyphGap >= m_textureWidth) {
				m_currentX = m_glyphGap;
				m_currentY += m_currentLineHeight + m_glyphGap + 1;  // Account for rounding errors
				m_currentLineHeight = 0;
			}
			if (m_currentY + boundingHeight + m_glyphGap + 1 >= m_textureHeight)
				newTargetRequired = true;
		}
		if (newTargetRequired) {
			m_mipmaps.emplace_back(std::make_shared<Texture::MemoryBackedMipmap>(
				m_textureWidth, m_textureHeight,
				Texture::CompressionType::L8_1,
				std::vector<uint8_t>(static_cast<size_t>(m_textureWidth) * m_textureHeight)));
			m_currentX = m_currentY = m_glyphGap;
			m_currentLineHeight = 0;
		}

		space = it->second = AllocatedSpace{
			.drawOffsetX = drawOffsetX,
			.drawOffsetY = drawOffsetY,
			.Index = static_cast<uint16_t>(m_mipmaps.size() - 1),
			.X = m_currentX,
			.Y = m_currentY,
			.BoundingHeight = boundingHeight,
		};
		mipmap = m_mipmaps.back().get();

		m_currentX += boundingWidth + m_glyphGap;
		m_currentLineHeight = std::max<uint16_t>(m_currentLineHeight, boundingHeight);
	}

	font->Draw(mipmap, space.X + drawOffsetX, space.Y + drawOffsetY, c, 0xFF, 0x00);
	std::cout << ToU8(std::vector<char32_t>{c, 0}.data());

	return space;
}


std::shared_ptr<Sqex::FontCsv::ModifiableFontCsvStream> Sqex::FontCsv::Creator::Compile(RenderTarget & renderTarget) const {
	auto result = std::make_shared<ModifiableFontCsvStream>();
	result->TextureWidth(renderTarget.TextureWidth());
	result->TextureHeight(renderTarget.TextureHeight());
	result->Points(SizePoints);

	std::vector<Implementation::CharacterPlan> planList;
	planList.reserve(m_pImpl->m_characters.size());
	std::transform(m_pImpl->m_characters.begin(), m_pImpl->m_characters.end(), std::back_inserter(planList), [](const auto& k) { return k.second; });

	GlyphMeasurement maxBbox;
	uint32_t maxAscent = 0, maxDescent = 0;
	{
		Utils::Win32::TpEnvironment tpEnv;
		std::vector<GlyphMeasurement> maxBboxes(tpEnv.ThreadCount());
		std::vector<uint32_t> maxAscents(tpEnv.ThreadCount());
		std::vector<uint32_t> maxDescents(tpEnv.ThreadCount());
		for (size_t i = 0; i < tpEnv.ThreadCount(); ++i) {
			tpEnv.SubmitWork([this, &planList, &maxBboxes, &maxAscents, &maxDescents, startI = i]() {
				auto& box = maxBboxes[startI];
				auto& ascent = maxAscents[startI];
				auto& descent = maxDescents[startI];
				for (auto i = startI; i < planList.size(); i += maxBboxes.size()) {
					box.ExpandToFit(planList[i].GetBbox());
					ascent = std::max(ascent, planList[i].Font->Ascent());
					descent = std::max(descent, planList[i].Font->Descent());
				}
			});
		}
		tpEnv.WaitOutstanding();
		for (const auto& bbox : maxBboxes)
			maxBbox.ExpandToFit(bbox);
		for (const auto& n : maxAscents)
			maxAscent = std::max(n, maxAscent);
		for (const auto& n : maxDescents)
			maxDescent = std::max(n, maxDescent);
	}
	const auto globalOffsetX = std::max<SSIZE_T>(MinGlobalOffsetX, std::min<SSIZE_T>(MaxGlobalOffsetX, std::max<SSIZE_T>(0, -maxBbox.left)));
	const auto boundingHeight = static_cast<uint8_t>(maxBbox.Height());

	if (AscentPixels == AutoAscentDescent)
		result->Ascent(maxAscent);
	else
		result->Ascent(AscentPixels);

	if (DescentPixels == AutoAscentDescent)
		result->LineHeight(result->Ascent() + maxDescent);
	else
		result->LineHeight(result->Ascent() + DescentPixels);

	result->ReserveStorage(m_pImpl->m_characters.size(), m_pImpl->m_kernings.size());

	{
		Utils::Win32::TpEnvironment tpEnv;
		for (auto& plan : m_pImpl->m_characters | std::views::values) {
			tpEnv.SubmitWork([&]() {
				const auto& bbox = plan.GetBbox();
				if (bbox.empty)
					return;

				const auto leftExtension = std::max<SSIZE_T>(-bbox.left, globalOffsetX);
				const auto boundingWidth = static_cast<uint8_t>(leftExtension + bbox.right);
				const auto nextOffsetX = static_cast<int8_t>(bbox.advanceX - bbox.right - globalOffsetX);
				const auto currentOffsetY = static_cast<int8_t>(
					(AlignToBaseline ? result->Ascent() - plan.Font->Ascent() : (0LL + result->LineHeight() - plan.Font->Height()) / 2)
					+ GlobalOffsetYModifier
					);
				const auto space = renderTarget.Draw(plan.Character, plan.Font.get(), leftExtension, 0, boundingWidth, boundingHeight);

				const auto resultingX = static_cast<uint16_t>(space.X - space.drawOffsetX + leftExtension);
				const auto resultingY = static_cast<uint16_t>(space.Y - space.drawOffsetY);
				result->AddFontEntry(plan.Character, space.Index, resultingX, resultingY, boundingWidth, std::min(space.BoundingHeight, boundingHeight), nextOffsetX, currentOffsetY);
			});
		}
		tpEnv.WaitOutstanding();
	}

	for (const auto& [pair, distance] : m_pImpl->m_kernings) {
		const auto [font, c1, c2] = pair;
		const auto f1 = m_pImpl->m_characters.at(c1).Font.get();
		const auto f2 = m_pImpl->m_characters.at(c2).Font.get();
		if (f1 != font && f2 != font)
			continue;
		if (f1 != f2
			&& AlwaysApplyKerningCharacters.find(c1) == AlwaysApplyKerningCharacters.end()
			&& AlwaysApplyKerningCharacters.find(c2) == AlwaysApplyKerningCharacters.end())
			continue;
		if (!distance)
			continue;
		result->AddKerning(c1, c2, distance);
	}

	return result;
}
