#pragma once

#include "main.h"
#include <vector>
#include <string>
#include "assimp/cimport.h"

class AnimationPlayer;
class ComputeSkinningManager;

class AnimationBakeManager {
public:
	// アニメーションをベイクしてファイルに保存する
    bool BakeAnimation(
        const char* animName,
        const char* outFilePath,

        AnimationPlayer* player,

        const aiScene* modelScene,
        const aiScene* animScene,

        ID3D11Buffer* boneConstantBuffer,
        ComputeSkinningManager* computeSkinningManager,

        ID3D11ShaderResourceView** skinInputSRV,
        ID3D11UnorderedAccessView** skinOutputUAV,
        ID3D11Buffer** skinOutputBuffer,

        int maxBoneCount
    );
};