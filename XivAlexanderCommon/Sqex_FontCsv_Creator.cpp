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

void Sqex::FontCsv::Creator::AddCharacter(char32_t codePoint, std::shared_ptr<const SeCompatibleDrawableFont<uint8_t>> font, bool replace) {
	if (!font->HasCharacter(codePoint))
		return;

	auto plan = Implementation::CharacterPlan{
		.Character = codePoint,
		.Font = std::move(font),
	};

	if (replace)
		m_pImpl->m_characters.insert_or_assign(codePoint, std::move(plan));
	else
		m_pImpl->m_characters.emplace(codePoint, std::move(plan));
}

void Sqex::FontCsv::Creator::AddCharacter(const std::shared_ptr<const SeCompatibleDrawableFont<uint8_t>>&font, bool replace) {
	for (const auto c : font->GetAllCharacters())
		AddCharacter(c, font);
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

Sqex::FontCsv::Creator::RenderTarget::AllocatedSpace Sqex::FontCsv::Creator::RenderTarget::Draw(char32_t c, const SeCompatibleDrawableFont<uint8_t>*font, SSIZE_T drawOffsetX, SSIZE_T drawOffsetY, uint16_t boundingWidth, uint16_t boundingHeight) {
	AllocatedSpace space;
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
				m_currentY += m_currentLineHeight + m_glyphGap;
				m_currentLineHeight = 0;
			}
			if (m_currentY + boundingHeight + m_glyphGap >= m_textureHeight)
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
			.Index = static_cast<uint16_t>(m_mipmaps.size() - 1),
			.X = m_currentX,
			.Y = m_currentY,
			.Mipmap = m_mipmaps.back().get(),
		};

		m_currentX += boundingWidth + m_glyphGap;
		m_currentLineHeight = std::max<uint16_t>(m_currentLineHeight, boundingHeight);
	}
	
	font->Draw(space.Mipmap, space.X + drawOffsetX, space.Y + drawOffsetY, c, 0xFF, 0x00);

	return space;
}


std::shared_ptr<Sqex::FontCsv::ModifiableFontCsvStream> Sqex::FontCsv::Creator::Compile(RenderTarget & renderTarget) const {
	auto result = std::make_shared<ModifiableFontCsvStream>();
	result->TextureWidth(renderTarget.TextureWidth());
	result->TextureHeight(renderTarget.TextureHeight());
	result->Points(SizePoints);
	result->Ascent(AscentPixels);
	result->LineHeight(AscentPixels + DescentPixels);

	SSIZE_T globalOffsetX = 0;
	SSIZE_T globalOffsetY = 0;
	
	Utils::Win32::TpEnvironment tpEnv;
	for (auto& plan : m_pImpl->m_characters | std::views::values)
		tpEnv.SubmitWork([&plan]() { void(plan.GetBbox()); });
	tpEnv.WaitOutstanding();

	for (const auto& plan : m_pImpl->m_characters | std::views::values) {
		const auto& bbox = plan.GetBbox();
		globalOffsetX = std::max<SSIZE_T>(globalOffsetX, -static_cast<int>(bbox.left));
		globalOffsetY = std::max<SSIZE_T>(globalOffsetY, -static_cast<int>(bbox.top));
	}
	globalOffsetX = std::min<SSIZE_T>(globalOffsetX, MaxGlobalOffsetX);

	result->ReserveStorage(m_pImpl->m_characters.size(), m_pImpl->m_kernings.size());
	for (auto& plan : m_pImpl->m_characters | std::views::values) {
		tpEnv.SubmitWork([&]() {
			const auto& bbox = plan.GetBbox();
			if (bbox.empty)
				return;
			
			const auto boundingWidth = static_cast<uint8_t>(bbox.right + globalOffsetX);
			const auto boundingHeight = static_cast<uint8_t>(std::max({
				bbox.bottom - bbox.top,
				static_cast<SSIZE_T>(result->LineHeight()),
				static_cast<SSIZE_T>(plan.Font->Height()),
				}) + globalOffsetY);
			const auto nextOffsetX = static_cast<int8_t>(bbox.offsetX - globalOffsetX);
			const auto currentOffsetY = static_cast<int8_t>(
				(AlignToBaseline ? result->Ascent() - plan.Font->Ascent() : (0LL + result->LineHeight() - plan.Font->Height()) / 2)
				- globalOffsetY
				+ GlobalOffsetYModifier
				);
			const auto space = renderTarget.Draw(plan.Character, plan.Font.get(), 0, 0, boundingWidth, boundingHeight);
			result->AddFontEntry(plan.Character, space.Index, space.X, space.Y, boundingWidth, boundingHeight, nextOffsetX, currentOffsetY);
		});
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

	tpEnv.WaitOutstanding();
	return result;
}
