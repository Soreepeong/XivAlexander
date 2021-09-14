#include "pch.h"
#include "Sqex_FontCsv_Creator.h"

#include "Sqex_FontCsv_DirectWriteFont.h"
#include "Sqex_FontCsv_FreeTypeFont.h"
#include "Sqex_FontCsv_GdiFont.h"
#include "Sqex_Sqpack_EntryRawStream.h"
#include "Sqex_Sqpack_Reader.h"
#include "Utils_Win32_ThreadPool.h"

struct Sqex::FontCsv::FontCsvCreator::Implementation {
	Win32::TpEnvironment WorkPool;
	bool Cancelled = false;
	Win32::Semaphore CoreLimitSemaphore;

	struct CharacterPlan {
		mutable GlyphMeasurement m_bbox;

		char32_t Character;
		const SeCompatibleDrawableFont<uint8_t>* Font;

		const GlyphMeasurement& GetBbox() const {
			if (m_bbox.empty)
				m_bbox = Font->Measure(0, 0, Character);
			return m_bbox;
		}
	};

	std::map<char32_t, CharacterPlan> CharacterPlans;
	std::map<std::tuple<const SeCompatibleDrawableFont<uint8_t>*, char32_t, char32_t>, int> Kernings;

	FontCreationProgress Progress{
		.Indeterminate = 1,
	};

	CallOnDestruction WaitSemaphore() {
		if (!CoreLimitSemaphore)
			return {};
		if (CoreLimitSemaphore.Wait(INFINITE) != WAIT_OBJECT_0)
			throw std::runtime_error("wait != WAIT_OBJECT_0");
		return CallOnDestruction([this]() {
			void(CoreLimitSemaphore.Release(1));
		});
	}
};

Sqex::FontCsv::FontCsvCreator::FontCsvCreator(const Win32::Semaphore& semaphore)
	: m_pImpl(std::make_unique<Implementation>()) {
	m_pImpl->CoreLimitSemaphore = semaphore;
}

Sqex::FontCsv::FontCsvCreator::~FontCsvCreator() = default;

void Sqex::FontCsv::FontCsvCreator::AddCharacter(char32_t codePoint, const SeCompatibleDrawableFont<uint8_t>* font, bool replace, bool extendRange) {
	if (!font->HasCharacter(codePoint))
		return;

	auto plan = Implementation::CharacterPlan{
		.Character = codePoint,
		.Font = font,
	};

	if (!extendRange) {
		if (m_pImpl->CharacterPlans.find(codePoint) == m_pImpl->CharacterPlans.end())
			return;
	}

	if (replace)
		m_pImpl->CharacterPlans.insert_or_assign(codePoint, plan);
	else
		m_pImpl->CharacterPlans.emplace(codePoint, plan);
}

void Sqex::FontCsv::FontCsvCreator::AddCharacter(const SeCompatibleDrawableFont<uint8_t>*font, bool replace, bool extendRange) {
	for (const auto c : font->GetAllCharacters())
		AddCharacter(c, font, replace, extendRange);
}

void Sqex::FontCsv::FontCsvCreator::AddKerning(const  SeCompatibleDrawableFont<uint8_t>*font, char32_t left, char32_t right, int distance, bool replace) {
	if (m_pImpl->CharacterPlans.find(left) == m_pImpl->CharacterPlans.end())
		return;
	if (m_pImpl->CharacterPlans.find(right) == m_pImpl->CharacterPlans.end())
		return;

	if (replace)
		m_pImpl->Kernings.insert_or_assign(std::make_tuple(font, left, right), distance);
	else
		m_pImpl->Kernings.emplace(std::make_tuple(font, left, right), distance);
}

void Sqex::FontCsv::FontCsvCreator::AddKerning(const  SeCompatibleDrawableFont<uint8_t>*font, bool replace) {
	for (const auto& [pair, distance] : font->GetKerningTable())
		AddKerning(font, pair.first, pair.second, static_cast<int>(distance), replace);
}

void Sqex::FontCsv::FontCsvCreator::AddFont(const  SeCompatibleDrawableFont<uint8_t>*font, bool replace, bool extendRange) {
	for (const auto c : font->GetAllCharacters())
		AddCharacter(c, font, replace, extendRange);
	for (const auto& [pair, distance] : font->GetKerningTable())
		AddKerning(font, pair.first, pair.second, static_cast<int>(distance), replace);
}

const Sqex::FontCsv::FontCreationProgress& Sqex::FontCsv::FontCsvCreator::GetProgress() const {
	return m_pImpl->Progress;
}

void Sqex::FontCsv::FontCsvCreator::Cancel() {
	m_pImpl->Cancelled = true;
	m_pImpl->WorkPool.Cancel();
}


Sqex::FontCsv::FontCsvCreator::RenderTarget::RenderTarget(uint16_t textureWidth, uint16_t textureHeight, uint16_t glyphGap)
	: m_textureWidth(textureWidth)
	, m_textureHeight(textureHeight)
	, m_glyphGap(glyphGap)
	, m_currentX(glyphGap)
	, m_currentY(glyphGap)
	, m_currentLineHeight(0) {
}

Sqex::FontCsv::FontCsvCreator::RenderTarget::~RenderTarget() = default;

std::vector<std::shared_ptr<const Sqex::Texture::MipmapStream>> Sqex::FontCsv::FontCsvCreator::RenderTarget::AsMipmapStreamVector() const {
	std::vector<std::shared_ptr<const Texture::MipmapStream>> res;
	for (const auto& i : m_mipmaps)
		res.emplace_back(i);
	return res;
}

std::vector<std::shared_ptr<Sqex::Texture::ModifiableTextureStream>> Sqex::FontCsv::FontCsvCreator::RenderTarget::AsTextureStreamVector() const {
	std::vector<std::shared_ptr<Texture::ModifiableTextureStream>> res;
	for (const auto& i : m_mipmaps) {
		auto texture = std::make_shared<Texture::ModifiableTextureStream>(i->Type(), i->Width(), i->Height());
		texture->AppendMipmap(i);
		res.emplace_back(std::move(texture));
	}
	return res;
}

std::pair<Sqex::FontCsv::FontCsvCreator::RenderTarget::AllocatedSpace, bool> Sqex::FontCsv::FontCsvCreator::RenderTarget::AllocateSpace(char32_t c, const SeCompatibleDrawableFont<uint8_t>* font, SSIZE_T drawOffsetX, SSIZE_T drawOffsetY, uint8_t boundingWidth, uint8_t boundingHeight, uint8_t borderThickness, uint8_t borderOpacity) {
	const auto actualGlyphGap = static_cast<uint16_t>(m_glyphGap + borderThickness);
	const auto lock = std::lock_guard(m_mtx);

	const auto [it, isNewEntry] = m_drawnGlyphs.emplace(std::make_tuple(c, font, borderThickness, borderOpacity), AllocatedSpace{});
	if (isNewEntry) {
		auto newTargetRequired = false;
		if (m_mipmaps.empty())
			newTargetRequired = true;
		else {
			if (static_cast<size_t>(0) + m_currentX + boundingWidth + actualGlyphGap >= m_textureWidth) {
				m_currentX = actualGlyphGap;
				m_currentY += m_currentLineHeight + actualGlyphGap + 1;  // Account for rounding errors
				m_currentLineHeight = 0;
			}
			if (m_currentY + boundingHeight + actualGlyphGap + 1 >= m_textureHeight)
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

		if (m_currentX < actualGlyphGap)
			m_currentX = actualGlyphGap;
		if (m_currentY < actualGlyphGap)
			m_currentY = actualGlyphGap;

		it->second = AllocatedSpace{
			.drawOffsetX = drawOffsetX,
			.drawOffsetY = drawOffsetY,
			.Index = static_cast<uint16_t>(m_mipmaps.size() - 1),
			.X = m_currentX,
			.Y = m_currentY,
			.BoundingHeight = boundingHeight,
		};

		m_currentX += boundingWidth + m_glyphGap;
		m_currentLineHeight = std::max<uint16_t>(m_currentLineHeight, boundingHeight);
	}

	return std::make_pair(it->second, isNewEntry);
}

Sqex::FontCsv::FontCsvCreator::RenderTarget::AllocatedSpace Sqex::FontCsv::FontCsvCreator::RenderTarget::Draw(
	char32_t c,
	const SeCompatibleDrawableFont<uint8_t>* font,
	SSIZE_T drawOffsetX, SSIZE_T drawOffsetY,
	uint8_t boundingWidth, uint8_t boundingHeight,
	uint8_t borderThickness, uint8_t borderOpacity
) {
	const auto [space, drawRequired] = AllocateSpace(c, font, drawOffsetX, drawOffsetY, boundingWidth, boundingHeight, borderThickness, borderOpacity);

	if (drawRequired) {
		const auto mipmap = m_mipmaps[space.Index].get();

		if (borderThickness) {
			for (auto i = 0; i <= 2 * borderThickness; ++i)
				for (auto j = 0; j <= 2 * borderThickness; ++j)
					font->Draw(mipmap, space.X + drawOffsetX + i, space.Y + drawOffsetY + j, c, borderOpacity, 0, 0xFF, 0);
			font->Draw(mipmap, space.X + drawOffsetX + borderThickness, space.Y + drawOffsetY + borderThickness, c, 0xFF, 0, 0xFF, 0);
		} else
			font->Draw(mipmap, space.X + drawOffsetX, space.Y + drawOffsetY, c, 0xFF, 0x00);
	}

	return space;
}


std::shared_ptr<Sqex::FontCsv::ModifiableFontCsvStream> Sqex::FontCsv::FontCsvCreator::Compile(RenderTarget & renderTarget) {
	// arbitary numbers
	constexpr static auto ProgressWeight_Bbox = 5;
	constexpr static auto ProgressWeight_Draw = 30;
	constexpr static auto ProgressWeight_Kerning = 1;

	const uint8_t borderThickness = BorderOpacity ? BorderThickness : 0;
	const uint8_t borderOpacity = borderThickness ? BorderOpacity : 0;

	try {
		auto result = std::make_shared<ModifiableFontCsvStream>();
		result->TextureWidth(renderTarget.TextureWidth());
		result->TextureHeight(renderTarget.TextureHeight());
		result->Points(SizePoints);

		std::vector<Implementation::CharacterPlan> planList;
		planList.reserve(m_pImpl->CharacterPlans.size());
		std::ranges::transform(m_pImpl->CharacterPlans,
			std::back_inserter(planList),
			[](const auto& k) { return k.second; });

		GlyphMeasurement maxBbox;
		uint32_t maxAscent = 0, maxLineHeight = 0;

		m_pImpl->Progress.Max = planList.size() * (ProgressWeight_Bbox + ProgressWeight_Draw) + m_pImpl->Kernings.size() * ProgressWeight_Kerning;
		m_pImpl->Progress.Indeterminate = 0;

		{
			std::vector<GlyphMeasurement> maxBboxes(m_pImpl->WorkPool.ThreadCount());
			std::vector<uint32_t> maxAscents(m_pImpl->WorkPool.ThreadCount());
			std::vector<uint32_t> maxLineHeights(m_pImpl->WorkPool.ThreadCount());
			for (size_t i = 0; i < m_pImpl->WorkPool.ThreadCount(); ++i) {
				m_pImpl->WorkPool.SubmitWork([this, &planList, &maxBboxes, &maxAscents, &maxLineHeights, startI = i]() {
					try {
						const auto waitRelease = m_pImpl->WaitSemaphore();
						auto& box = maxBboxes[startI];
						auto& ascent = maxAscents[startI];
						auto& lineHeight = maxLineHeights[startI];
						for (auto i = startI; i < planList.size(); i += maxBboxes.size()) {
							if (m_pImpl->Cancelled)
								return;

							m_pImpl->Progress.Progress += ProgressWeight_Bbox;

							box.ExpandToFit(planList[i].GetBbox());
							ascent = std::max(ascent, planList[i].Font->Ascent());
							lineHeight = std::max(lineHeight, planList[i].Font->LineHeight());
						}
					} catch (const std::exception& e) {
						OnError(e);
#ifdef _DEBUG
						throw;
#endif
					}
				});
			}
			m_pImpl->WorkPool.WaitOutstanding();
			if (m_pImpl->Cancelled)
				return nullptr;

			for (const auto& bbox : maxBboxes)
				maxBbox.ExpandToFit(bbox);
			for (const auto& n : maxAscents)
				maxAscent = std::max(n, maxAscent);
			for (const auto& n : maxLineHeights)
				maxLineHeight = std::max(n, maxLineHeight);
		}
		const auto globalOffsetX = std::max<SSIZE_T>(MinGlobalOffsetX,
			std::min<SSIZE_T>(MaxGlobalOffsetX,
				std::max<SSIZE_T>(0, -maxBbox.left)));
		const auto globalOffsetY = GlobalOffsetYModifier - borderThickness + std::min<SSIZE_T>(maxBbox.top, 0);
		const auto boundingHeight = static_cast<uint8_t>(maxBbox.bottom + std::max<SSIZE_T>(-maxBbox.top, 0) + static_cast<SSIZE_T>(2) * borderThickness);

		if (AscentPixels == AutoVerticalValues)
			result->Ascent(maxAscent + borderThickness);
		else
			result->Ascent(AscentPixels + borderThickness);

		if (LineHeightPixels == AutoVerticalValues)
			result->LineHeight(maxLineHeight + 2 * borderThickness);
		else
			result->LineHeight(LineHeightPixels + 2 * borderThickness);

		result->ReserveStorage(m_pImpl->CharacterPlans.size(), m_pImpl->Kernings.size());

		{
			for (auto& plan : m_pImpl->CharacterPlans | std::views::values) {
				m_pImpl->WorkPool.SubmitWork([&]() {
					try {
						if (m_pImpl->Cancelled)
							return;
						const auto waitRelease = m_pImpl->WaitSemaphore();

						m_pImpl->Progress.Progress += ProgressWeight_Draw;

						const auto& bbox = plan.GetBbox();
						if (bbox.empty)
							return;

						const auto leftExtension = std::max<SSIZE_T>(-bbox.left, globalOffsetX);
						const auto boundingWidth = static_cast<uint8_t>(leftExtension + bbox.right + borderThickness + borderThickness);
						const auto nextOffsetX = static_cast<int8_t>(bbox.advanceX + leftExtension - boundingWidth - globalOffsetX);
						const auto currentOffsetY = static_cast<int8_t>(
							(AlignToBaseline ? result->Ascent() - plan.Font->Ascent() : (0LL + result->LineHeight() - plan.Font->LineHeight()) / 2)
							+ globalOffsetY
							);

						const auto space = renderTarget.Draw(plan.Character, plan.Font,
							leftExtension, 0,
							boundingWidth, boundingHeight, borderThickness, borderOpacity);

						const auto resultingX = static_cast<uint16_t>(space.X + space.drawOffsetX - leftExtension);
						const auto resultingY = static_cast<uint16_t>(space.Y + space.drawOffsetY);
						result->AddFontEntry(plan.Character, space.Index, resultingX, resultingY, boundingWidth, std::min(space.BoundingHeight, boundingHeight), nextOffsetX, currentOffsetY);
					} catch (const std::exception& e) {
						OnError(e);
#ifdef _DEBUG
						throw;
#endif
					}
				});
			}
			m_pImpl->WorkPool.WaitOutstanding();
			if (m_pImpl->Cancelled)
				return nullptr;
		}

		for (const auto& [pair, distance] : m_pImpl->Kernings) {
			m_pImpl->Progress.Progress += ProgressWeight_Kerning;

			const auto [font, c1, c2] = pair;
			const auto f1 = m_pImpl->CharacterPlans.at(c1).Font;
			const auto f2 = m_pImpl->CharacterPlans.at(c2).Font;
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

		m_pImpl->Progress.Finished = true;

		return result;
	} catch (const std::exception& e) {
		OnError(e);
		return {};
	}
}

struct Sqex::FontCsv::FontSetsCreator::Implementation {
	const CreateConfig::FontCreateConfig Config;
	const std::filesystem::path GamePath;
	const Win32::Event CancelEvent = Win32::Event::Create();
	bool Cancelled = false;
	Win32::Thread WorkerThread;

	std::mutex SourceFontMapAccessMtx, SourceFontLoadMtx;
	std::map<std::filesystem::path, std::unique_ptr<Sqpack::Reader>> SqpackReaders;
	std::map<std::tuple<std::filesystem::path, std::filesystem::path>, std::vector<std::shared_ptr<const Texture::MipmapStream>>> GameTextures;
	std::map<std::string, std::shared_ptr<const SeCompatibleDrawableFont<uint8_t>>> SourceFonts;

	ResultFontSets Result;
	std::mutex ResultMtx;
	std::map<std::string, std::map<std::string, std::unique_ptr<FontCsvCreator>>> ResultWork;

	Win32::TpEnvironment WorkPool;
	Win32::Semaphore CoreLimitSemaphore;
	std::string LastErrorMessage;

	CallOnDestruction::Multiple Cleanup;

	Implementation(CreateConfig::FontCreateConfig config, std::filesystem::path gamePath, LONG maxCoreCount)
		: Config(std::move(config))
		, GamePath(std::move(gamePath))
		, CoreLimitSemaphore(maxCoreCount ? Win32::Semaphore::Create(nullptr, maxCoreCount, maxCoreCount) : nullptr) {
	}

	~Implementation() {
		Cleanup.Clear();
	}

	CallOnDestruction WaitSemaphore() {
		if (!CoreLimitSemaphore)
			return {};
		if (CoreLimitSemaphore.Wait(INFINITE) != WAIT_OBJECT_0)
			throw std::runtime_error("wait != WAIT_OBJECT_0");
		return CallOnDestruction([this]() {
			void(CoreLimitSemaphore.Release(1));
		});
	}

	const SeCompatibleDrawableFont<uint8_t>& GetSourceFont(const std::string& name) {
		{
			const auto lock = std::lock_guard(SourceFontMapAccessMtx);
			if (const auto it = SourceFonts.find(name); it != SourceFonts.end())
				return *it->second;
		}
		{
			const auto lockLoad = std::lock_guard(SourceFontLoadMtx);

			// test again
			{
				const auto lock = std::lock_guard(SourceFontMapAccessMtx);
				if (const auto it = SourceFonts.find(name); it != SourceFonts.end())
					return *it->second;
			}

			std::shared_ptr<const SeCompatibleDrawableFont<uint8_t>> newFont;
			const auto& inputFontSource = Config.sources.at(name);
			if (const auto& source = inputFontSource.gameSource; inputFontSource.isGameSource) {
				auto indexFilePath = source.indexFile.empty() ? GamePath / LR"(sqpack\ffxiv\000000.win32.index)" : std::filesystem::path(source.indexFile);
				indexFilePath = canonical(indexFilePath);

				auto reader = SqpackReaders.find(indexFilePath);
				if (reader == SqpackReaders.end())
					reader = SqpackReaders.emplace(indexFilePath, std::make_unique<Sqpack::Reader>(indexFilePath)).first;

				auto textureKey = std::make_tuple(indexFilePath, source.texturePath);
				auto textures = GameTextures.find(textureKey);
				if (textures == GameTextures.end())
					textures = GameTextures.emplace(textureKey, std::vector<std::shared_ptr<const Texture::MipmapStream>>()).first;

				for (size_t i = textures->second.size(); i < source.textureCount; ++i) {
					auto mipmap = Texture::MipmapStream::FromTexture(std::make_shared<Sqpack::EntryRawStream>(reader->second->GetEntryProvider(std::format(source.texturePath.string(), i + 1))), 0);

					// preload sqex entry layout so that we can process stuff multithreaded later
					void(mipmap->ReadStreamIntoVector<char>(0));

					textures->second.emplace_back(std::move(mipmap));
				}

				newFont = std::make_shared<SeDrawableFont<Texture::RGBA4444, uint8_t>>(
					std::make_shared<ModifiableFontCsvStream>(Sqpack::EntryRawStream(reader->second->GetEntryProvider(source.fdtPath))), textures->second);
			} else if (const auto& source = inputFontSource.gdiSource; inputFontSource.isGdiSource) {
				newFont = std::make_shared<GdiDrawingFont<uint8_t>>(source);
			} else if (const auto& source = inputFontSource.directWriteSource; inputFontSource.isDirectWriteSource) {
				if (!source.fontFile.empty())
					newFont = std::make_shared<DirectWriteDrawingFont<uint8_t>>(
						source.fontFile, source.faceIndex, static_cast<float>(source.height), source.renderMode
						);
				else if (!source.familyName.empty())
					newFont = std::make_shared<DirectWriteDrawingFont<uint8_t>>(
						FromUtf8(source.familyName).c_str(), static_cast<float>(source.height), static_cast<DWRITE_FONT_WEIGHT>(source.weight), source.stretch, source.style, source.renderMode
						);
				else
					throw std::invalid_argument("Neither of fontFile nor familyName was specified.");
			} else if (const auto& source = inputFontSource.freeTypeSource; inputFontSource.isFreeTypeSource) {
				if (!source.fontFile.empty())
					newFont = std::make_shared<FreeTypeDrawingFont<uint8_t>>(
						source.fontFile, source.faceIndex, static_cast<float>(source.height), source.loadFlags
						);
				else if (!source.familyName.empty())
					newFont = std::make_shared<FreeTypeDrawingFont<uint8_t>>(
						FromUtf8(source.familyName).c_str(), static_cast<float>(source.height), static_cast<DWRITE_FONT_WEIGHT>(source.weight), source.stretch, source.style, source.loadFlags
						);
				else
					throw std::invalid_argument("Neither of fontFile nor familyName was specified.");
			} else
				throw std::invalid_argument("Could not identify which font to load.");

			// preload for multithreading
			void(newFont->GetAllCharacters());
			void(newFont->GetKerningTable());

			const auto lockAccess = std::lock_guard(SourceFontMapAccessMtx);
			const auto newFontRawPtr = newFont.get();
			SourceFonts.emplace(name, std::move(newFont));
			return *newFontRawPtr;
		}
	}

	void Compile() {
		std::map<std::string, std::unique_ptr<FontCsvCreator::RenderTarget>> renderTargets;
		for (const auto& [textureGroupFilenamePattern, fonts] : Config.targets) {
			renderTargets.emplace(textureGroupFilenamePattern, std::make_unique<FontCsvCreator::RenderTarget>(Config.textureWidth, Config.textureHeight, Config.glyphGap));
			Result.Result.emplace(textureGroupFilenamePattern, ResultFontSet{});
			auto& remainingFonts = ResultWork.emplace(textureGroupFilenamePattern, std::map<std::string, std::unique_ptr<FontCsvCreator>>()).first->second;
			for (const auto& fontName : fonts.fontTargets | std::views::keys) {
				auto creator = std::make_unique<FontCsvCreator>();
				Cleanup += creator->OnError([this](const std::exception& e) {
					if (LastErrorMessage.empty()) {
						if (e.what() && *e.what())
							LastErrorMessage = e.what();
						else
							LastErrorMessage = "Unknown error";
						void(Win32::Thread(L"Canceller", [this]() { Cancel(false); }));
					}
				});
				remainingFonts.emplace(fontName, std::move(creator));
			}
		}

		for (const auto& [textureGroupFilenamePattern, fonts] : Config.targets) {
			auto& resultSet = Result.Result.at(textureGroupFilenamePattern);
			auto& target = *renderTargets.at(textureGroupFilenamePattern);
			auto& remainingFonts = ResultWork.at(textureGroupFilenamePattern);

			std::vector<std::string> sortedRemainingFontList;
			for (const auto& i : fonts.fontTargets | std::views::keys)
				sortedRemainingFontList.emplace_back(i);
			std::ranges::sort(sortedRemainingFontList, [&](const auto& l, const auto& r) {
				return fonts.fontTargets.at(l).height < fonts.fontTargets.at(r).height;
			});

			for (const auto& fontName : sortedRemainingFontList) {
				auto& plan = fonts.fontTargets.at(fontName);
				auto& creator = *remainingFonts.at(fontName);

				WorkPool.SubmitWork([&, fontName = fontName]() {
					try {
						if (Cancelled)
							return;
						const auto waitRelease = WaitSemaphore();

						creator.SizePoints = static_cast<float>(plan.height);
						if (plan.autoAscent)
							creator.AscentPixels = FontCsvCreator::AutoVerticalValues;
						else if (!plan.ascentFrom.empty())
							creator.AscentPixels = GetSourceFont(plan.ascentFrom).Ascent();
						else
							creator.AscentPixels = 0;
						if (plan.autoLineHeight)
							creator.LineHeightPixels = FontCsvCreator::AutoVerticalValues;
						else if (!plan.ascentFrom.empty())
							creator.LineHeightPixels = GetSourceFont(plan.lineHeightFrom).LineHeight();
						else
							creator.LineHeightPixels = 0;
						creator.MinGlobalOffsetX = plan.minGlobalOffsetX;
						creator.MaxGlobalOffsetX = plan.maxGlobalOffsetX;
						creator.GlobalOffsetYModifier = plan.globalOffsetY;
						creator.AlwaysApplyKerningCharacters.insert(plan.charactersToKernAcrossFonts.begin(), plan.charactersToKernAcrossFonts.end());
						creator.AlignToBaseline = plan.alignToBaseline;
						creator.BorderThickness = plan.borderThickness;
						creator.BorderOpacity = plan.borderOpacity;

						for (const auto& source : plan.sources) {
							const auto& sourceFont = GetSourceFont(source.name);
							if (source.ranges.empty()) {
								creator.AddFont(&sourceFont, source.replace, source.extendRange);
							} else {
								for (const auto& rangeName : source.ranges) {
									for (const auto& range : Config.ranges.at(rangeName).ranges | std::views::values) {
										for (auto i = range.from; i < range.to; ++i)
											creator.AddCharacter(i, &sourceFont, source.replace, source.extendRange);
										if (range.from != range.to)  // separate line to prevent overflow
											creator.AddCharacter(range.to, &sourceFont, source.replace, source.extendRange);

										if (Cancelled)
											return;
									}
								}
							}
							creator.AddKerning(&sourceFont, source.replace);

							if (Cancelled)
								return;
						}

						auto compileResult = creator.Compile(target);
						if (Cancelled)
							return;

						{
							const auto lock = std::lock_guard(ResultMtx);
							resultSet.Fonts.emplace(fontName, std::move(compileResult));

							auto allFinished = true;
							for (const auto& c : remainingFonts | std::views::values)
								allFinished &= c->GetProgress().Finished;
							if (!allFinished)
								return;
						}
						if (Cancelled)
							return;

						if (Config.textureType == Texture::CompressionType::RGBA4444)
							target.Finalize<Texture::RGBA4444, Texture::CompressionType::RGBA4444>();
						else if (Config.textureType == Texture::CompressionType::RGBA_1)
							target.Finalize<Texture::RGBA8888, Texture::CompressionType::RGBA_1>();
						else if (Config.textureType == Texture::CompressionType::RGBA_2)
							target.Finalize<Texture::RGBA8888, Texture::CompressionType::RGBA_2>();
						else
							throw std::invalid_argument("Unsupported textureType for font generation");

						resultSet.Textures = target.AsTextureStreamVector();
					} catch (const std::exception& e) {
						if (LastErrorMessage.empty()) {
							LastErrorMessage = e.what() && *e.what() ? e.what() : "Unknwon error";
							void(Win32::Thread(L"Canceller", [this]() { Cancel(false); }));
						}
#ifdef _DEBUG
						throw;
#endif
					}
				});
			}
		}

		WorkPool.WaitOutstanding();
	}

	void Cancel(bool wait = true) {
		Cancelled = true;
		CancelEvent.Set();
		WorkPool.Cancel();
		const auto lock = std::lock_guard(ResultMtx);
		for (const auto& v1 : ResultWork | std::views::values) {
			for (const auto& v2 : v1 | std::views::values) {
				v2->Cancel();
			}
		}
		if (wait)
			WorkerThread.Wait();
	}
};

Sqex::FontCsv::FontSetsCreator::FontSetsCreator(CreateConfig::FontCreateConfig config, std::filesystem::path gamePath, LONG maxCoreCount)
	: m_pImpl(std::make_unique<Implementation>(std::move(config), std::move(gamePath), maxCoreCount ? maxCoreCount : Win32::GetCoreCount())) {
	m_pImpl->WorkerThread = Win32::Thread(L"FontSetsCreator", [this]() { m_pImpl->Compile(); });
}

Sqex::FontCsv::FontSetsCreator::~FontSetsCreator() {
	m_pImpl->Cancel();
}

std::map<Sqex::Sqpack::EntryPathSpec, std::shared_ptr<const Sqex::RandomAccessStream>> Sqex::FontCsv::FontSetsCreator::ResultFontSets::GetAllStreams() const {
	std::map<Sqpack::EntryPathSpec, std::shared_ptr<const RandomAccessStream>> result;

	for (const auto& [textureFilenameFormat, fontSet] : Result) {
		for (size_t i = 0; i < fontSet.Textures.size(); ++i)
			result.emplace(std::format("common/font/{}", std::format(textureFilenameFormat, i + 1)), fontSet.Textures[i]);

		for (const auto& [fontName, newFontCsv] : fontSet.Fonts)
			result.emplace(std::format("common/font/{}", fontName), newFontCsv);
	}

	return result;
}

const Sqex::FontCsv::FontSetsCreator::ResultFontSets& Sqex::FontCsv::FontSetsCreator::GetResult() const {
	if (m_pImpl->Cancelled)
		throw std::runtime_error(m_pImpl->LastErrorMessage.empty() ? "Cancelled" : m_pImpl->LastErrorMessage.c_str());
	if (m_pImpl->WorkerThread.Wait(0) == WAIT_TIMEOUT)
		throw std::runtime_error("not finished");

	return m_pImpl->Result;
}

bool Sqex::FontCsv::FontSetsCreator::Wait(DWORD timeout) const {
	const auto res = m_pImpl->WorkerThread.Wait(false, {m_pImpl->CancelEvent}, timeout);
	if (res == WAIT_TIMEOUT)
		return false;
	if (res == WAIT_OBJECT_0)
		return true;
	throw std::runtime_error(m_pImpl->LastErrorMessage.empty() ? "Cancelled" : m_pImpl->LastErrorMessage.c_str());
}

const std::string& Sqex::FontCsv::FontSetsCreator::GetError() const {
	return m_pImpl->LastErrorMessage;
}

Sqex::FontCsv::FontCreationProgress Sqex::FontCsv::FontSetsCreator::GetProgress() const {
	auto result = FontCreationProgress{
		.Finished = m_pImpl->WorkerThread.Wait(0) != WAIT_TIMEOUT,
	};
	const auto lock = std::lock_guard(m_pImpl->ResultMtx);
	for (const auto& v1 : m_pImpl->ResultWork | std::views::values) {
		for (const auto& v2 : v1 | std::views::values) {
			result += v2->GetProgress();
		}
	}
	return result;
}
