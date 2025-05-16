#pragma once

#include <d3dcommon.h>
#include <cstdint>
#include <vector>

std::vector<uint8_t> BuildDX10VertexShader();
std::vector<uint8_t> BuildDX10PixelShader();
std::vector<uint8_t> BuildDX10RGBAPixelShader();

std::vector<uint8_t> BuildDX11VertexShader(D3D_FEATURE_LEVEL feature_level);
std::vector<uint8_t> BuildDX11PixelShader(D3D_FEATURE_LEVEL feature_level);

std::vector<uint8_t> BuildDX12VertexShader();
std::vector<uint8_t> BuildDX12PixelShader();