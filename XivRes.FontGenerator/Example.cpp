#include <iostream>
#include <Windows.h>
#include <windowsx.h>

#include "XivRes/FontdataStream.h"
#include "XivRes/GameReader.h"
#include "XivRes/MipmapStream.h"
#include "XivRes/PackedFileUnpackingStream.h"
#include "XivRes/PixelFormats.h"
#include "XivRes/TextureStream.h"
#include "XivRes/FontGenerator/CodepointLimitingFixedSizeFont.h"
#include "XivRes/FontGenerator/FontdataPacker.h"
#include "XivRes/FontGenerator/GameFontdataFixedSizeFont.h"
#include "XivRes/FontGenerator/MergedFixedSizeFont.h"
#include "XivRes/FontGenerator/TextMeasurer.h"
#include "XivRes/Internal/TexturePreview.Windows.h"

extern const char8_t* const g_pszTestString;

int main() {
	system("chcp 65001");

	auto fontGlos = XivRes::GameReader(R"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game)").GetFonts(XivRes::GameFontType::font);
	auto fontKrns = XivRes::GameReader(R"(C:\Program Files (x86)\FINAL FANTASY XIV - KOREA\game)").GetFonts(XivRes::GameFontType::krn_axis);
	auto fontChns = XivRes::GameReader(R"(C:\Program Files (x86)\SNDA\FFXIV\game)").GetFonts(XivRes::GameFontType::chn_axis);

	std::set<char32_t> codepointsGlo, codepointsKrn, codepointsChn;
	codepointsGlo = fontGlos[0]->GetAllCodepoints();
	for (char32_t i = 'A'; i <= 'Z'; i += 3) {
		codepointsGlo.erase(i + 1);
		codepointsGlo.erase(i + 2);
	}
	for (char32_t i = 'a'; i <= 'z'; i += 3) {
		codepointsGlo.erase(i + 1);
		codepointsGlo.erase(i + 2);
	}
	for (char32_t i = '0'; i <= '9'; i += 3) {
		codepointsGlo.erase(i + 1);
		codepointsGlo.erase(i + 2);
	}
	std::ranges::set_difference(fontKrns[0]->GetAllCodepoints(), codepointsGlo, std::inserter(codepointsKrn, codepointsKrn.end()));
	for (char32_t i = 'A'; i <= 'Z'; i += 3) {
		codepointsKrn.erase(i + 2);
	}
	for (char32_t i = 'a'; i <= 'z'; i += 3) {
		codepointsKrn.erase(i + 2);
	}
	for (char32_t i = '0'; i <= '9'; i += 3) {
		codepointsKrn.erase(i + 2);
	}
	std::ranges::set_difference(fontChns[0]->GetAllCodepoints(), codepointsGlo, std::inserter(codepointsChn, codepointsChn.end()));

	XivRes::FontGenerator::FontdataPacker packer;
	packer.AddFont(fontGlos[0]);
	packer.AddFont(std::make_shared<XivRes::FontGenerator::MergedFixedSizeFont>(std::vector<std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>> {
		std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontGlos[1], codepointsGlo),
			std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontKrns[0], codepointsKrn),
			std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontChns[0], codepointsChn),
	}));
	packer.AddFont(std::make_shared<XivRes::FontGenerator::MergedFixedSizeFont>(std::vector<std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>> {
		std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontGlos[2], codepointsGlo),
			std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontKrns[1], codepointsKrn),
			std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontChns[1], codepointsChn),
	}));
	packer.AddFont(std::make_shared<XivRes::FontGenerator::MergedFixedSizeFont>(std::vector<std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>> {
		std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontGlos[3], codepointsGlo),
			std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontKrns[2], codepointsKrn),
			std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontChns[2], codepointsChn),
	}));
	packer.AddFont(std::make_shared<XivRes::FontGenerator::MergedFixedSizeFont>(std::vector<std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>> {
		std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontGlos[4], codepointsGlo),
			std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontKrns[3], codepointsKrn),
			std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontChns[3], codepointsChn),
	}));
	for (int i = 5; i < 23; i++) {
		packer.AddFont(fontGlos[i]);
	}
	dynamic_cast<XivRes::FontGenerator::MergedFixedSizeFont*>(packer.GetFont(1).get())->SetIndividualVerticalAdjustment(1, 1);
	dynamic_cast<XivRes::FontGenerator::MergedFixedSizeFont*>(packer.GetFont(4).get())->SetIndividualVerticalAdjustment(1, 1);
	dynamic_cast<XivRes::FontGenerator::MergedFixedSizeFont*>(packer.GetFont(1).get())->SetIndividualVerticalAdjustment(2, 1);
	dynamic_cast<XivRes::FontGenerator::MergedFixedSizeFont*>(packer.GetFont(4).get())->SetIndividualVerticalAdjustment(2, 1);

	{
		std::vector<std::thread> threads;
		
		//threads.emplace_back([&]() {
		//	auto face = packer.GetFont(0);
		//	XivRes::Internal::ShowTextureStream(*XivRes::FontGenerator::TextMeasurer(*face)
		//		.Measure(pszTestString)
		//		.CreateMipmap(*face, XivRes::RGBA8888(255, 255, 255, 255), XivRes::RGBA8888(0, 0, 0, 200))
		//		->ToSingleTextureStream(), L"AXIS_12");
		//});
		//threads.emplace_back([&]() {
		//	auto face = packer.GetFont(1);
		//	XivRes::Internal::ShowTextureStream(*XivRes::FontGenerator::TextMeasurer(*face)
		//		.Measure(pszTestString)
		//		.CreateMipmap(*face, XivRes::RGBA8888(255, 255, 255, 255), XivRes::RGBA8888(0, 0, 0, 200))
		//		->ToSingleTextureStream(), L"AXIS_14");
		//});
		//threads.emplace_back([&]() {
		//	auto face = packer.GetFont(2);
		//	XivRes::Internal::ShowTextureStream(*XivRes::FontGenerator::TextMeasurer(*face)
		//		.Measure(pszTestString)
		//		.CreateMipmap(*face, XivRes::RGBA8888(255, 255, 255, 255), XivRes::RGBA8888(0, 0, 0, 200))
		//		->ToSingleTextureStream(), L"AXIS_18");
		//});
		//threads.emplace_back([&]() {
		//	auto face = packer.GetFont(3);
		//	XivRes::Internal::ShowTextureStream(*XivRes::FontGenerator::TextMeasurer(*face)
		//		.Measure(pszTestString)
		//		.CreateMipmap(*face, XivRes::RGBA8888(255, 255, 255, 255), XivRes::RGBA8888(0, 0, 0, 200))
		//		->ToSingleTextureStream(), L"AXIS_36");
		//});

		//*
		auto [fdts, texs] = packer.Compile();
		auto res = std::make_shared<XivRes::TextureStream>(texs[0]->Type, texs[0]->Width, texs[0]->Height, 1, 1, texs.size());
		for (size_t i = 0; i < texs.size(); i++)
			res->SetMipmap(0, i, texs[i]);

		threads.emplace_back([&]() { XivRes::Internal::ShowTextureStream(*res); });

		auto face = std::make_shared<XivRes::FontGenerator::GameFontdataFixedSizeFont>(fdts[0], texs);
		threads.emplace_back([&]() {
			XivRes::Internal::ShowTextureStream(*XivRes::FontGenerator::TextMeasurer(*face)
				.Measure(g_pszTestString)
				.CreateMipmap(*face, XivRes::RGBA8888(255, 255, 255, 255), XivRes::RGBA8888(0, 0, 0, 200))
				->ToSingleTextureStream());
		});
		//*/

		for (auto& t : threads)
			t.join();
	}
	return 0;
}
