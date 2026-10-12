#include "pch.h"
#include "MainApp/FontReplacement/EdgeShaderCode.h"

#include "resource.h"
#include "XivAlexander.h"

namespace EdgeShaderCode = XivAlexander::Apps::MainApp::FontReplacement::EdgeShaderCode;

namespace {
	uint32_t ReadU32(const uint8_t* p) {
		uint32_t v;
		std::memcpy(&v, p, sizeof v);
		return v;
	}

	void WriteU32(uint8_t* p, uint32_t v) {
		std::memcpy(p, &v, sizeof v);
	}

	constexpr std::array<int, 64> Shifts{
		7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
		5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
		4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
		6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
	};

	constexpr std::array<uint32_t, 64> Sines{
		0xD76AA478, 0xE8C7B756, 0x242070DB, 0xC1BDCEEE, 0xF57C0FAF, 0x4787C62A, 0xA8304613, 0xFD469501,
		0x698098D8, 0x8B44F7AF, 0xFFFF5BB1, 0x895CD7BE, 0x6B901122, 0xFD987193, 0xA679438E, 0x49B40821,
		0xF61E2562, 0xC040B340, 0x265E5A51, 0xE9B6C7AA, 0xD62F105D, 0x02441453, 0xD8A1E681, 0xE7D3FBC8,
		0x21E1CDE6, 0xC33707D6, 0xF4D50D87, 0x455A14ED, 0xA9E3E905, 0xFCEFA3F8, 0x676F02D9, 0x8D2A4C8A,
		0xFFFA3942, 0x8771F681, 0x6D9D6122, 0xFDE5380C, 0xA4BEEA44, 0x4BDECFA9, 0xF6BB4B60, 0xBEBFBC70,
		0x289B7EC6, 0xEAA127FA, 0xD4EF3085, 0x04881D05, 0xD9D4D039, 0xE6DB99E5, 0x1FA27CF8, 0xC4AC5665,
		0xF4292244, 0x432AFF97, 0xAB9423A7, 0xFC93A039, 0x655B59C3, 0x8F0CCC92, 0xFFEFF47D, 0x85845DD1,
		0x6FA87E4F, 0xFE2CE6E0, 0xA3014314, 0x4E0811A1, 0xF7537E82, 0xBD3AF235, 0x2AD7D2BB, 0xEB86D391,
	};

	// MD5's rounds over a 64-byte block.
	void Transform(std::array<uint32_t, 4>& state, const uint8_t* block) {
		std::array<uint32_t, 16> m{};
		for (size_t i = 0; i < m.size(); i++)
			m[i] = ReadU32(block + 4 * i);

		auto [a, b, c, d] = state;
		for (int i = 0; i < 64; i++) {
			uint32_t f;
			int g;
			switch (i >> 4) {
				case 0:
					f = (b & c) | (~b & d);
					g = i;
					break;
				case 1:
					f = (d & b) | (~d & c);
					g = (5 * i + 1) & 15;
					break;
				case 2:
					f = b ^ c ^ d;
					g = (3 * i + 5) & 15;
					break;
				default:
					f = c ^ (b | ~d);
					g = (7 * i) & 15;
					break;
			}
			f += a + Sines[i] + m[g];
			a = d;
			d = c;
			c = b;
			b += std::rotl(f, Shifts[i]);
		}
		state[0] += a;
		state[1] += b;
		state[2] += c;
		state[3] += d;
	}

	// The code chunk's (SHEX or SHDR) tokens.
	std::span<uint8_t> FindCode(std::span<uint8_t> dxbc) {
		if (dxbc.size() < 32 || ReadU32(dxbc.data()) != 0x43425844)
			throw std::runtime_error("Not a DXBC container");
		const auto chunks = ReadU32(dxbc.data() + 28);
		for (uint32_t i = 0; i < chunks && 32 + 4 * (i + 1) <= dxbc.size(); i++) {
			const auto at = ReadU32(dxbc.data() + 32 + 4 * i);
			if (at + 8 > dxbc.size())
				break;
			const auto fourCc = ReadU32(dxbc.data() + at);
			if (fourCc == 0x58454853 || fourCc == 0x52444853)
				return dxbc.subspan(at + 8, (std::min<size_t>)(ReadU32(dxbc.data() + at + 4), dxbc.size() - at - 8));
		}
		throw std::runtime_error("The shader has no code chunk");
	}
}

std::array<uint8_t, 16> EdgeShaderCode::ComputeChecksum(std::span<const uint8_t> dxbc) {
	// MD5's rounds, but the bit count goes in front of the last block's data, and (bits >> 2) | 1 at its end.
	const auto data = dxbc.subspan(20);
	const auto bits = static_cast<uint32_t>(data.size() * 8);
	std::array<uint32_t, 4> state{0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476};
	const auto full = data.size() / 64 * 64;
	for (size_t i = 0; i < full; i += 64)
		Transform(state, data.data() + i);

	const auto last = data.subspan(full);
	std::array<uint8_t, 64> block{};
	if (last.size() >= 56) {
		std::ranges::copy(last, block.begin());
		block[last.size()] = 0x80;
		Transform(state, block.data());
		block.fill(0);
		WriteU32(block.data(), bits);
	} else {
		WriteU32(block.data(), bits);
		std::ranges::copy(last, block.begin() + 4);
		block[4 + last.size()] = 0x80;
	}
	WriteU32(block.data() + 60, (bits >> 2) | 1);
	Transform(state, block.data());

	std::array<uint8_t, 16> res{};
	for (size_t i = 0; i < state.size(); i++)
		WriteU32(res.data() + 4 * i, state[i]);
	return res;
}

size_t EdgeShaderCode::Patch(std::span<uint8_t> dxbc, float softness) {
	return PatchConstants(dxbc, {{Sentinel, softness}});
}

size_t EdgeShaderCode::PatchConstants(std::span<uint8_t> dxbc, std::initializer_list<std::pair<uint32_t, float>> values) {
	const auto code = FindCode(dxbc);
	size_t count = 0;
	for (size_t i = 0; i + 4 <= code.size(); i += 4) {
		const auto word = ReadU32(code.data() + i);
		for (const auto& [sentinel, value] : values) {
			if (word != sentinel)
				continue;
			WriteU32(code.data() + i, std::bit_cast<uint32_t>(value));
			count++;
			break;
		}
	}
	if (count) {
		const auto checksum = ComputeChecksum(dxbc);
		std::ranges::copy(checksum, dxbc.begin() + 4);
	}
	return count;
}

std::vector<uint8_t> EdgeShaderCode::Get(float softness, std::string& failure) {
	const auto module = static_cast<HMODULE>(Dll::Module());
	const auto resource = FindResourceW(module, MAKEINTRESOURCEW(IDR_FONTEDGEPS), RT_RCDATA);
	if (!resource)
		throw std::runtime_error("FontEdgePS.cso isn't embedded.");
	const auto begin = static_cast<const uint8_t*>(LockResource(LoadResource(module, resource)));
	std::vector<uint8_t> code(begin, begin + SizeofResource(module, resource));
	if (!Patch(code, softness))
		failure = "FontEdgePS.cso has no Softness to set; the edge is drawn with the value it was compiled with.";
	return code;
}
