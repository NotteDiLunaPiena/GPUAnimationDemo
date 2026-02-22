#pragma once

#include "assimp/cimport.h"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include "assimp/matrix4x4.h"
#include <unordered_map>
#include "animationModel.h"
#pragma comment (lib, "assimp-vc143-mt.lib")

/*********************************************************************
読み込み専用・不変データ
・aiScene
・メッシュ
・ボーン情報
・GPUバッファ
・アニメーションデータ
**********************************************************************/

class ModelResource
{
private:
    const aiScene* m_AiScene = nullptr;

    ID3D11Buffer** m_VertexBuffer = nullptr;
    ID3D11Buffer** m_IndexBuffer = nullptr;

    std::unordered_map<std::string, BONE> m_Bone;
    std::unordered_map<std::string, UINT> m_BoneNameToIndex;

    std::unordered_map<std::string, ID3D11ShaderResourceView*> m_Textures;

    std::unordered_map<std::string, const aiScene*> m_Animations;

    void CreateBuffers();
    void CreateBone(aiNode* node);
    void ApplyBoneWeights(aiMesh* mesh, VERTEX_3D* vertices);

public:
    const aiScene* LoadModel(const char* fileName);
    // アニメーションをロードして登録する関数
    void LoadAnimation(const char* fileName, const char* animName);

    void Uninit();

    ID3D11Buffer* GetVertexBuffer(int mesh) const { return m_VertexBuffer[mesh]; }
    ID3D11Buffer* GetIndexBuffer(int mesh) const { return m_IndexBuffer[mesh]; }
    const std::unordered_map<std::string, BONE>& GetBones() const { return m_Bone; }
    const std::unordered_map<std::string, UINT>& GetBoneNameToIndex() const { return m_BoneNameToIndex; }

    const aiScene* GetScene() const { return m_AiScene; }

    ID3D11ShaderResourceView* GetTexture(const std::string& name) {
        if (m_Textures.count(name)) return m_Textures[name];
        return nullptr;
    }

    const aiScene* GetAnimationScene(const std::string& name) const {
        if (m_Animations.count(name) > 0) {
            return m_Animations.at(name);
        }
        return nullptr; // 見つからない場合はnullptr
    }


};