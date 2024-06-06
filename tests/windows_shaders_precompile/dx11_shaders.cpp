#include <d3dcompiler.h>

#include "shaders.h"

std::vector<uint8_t> BuildDX11VertexShader(D3D_FEATURE_LEVEL feature_level)
{
    static const char* vertexShader =
        "cbuffer vertexBuffer : register(b0) \
            {\
              float4x4 ProjectionMatrix; \
            };\
            struct VS_INPUT\
            {\
              float2 pos : POSITION;\
              float4 col : COLOR0;\
              float2 uv  : TEXCOORD0;\
            };\
            \
            struct PS_INPUT\
            {\
              float4 pos : SV_POSITION;\
              float4 col : COLOR0;\
              float2 uv  : TEXCOORD0;\
            };\
            \
            PS_INPUT main(VS_INPUT input)\
            {\
              PS_INPUT output;\
              output.pos = mul( ProjectionMatrix, float4(input.pos.xy, 0.f, 1.f));\
              output.col = input.col;\
              output.uv  = input.uv;\
              return output;\
            }";

    const char* target;
    switch (feature_level)
    {
        case D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_9_1 : target = "vs_4_0_level_9_1"; break;
        case D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_9_2 : target = "vs_4_0_level_9_1"; break;
        case D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_9_3 : target = "vs_4_0_level_9_3"; break;
        case D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_10_0: target = "vs_4_0"; break;
        case D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_10_1: target = "vs_4_1"; break;
        case D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_11_0: target = "vs_5_0"; break;
        case D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_11_1: target = "vs_5_0"; break;
        case D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_12_0: target = "vs_5_0"; break;
        case D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_12_1: target = "vs_5_0"; break;
    }

    ID3DBlob* vertexShaderBlob;
    if (FAILED(D3DCompile(vertexShader, strlen(vertexShader), nullptr, nullptr, nullptr, "main", target, 0, 0, &vertexShaderBlob, nullptr)))
        return {};

    std::vector<uint8_t> shader(
        reinterpret_cast<const uint8_t*>(vertexShaderBlob->GetBufferPointer()),
        reinterpret_cast<const uint8_t*>(vertexShaderBlob->GetBufferPointer()) + vertexShaderBlob->GetBufferSize());
    vertexShaderBlob->Release();
    return shader;
}

std::vector<uint8_t> BuildDX11PixelShader(D3D_FEATURE_LEVEL feature_level)
{
    static const char* pixelShader =
        "struct PS_INPUT\
            {\
              float4 pos : SV_POSITION;\
              float4 col : COLOR0;\
              float2 uv  : TEXCOORD0;\
            };\
            sampler sampler0;\
            Texture2D texture0;\
            \
            float4 main(PS_INPUT input) : SV_Target\
            {\
              float4 out_col = input.col * texture0.Sample(sampler0, input.uv); \
              return out_col; \
            }";

    const char* target;
    switch (feature_level)
    {
        case D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_9_1 : target = "ps_4_0_level_9_1"; break;
        case D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_9_2 : target = "ps_4_0_level_9_1"; break;
        case D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_9_3 : target = "ps_4_0_level_9_3"; break;
        case D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_10_0: target = "ps_4_0"; break;
        case D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_10_1: target = "ps_4_1"; break;
        case D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_11_0: target = "ps_5_0"; break;
        case D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_11_1: target = "ps_5_0"; break;
        case D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_12_0: target = "ps_5_0"; break;
        case D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_12_1: target = "ps_5_0"; break;
    }

    ID3DBlob* pixelShaderBlob;
    if (FAILED(D3DCompile(pixelShader, strlen(pixelShader), nullptr, nullptr, nullptr, "main", target, 0, 0, &pixelShaderBlob, nullptr)))
        return {};

    std::vector<uint8_t> shader(
        reinterpret_cast<const uint8_t*>(pixelShaderBlob->GetBufferPointer()),
        reinterpret_cast<const uint8_t*>(pixelShaderBlob->GetBufferPointer()) + pixelShaderBlob->GetBufferSize());
    pixelShaderBlob->Release();
    return shader;
}