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
    const aiScene* m_AiSceneLOD1 = nullptr; // 追加
    const aiScene* m_AiSceneLOD2 = nullptr; // 追加
    int m_IdleCount = 0;
    int m_RunCount = 0;

    // 頂点・インデックスバッファ
    ID3D11Buffer** m_VertexBuffer = nullptr;
    ID3D11Buffer** m_IndexBuffer = nullptr;
    std::unordered_map<std::string, ID3D11ShaderResourceView*> m_Texture;

    // CS スキニング用
    ID3D11Buffer* m_BoneConstantBuffer = nullptr;
    ID3D11Buffer** m_SkinInputBuffer = nullptr;
    ID3D11ShaderResourceView** m_SkinInputSRV = nullptr;
    ID3D11Buffer** m_SkinOutputBuffer = nullptr;
    ID3D11UnorderedAccessView** m_SkinOutputUAV = nullptr;
    ID3D11ComputeShader* m_SkinningCS = nullptr;

    // インスタンスバッファ
    ID3D11Buffer* m_InstanceBuffer = nullptr;
    std::vector<InstanceData> m_InstanceData;
    ID3D11Buffer* m_InstanceBufferIdle = nullptr;
    std::vector<InstanceData> m_InstanceDataIdle;
    ID3D11Buffer* m_InstanceBufferRun = nullptr;
    std::vector<InstanceData> m_InstanceDataRun;

    ID3D11Buffer* m_SkinOutputBuffer_Idle[MAX_MESHES];
    ID3D11Buffer* m_SkinOutputBuffer_Run[MAX_MESHES];
    int m_InstanceCount;

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

    // LOD用リソース
    ModelResource* m_ResourceLOD1 = nullptr;
    ModelResource* m_ResourceLOD2 = nullptr;
    ID3D11Buffer** m_VertexBufferLOD1 = nullptr;
    ID3D11Buffer** m_IndexBufferLOD1 = nullptr;
    ID3D11Buffer** m_VertexBufferLOD2 = nullptr;
    ID3D11Buffer** m_IndexBufferLOD2 = nullptr;

    // LOD別インスタンスデータ
    std::vector<InstanceData> m_InstanceDataIdle_LOD1;
    std::vector<InstanceData> m_InstanceDataRun_LOD1;
    std::vector<InstanceData> m_InstanceDataIdle_LOD2;
    std::vector<InstanceData> m_InstanceDataRun_LOD2;
    ID3D11Buffer* m_InstanceBufferIdle_LOD1 = nullptr;
    ID3D11Buffer* m_InstanceBufferRun_LOD1 = nullptr;
    ID3D11Buffer* m_InstanceBufferIdle_LOD2 = nullptr;
    ID3D11Buffer* m_InstanceBufferRun_LOD2 = nullptr;

    // 内部処理
    void CreateBone(aiNode* Node);
    void CreateComputeSkinningBuffers(VERTEX_3D* vertices, UINT vertexCount, unsigned int meshIndex);
    void LoadComputeShader(const char* FileName);
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

public:
    AnimationModel() : m_Resource(nullptr) {}

    void Load(const char* FileName);
    void LoadAnimation(const char* FileName, const char* Name);

    // LOD 用のカウント取得（追加）
    int GetLOD1MeshCount() const;
    int GetLOD2MeshCount() const;
    int GetLOD1PolygonCount() const;
    int GetLOD2PolygonCount() const;

    void Update(const char* AnimationName1, int Frame1,const char* AnimationName2, int Frame2, float BlendRate);
    
    void UpdateInstanceData(const std::vector<Player*>& players, const XMMATRIX& view, const XMMATRIX& proj);
    void UpdateInstanceData(const std::vector<Player*>& players);
    
    void Draw() override;
    void Uninit() override;

    void SetSkinningMode(SkinningMode mode) { m_SkinningMode = mode; }
    SkinningMode GetSkinningMode() const { return m_SkinningMode; }

    int  GetTotalInstanceCount() const { return (int)(m_InstanceDataIdle.size() + m_InstanceDataRun.size()); }
    int  GetMeshCount() const;

    int GetTotalPolygonCount() const;
    void LoadLOD(const char* FileNameLOD1, const char* FileNameLOD2);
    float m_LOD1Distance = 10.0f;
    float m_LOD2Distance = 20.0f;

    void BakeAnimationToDisk(const char* AnimName, const char* OutFilePath);
    bool LoadBakedAnimation(const char* FilePath, const char* AnimName);

    void ResetDebugCounters();
    int  GetComputeDispatchCount() const;
    bool WasAnimationBakedThisFrame(const char* AnimName) const;
};