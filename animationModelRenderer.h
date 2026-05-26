#pragma once

#include "main.h"
#include "renderer.h"

class ModelResource;

struct aiMaterial;

class AnimationModelRenderer {
public:
	// マテリアルの適用
	static void ApplyMaterial(aiMaterial* aiMat, ModelResource* resource);

	// インスタンシング描画
    static void DrawInstancedMesh(
        ID3D11Buffer* vertexBuffer,
        ID3D11Buffer* instanceBuffer,
        ID3D11Buffer* indexBuffer,
        UINT vertexStride,
        UINT instanceCount,
        UINT indexCount
    );
};