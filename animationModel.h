#pragma once

#include "main.h"
#include <unordered_map>
#include <vector>
#include <string>

#include <d3d11.h>
#include <DirectXMath.h>

#include "assimp/cimport.h"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include "assimp/matrix4x4.h"
#pragma comment (lib, "assimp-vc143-mt.lib")

#include "component.h"
#include "renderer.h"

class ModelResource;
class AnimationPlayer;
class Player;
class InstanceDataManager;
class ComputeSkinningManager;
class AnimationBakeManager;

constexpr UINT MAX_MESHES = 32;
constexpr UINT MAX_INSTANCE_COUNT = 3000;
constexpr UINT MAX_BONE_COUNT = 256;
constexpr UINT SKINNING_THREAD_GROUP_SIZE = 256;
constexpr UINT MAX_BONE_INFLUENCE = 4;

struct BONE
{
    aiMatrix4x4 Matrix;
    aiMatrix4x4 AnimationMatrix;
    aiMatrix4x4 OffsetMatrix;
};

enum class SkinningMode
{
    ComputeShader,
    VertexShader
};

class AnimationModel : public Component
{
private:
    const aiScene* m_AiScene = nullptr;

    // 頂点・インデックスバッファ
    ID3D11Buffer** m_VertexBuffer = nullptr;
    ID3D11Buffer** m_IndexBuffer = nullptr;

    // CS スキニング用
    ID3D11Buffer* m_BoneConstantBuffer = nullptr;
    ID3D11Buffer** m_SkinInputBuffer = nullptr;
    ID3D11ShaderResourceView** m_SkinInputSRV = nullptr;
    ID3D11Buffer** m_SkinOutputBuffer = nullptr;
    ID3D11UnorderedAccessView** m_SkinOutputUAV = nullptr;

    ComputeSkinningManager* m_ComputeSkinningManager = nullptr;

	// インスタンスデータ管理
    InstanceDataManager* m_InstanceManager = nullptr;

    ID3D11Buffer* m_SkinOutputBuffer_Idle[MAX_MESHES];
    ID3D11Buffer* m_SkinOutputBuffer_Run[MAX_MESHES];

    // CS パス用シェーダー
    ID3D11InputLayout* m_CS_VertexLayout = nullptr;
    ID3D11VertexShader* m_CS_VertexShader = nullptr;
    ID3D11PixelShader* m_CS_PixelShader = nullptr;
    ID3D11Buffer** m_CS_VertexBufferGPU = nullptr;

    // VS パス用シェーダー
    ID3D11InputLayout* m_VS_VertexLayout = nullptr;
    ID3D11VertexShader* m_VS_VertexShader = nullptr;
    ID3D11PixelShader* m_VS_PixelShader = nullptr;

    // パス用 StructuredBuffer（全フレーム分のボーン行列）
    ID3D11Buffer* m_VS_BoneMatrixBuffer_Idle = nullptr;
    ID3D11ShaderResourceView* m_VS_BoneMatrixSRV_Idle = nullptr;
    ID3D11Buffer* m_VS_BoneMatrixBuffer_Run = nullptr;
    ID3D11ShaderResourceView* m_VS_BoneMatrixSRV_Run = nullptr;

    SkinningMode m_SkinningMode = SkinningMode::ComputeShader;

    ModelResource* m_Resource = nullptr;
    AnimationPlayer* m_Player = nullptr;

    AnimationBakeManager* m_BakeManager = nullptr;

    // 内部処理
    void CreateComputeSkinningBuffers(VERTEX_3D* vertices, UINT vertexCount, unsigned int meshIndex);
    XMMATRIX ConvertAiMatrixToXMMatrix(const aiMatrix4x4& m);
    VERTEX_3D* GetVerticesFromMesh(aiMesh* mesh, unsigned int meshIndex);

    //VSパス用：アニメーション名の全フレームボーン行列をStructuredBufferに焼く
    void BuildVSBoneMatrixBuffer(const char* AnimName);

    // ベイク関連
    std::unordered_map<std::string, std::vector<std::vector<ID3D11Buffer*>>> m_BakedBuffers;
    std::unordered_map<std::string, int> m_LastBakedFrame;
    void ReleaseBakedAnimations();

    int m_ComputeDispatchCount = 0;
    std::unordered_map<std::string, bool> m_AnimationUsedBaked;

    void DrawCS();
    void DrawVS();

    void DrawCSAnimation(
        unsigned int meshIndex,
        aiMesh* mesh,
        const char* animName,
        ID3D11Buffer* instanceBuffer,
        UINT instanceCount
    );

    void DrawVSAnimation(
        unsigned int meshIndex,
        aiMesh* mesh,
        ID3D11Buffer* instanceBuffer,
        UINT instanceCount,
        ID3D11ShaderResourceView* boneSRV
    );

public:
    AnimationModel() : m_Resource(nullptr) {}

    void Load(const char* FileName);
    void LoadAnimation(const char* FileName, const char* Name);

    void Update(const char* AnimationName1, int Frame1,const char* AnimationName2, int Frame2, float BlendRate);
    
    void UpdateInstanceData(const std::vector<Player*>& players, const XMMATRIX& view, const XMMATRIX& proj);
    void UpdateInstanceData(const std::vector<Player*>& players);
    
    void Draw() override;
    void Uninit() override;

    void SetSkinningMode(SkinningMode mode) { m_SkinningMode = mode; }
    SkinningMode GetSkinningMode() const { return m_SkinningMode; }

    int GetTotalInstanceCount() const;
    int  GetMeshCount() const;

    void BakeAnimationToDisk(const char* AnimName, const char* OutFilePath);
    bool LoadBakedAnimation(const char* FilePath, const char* AnimName);

    void ResetDebugCounters();
    int  GetComputeDispatchCount() const;
    bool WasAnimationBakedThisFrame(const char* AnimName) const;
};