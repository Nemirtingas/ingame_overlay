#include "shaders.h"
#include "sha256.h"

struct Shader_t
{
	std::vector<uint8_t> shaderData;
	std::string hash;

	Shader_t(std::vector<uint8_t>& shaderData) :
		shaderData(std::move(shaderData)),
		hash(SHA256()(this->shaderData.data(), this->shaderData.size()))
	{
	}
};

int main(int argc, char* argv[])
{
	std::vector<Shader_t> pixelShaders;

	pixelShaders.emplace_back(BuildDX10PixelShader());
	pixelShaders.emplace_back(BuildDX11PixelShader(D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_9_1));
	pixelShaders.emplace_back(BuildDX11PixelShader(D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_9_2));
	pixelShaders.emplace_back(BuildDX11PixelShader(D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_9_3));
	pixelShaders.emplace_back(BuildDX11PixelShader(D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_10_0));
	pixelShaders.emplace_back(BuildDX11PixelShader(D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_10_1));
	pixelShaders.emplace_back(BuildDX11PixelShader(D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_11_0));
	pixelShaders.emplace_back(BuildDX11PixelShader(D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_11_1));
	pixelShaders.emplace_back(BuildDX11PixelShader(D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_12_0));
	pixelShaders.emplace_back(BuildDX11PixelShader(D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_12_1));
	pixelShaders.emplace_back(BuildDX12PixelShader());

	std::vector<Shader_t> vertexShaders;

	vertexShaders.emplace_back(BuildDX10VertexShader());
	vertexShaders.emplace_back(BuildDX11VertexShader(D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_9_1));
	vertexShaders.emplace_back(BuildDX11VertexShader(D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_9_2));
	vertexShaders.emplace_back(BuildDX11VertexShader(D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_9_3));
	vertexShaders.emplace_back(BuildDX11VertexShader(D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_10_0));
	vertexShaders.emplace_back(BuildDX11VertexShader(D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_10_1));
	vertexShaders.emplace_back(BuildDX11VertexShader(D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_11_0));
	vertexShaders.emplace_back(BuildDX11VertexShader(D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_11_1));
	vertexShaders.emplace_back(BuildDX11VertexShader(D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_12_0));
	vertexShaders.emplace_back(BuildDX11VertexShader(D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_12_1));
	vertexShaders.emplace_back(BuildDX12VertexShader());

	return 0;
}