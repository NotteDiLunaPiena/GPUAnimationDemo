#include "animationBakeManager.h"
#include "animationPlayer.h"
#include "computeSkinningManager.h"
#include "renderer.h"

// アニメーションをベイクしてファイルに保存する
bool AnimationBakeManager::BakeAnimation(const char* animName, const char* outFilePath, AnimationPlayer* player, const aiScene* modelScene, const aiScene* animScene, ID3D11Buffer* boneConstantBuffer, ComputeSkinningManager* computeSkinningManager, ID3D11ShaderResourceView** skinInputSRV, ID3D11UnorderedAccessView** skinOutputUAV, ID3D11Buffer** skinOutputBuffer, int maxBoneCount)
{
    if (!animName || !outFilePath) return false;
    if (!player || !modelScene || !animScene) return false;
    if (!animScene->HasAnimations()) return false;
    if (!boneConstantBuffer || !computeSkinningManager) return false;
    if (!skinInputSRV || !skinOutputUAV || !skinOutputBuffer) return false;

    aiAnimation* animation = animScene->mAnimations[0];
    int duration = (int)animation->mDuration;
    if (duration <= 0) return false;

    UINT numMeshes = modelScene->mNumMeshes;
    ID3D11Device* device = Renderer::GetDevice();
    ID3D11DeviceContext* ctx = Renderer::GetDeviceContext();

    std::vector<ID3D11Buffer*> staging(numMeshes, nullptr);

    for (UINT m = 0; m < numMeshes; ++m)
    {
        UINT numVertices = modelScene->mMeshes[m]->mNumVertices;

        D3D11_BUFFER_DESC sd{};
        sd.Usage = D3D11_USAGE_STAGING;
        sd.ByteWidth = sizeof(VERTEX_SKIN_OUT) * numVertices;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        sd.BindFlags = 0;
        sd.MiscFlags = 0;

        HRESULT hr = device->CreateBuffer(&sd, nullptr, &staging[m]);
        assert(SUCCEEDED(hr));
    }

    FILE* f = fopen(outFilePath, "wb");
    if (!f)
    {
        for (auto b : staging)
        {
            if (b) b->Release();
        }
        return false;
    }

    uint32_t meshCountU32 = (uint32_t)numMeshes;
    fwrite(&meshCountU32, sizeof(meshCountU32), 1, f);

    for (UINT m = 0; m < numMeshes; ++m)
    {
        uint32_t vertexCount = (uint32_t)modelScene->mMeshes[m]->mNumVertices;
        uint32_t frameCount = (uint32_t)duration;
        uint32_t stride = (uint32_t)sizeof(VERTEX_SKIN_OUT);

        fwrite(&vertexCount, sizeof(vertexCount), 1, f);
        fwrite(&frameCount, sizeof(frameCount), 1, f);
        fwrite(&stride, sizeof(stride), 1, f);
    }

    for (int frame = 0; frame < duration; ++frame)
    {
        player->Update(animName, frame, animName, frame, 0.0f);

        D3D11_MAPPED_SUBRESOURCE ms{};
        ctx->Map(boneConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);

        struct CB_BONE_MATRIX
        {
            DirectX::XMFLOAT4X4 BoneMatrix[256];
        };

        auto* cbBone = (CB_BONE_MATRIX*)ms.pData;

        const auto& boneNameToIndex = player->GetBoneNameToIndex();
        const auto& bones = player->GetBones();

        for (auto& pair : boneNameToIndex)
        {
            unsigned int index = pair.second;
            if (index >= (unsigned int)maxBoneCount) continue;

            DirectX::XMMATRIX mat = DirectX::XMMATRIX(
                bones.at(pair.first).Matrix.a1, bones.at(pair.first).Matrix.b1, bones.at(pair.first).Matrix.c1, bones.at(pair.first).Matrix.d1,
                bones.at(pair.first).Matrix.a2, bones.at(pair.first).Matrix.b2, bones.at(pair.first).Matrix.c2, bones.at(pair.first).Matrix.d2,
                bones.at(pair.first).Matrix.a3, bones.at(pair.first).Matrix.b3, bones.at(pair.first).Matrix.c3, bones.at(pair.first).Matrix.d3,
                bones.at(pair.first).Matrix.a4, bones.at(pair.first).Matrix.b4, bones.at(pair.first).Matrix.c4, bones.at(pair.first).Matrix.d4
            );

            DirectX::XMStoreFloat4x4(
                &cbBone->BoneMatrix[index],
                DirectX::XMMatrixTranspose(mat)
            );
        }

        ctx->Unmap(boneConstantBuffer, 0);

        for (UINT m = 0; m < numMeshes; ++m)
        {
            UINT numVertices = modelScene->mMeshes[m]->mNumVertices;

            computeSkinningManager->Dispatch(
                boneConstantBuffer,
                skinInputSRV[m],
                skinOutputUAV[m],
                numVertices
            );

            ctx->CopyResource(staging[m], skinOutputBuffer[m]);

            D3D11_MAPPED_SUBRESOURCE mapped{};
            HRESULT hr = ctx->Map(staging[m], 0, D3D11_MAP_READ, 0, &mapped);
            assert(SUCCEEDED(hr));

            size_t bytes = sizeof(VERTEX_SKIN_OUT) * numVertices;
            fwrite(mapped.pData, 1, bytes, f);

            ctx->Unmap(staging[m], 0);
        }
    }

    fclose(f);

    for (auto b : staging)
    {
        if (b) b->Release();
    }

    return true;
}
