#include "pch.h"
#include "Sqex_FontCsv_Creator.h"

struct Sqex::FontCsv::Creator::Implementation {
	struct CharacterPlan {
		mutable GlyphMeasurement m_bbox{};

		char32_t Character;
		std::shared_ptr<SeCompatibleDrawableFont<uint8_t>> Font;

		const GlyphMeasurement& GetBbox() const {
			if (m_bbox.empty)
				m_bbox = Font->GetBoundingBox(Character, 0, 0);
			return m_bbox;
		}
	};
	std::map<char32_t, CharacterPlan> m_characters;
	std::map<std::pair<char32_t, char32_t>, int> m_kernings;
};

Sqex::FontCsv::Creator::Creator()
	: m_pImpl(std::make_unique<Implementation>()) {
}

Sqex::FontCsv::Creator::~Creator() = default;

void Sqex::FontCsv::Creator::AddCharacter(char32_t codePoint, std::shared_ptr<SeCompatibleDrawableFont<uint8_t>> font, bool replace) {
	auto plan = Implementation::CharacterPlan{
		.Character = codePoint,
		.Font = std::move(font),
	};

	if (replace)
		m_pImpl->m_characters.insert_or_assign(codePoint, std::move(plan));
	else
		m_pImpl->m_characters.emplace(codePoint, std::move(plan));
}

void Sqex::FontCsv::Creator::AddCharacter(const std::shared_ptr<SeCompatibleDrawableFont<uint8_t>>& font, bool replace) {
	for (const auto c : font->GetAllCharacters())
		AddCharacter(c, font);
}

void Sqex::FontCsv::Creator::AddKerning(char32_t left, char32_t right, int distance, bool replace) {
	if (m_pImpl->m_characters.find(left) == m_pImpl->m_characters.end())
		return;
	if (m_pImpl->m_characters.find(right) == m_pImpl->m_characters.end())
		return;

	if (replace)
		m_pImpl->m_kernings.insert_or_assign(std::make_pair(left, right), distance);
	else
		m_pImpl->m_kernings.emplace(std::make_pair(left, right), distance);
}

void Sqex::FontCsv::Creator::AddKerning(const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& table, bool replace) {
	for (const auto& [pair ,distance] : table)
		AddKerning(pair.first, pair.second, static_cast<int>(distance), replace);
}

Sqex::FontCsv::Creator::RenderTarget::RenderTarget(int textureWidth, int textureHeight)
	: m_textureWidth(textureWidth)
	, m_textureHeight(textureHeight) {
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
	for (const auto i : m_mipmaps)
		res.emplace_back(i);
	return res;
}

std::shared_ptr<Sqex::FontCsv::ModifiableFontCsvStream> Sqex::FontCsv::Creator::Compile(RenderTarget& renderTargets) const {
	auto result = std::make_shared<ModifiableFontCsvStream>();
	result->TextureWidth(static_cast<uint16_t>(renderTargets.m_textureWidth));
	result->TextureHeight(static_cast<uint16_t>(renderTargets.m_textureHeight));
	result->Points(Points);
	result->Ascent(Ascent);
	result->LineHeight(Ascent + Descent);

	SSIZE_T globalLeftOffset = 0;
	SSIZE_T minShiftY = 0;
	for (const auto& plan : m_pImpl->m_characters | std::views::values) {
		const auto& bbox = plan.GetBbox();
		globalLeftOffset = std::max<SSIZE_T>( globalLeftOffset, -static_cast<int>(bbox.left));
		minShiftY = std::max<SSIZE_T>(minShiftY, -static_cast<int>(bbox.top));
	}
	globalLeftOffset = std::min<SSIZE_T>(globalLeftOffset, MaxLeftOffset);

	for (const auto& plan : m_pImpl->m_characters | std::views::values) {
		const auto& bbox = plan.GetBbox();
		if (bbox.empty)
			continue;

		const auto boundingWidth = static_cast<uint8_t>(bbox.right - bbox.left + globalLeftOffset);
		const auto boundingHeight = static_cast<uint8_t>(std::max(result->LineHeight(), plan.Font->Height()) + minShiftY);
		const auto nextOffsetX = static_cast<uint8_t>(bbox.offsetX);
		const auto currentOffsetY = static_cast<uint8_t>((result->LineHeight() - plan.Font->Height()) / 2 - minShiftY + GlobalOffsetYModifier);

		auto newTargetRequired = false;
		if (renderTargets.m_mipmaps.empty())
			newTargetRequired = true;
		else {
			if (renderTargets.m_currentX + boundingWidth + GlyphGap >= renderTargets.m_textureWidth) {
				renderTargets.m_currentX = GlyphGap;
				renderTargets.m_currentY += renderTargets.m_currentLineHeight + GlyphGap;
				renderTargets.m_currentLineHeight = 0;
			}
			if (renderTargets.m_currentY + boundingHeight + GlyphGap >= renderTargets.m_textureHeight)
				newTargetRequired = true;
		}
		if (newTargetRequired) {
			renderTargets.m_mipmaps.emplace_back(std::make_shared<Texture::MemoryBackedMipmap>(renderTargets.m_textureWidth, renderTargets.m_textureHeight, Texture::CompressionType::L8_1, std::vector<uint8_t>(renderTargets.m_textureWidth * renderTargets.m_textureHeight)));
			renderTargets.m_currentX = renderTargets.m_currentY = GlyphGap;
			renderTargets.m_currentLineHeight = 0;
		}

		const auto textureIndex = static_cast<uint16_t>(renderTargets.m_mipmaps.size() - 1);
		const auto textureOffsetX = renderTargets.m_currentX;
		const auto textureOffsetY = renderTargets.m_currentY;

		// ReSharper disable once CppExpressionWithoutSideEffects
		plan.Font->Draw(renderTargets.m_mipmaps.back().get(), textureOffsetX + globalLeftOffset, textureOffsetY - minShiftY, plan.Character, 0xFF, 0x00);

		result->AddFontEntry(plan.Character, textureIndex, textureOffsetX, textureOffsetY, boundingWidth, boundingHeight, nextOffsetX, currentOffsetY);

		renderTargets.m_currentX += boundingWidth + GlyphGap;
		renderTargets.m_currentLineHeight = std::max<uint16_t>(renderTargets.m_currentLineHeight, boundingHeight);
	}

	for (const auto& [pair, distance] : m_pImpl->m_kernings) {
		const auto f1 = m_pImpl->m_characters.at(pair.first).Font.get();
		const auto f2 = m_pImpl->m_characters.at(pair.second).Font.get();
		if (f1 != f2
			&& AlwaysApplyKerningCharacters.find(pair.first) == AlwaysApplyKerningCharacters.end()
			&& AlwaysApplyKerningCharacters.find(pair.second) == AlwaysApplyKerningCharacters.end())
			continue;
		if (!distance)
			continue;
		result->AddKerning(pair.first, pair.second, distance);
	}
	return result;
}
