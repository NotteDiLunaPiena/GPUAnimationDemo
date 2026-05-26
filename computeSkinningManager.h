#pragma once
#include "main.h"
#include "renderer.h"

class ComputeSkinningManager
{
private:
	ID3D11ComputeShader* m_SkinningCS = nullptr;

public:
	// コンピュートシェーダ読み込み
	void LoadComputeShader(const char* fileName);
	// スキニング用バッファの作成
	void CreateSkinningBuffers(
		VERTEX_3D* vertices,
		UINT vertexCount,
		ID3D11Buffer** skinInputBuffer,
		ID3D11ShaderResourceView** skinInputSRV,
		ID3D11Buffer** skinOutputBuffer,
		ID3D11UnorderedAccessView** skinOutputUAV,
		ID3D11Buffer** csVertexBufferGPU
	);
	// スキニングCSのディスパッチ
	void Dispatch(
		ID3D11Buffer* boneConstantBuffer,
		ID3D11ShaderResourceView* skinInputSRV,
		ID3D11UnorderedAccessView* skinOutputUAV,
		UINT vertexCount
	);
	//解放
	void Uninit();

	//ゲッター
	//シェーダー
	ID3D11ComputeShader* GetSkinningCS() const { return m_SkinningCS; }

};
