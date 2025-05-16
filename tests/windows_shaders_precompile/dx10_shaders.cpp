#include <d3dcompiler.h>

#include "shaders.h"

std::vector<uint8_t> BuildDX10VertexShader()
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

    ID3DBlob* vertexShaderBlob;
    if (FAILED(D3DCompile(vertexShader, strlen(vertexShader), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vertexShaderBlob, nullptr)))
        return {};

    vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize();
    std::vector<uint8_t> shader(
        reinterpret_cast<const uint8_t*>(vertexShaderBlob->GetBufferPointer()),
        reinterpret_cast<const uint8_t*>(vertexShaderBlob->GetBufferPointer()) + vertexShaderBlob->GetBufferSize());
    vertexShaderBlob->Release();

    return shader;
}

std::vector<uint8_t> BuildDX10PixelShader()
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

    ID3DBlob* pixelShaderBlob;
    if (FAILED(D3DCompile(pixelShader, strlen(pixelShader), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &pixelShaderBlob, nullptr)))
        return {};

    std::vector<uint8_t> shader(
        reinterpret_cast<const uint8_t*>(pixelShaderBlob->GetBufferPointer()),
        reinterpret_cast<const uint8_t*>(pixelShaderBlob->GetBufferPointer()) + pixelShaderBlob->GetBufferSize());
    pixelShaderBlob->Release();
    return shader;
}

std::vector<uint8_t> BuildDX10RGBAPixelShader()
{
    static const char* pixelShader = "\
    struct PS_INPUT\
    {\
        float4 pos : SV_POSITION; \
        float4 col : COLOR0; \
        float2 uv : TEXCOORD0; \
    }; \
        sampler sampler0; \
        Texture2D texture0; \
        \
        float4 main(PS_INPUT input) : SV_Target\
    {\
        float4 out_col = texture0.Sample(sampler0, input.uv); \
        return float4(out_col.r, out_col.g, out_col.b, 1.0); \
    }";

    ID3DBlob* pixelShaderBlob;
    if (FAILED(D3DCompile(pixelShader, strlen(pixelShader), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &pixelShaderBlob, nullptr)))
        return {};

    std::vector<uint8_t> shader(
        reinterpret_cast<const uint8_t*>(pixelShaderBlob->GetBufferPointer()),
        reinterpret_cast<const uint8_t*>(pixelShaderBlob->GetBufferPointer()) + pixelShaderBlob->GetBufferSize());
    pixelShaderBlob->Release();
    return shader;
}