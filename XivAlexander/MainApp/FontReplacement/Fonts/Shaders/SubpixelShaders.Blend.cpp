#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Shaders/SubpixelShaders.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

std::string FontReplacement::SubpixelShaders::Describe(uint32_t blend) {
	// PackedBlendStateDesc: enable bit 0, op 1-3, src 4-7, dest 8-11, alpha op 12-14, alpha src 15-18, alpha dest 19-22; fields hold values minus one.
	const auto field = [blend](int shift, int bits) { return ((blend >> shift) & ((1u << bits) - 1)) + 1; };
	return std::format(
		"BlendState({}, op={}(src={}, dest={}), alpha={}(src={}, dest={}))",
		blend & 1 ? "enabled" : "disabled",
		DescribeBlendOp(field(1, 3)),
		DescribeBlend(field(4, 4)),
		DescribeBlend(field(8, 4)),
		DescribeBlendOp(field(12, 3)),
		DescribeBlend(field(15, 4)),
		DescribeBlend(field(19, 4)));
}

std::string FontReplacement::SubpixelShaders::DescribeBlendOp(uint32_t op) {
	switch (op) {
		case D3D11_BLEND_OP_ADD: return "ADD";
		case D3D11_BLEND_OP_SUBTRACT: return "SUBTRACT";
		case D3D11_BLEND_OP_REV_SUBTRACT: return "REV_SUBTRACT";
		case D3D11_BLEND_OP_MIN: return "MIN";
		case D3D11_BLEND_OP_MAX: return "MAX";
		default: return std::format("unknown(0x{:08X})", op);
	}
}

std::string FontReplacement::SubpixelShaders::DescribeBlend(uint32_t blend) {
	switch (blend) {
		case D3D11_BLEND_ZERO: return "ZERO";
		case D3D11_BLEND_ONE: return "ONE";
		case D3D11_BLEND_SRC_COLOR: return "SRC_COLOR";
		case D3D11_BLEND_INV_SRC_COLOR: return "INV_SRC_COLOR";
		case D3D11_BLEND_SRC_ALPHA: return "SRC_ALPHA";
		case D3D11_BLEND_INV_SRC_ALPHA: return "INV_SRC_ALPHA";
		case D3D11_BLEND_DEST_ALPHA: return "DEST_ALPHA";
		case D3D11_BLEND_INV_DEST_ALPHA: return "INV_DEST_ALPHA";
		case D3D11_BLEND_DEST_COLOR: return "DEST_COLOR";
		case D3D11_BLEND_INV_DEST_COLOR: return "INV_DEST_COLOR";
		case D3D11_BLEND_SRC_ALPHA_SAT: return "SRC_ALPHA_SAT";
		case D3D11_BLEND_BLEND_FACTOR: return "BLEND_FACTOR";
		case D3D11_BLEND_INV_BLEND_FACTOR: return "INV_BLEND_FACTOR";
		case D3D11_BLEND_SRC1_COLOR: return "SRC1_COLOR";
		case D3D11_BLEND_INV_SRC1_COLOR: return "INV_SRC1_COLOR";
		case D3D11_BLEND_SRC1_ALPHA: return "SRC1_ALPHA";
		case D3D11_BLEND_INV_SRC1_ALPHA: return "INV_SRC1_ALPHA";
		default: return std::format("unknown(0x{:08X})", blend);
	}
}
