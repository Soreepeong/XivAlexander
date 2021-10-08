#include "pch.h"

#include <XivAlexanderCommon/Sqex_FontCsv_CreateConfig.h>

int main() {
	std::ifstream f(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\FontConfig\Mix.JpKr.json)");
	nlohmann::json j;
	f >> j;
	Sqex::FontCsv::CreateConfig::FontCreateConfig c;
	from_json(j, c);
	return 0;
}