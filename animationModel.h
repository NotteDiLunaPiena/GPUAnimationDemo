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

/*********************************************************************
静的データ管理
・モデルデータ
・メッシュ
・ボーン改装
・GPUバッファ
・描画
**********************************************************************/


//最大メッシュ数
constexpr UINT MAX_MESHES = 32;
//描画対数
constexpr UINT MAX_INSTANCE_COUNT = 3000;
//ボーン数
constexpr UINT MAX_BONE_COUNT = 256;
//スレッドグループサイズ
constexpr UINT SKINNING_THREAD_GROUP_SIZE = 256;
//ボーンウェイト数
constexpr UINT MAX_BONE_INFLUENCE = 4;


// ボーン情報構造体
struct BONE
{
    aiMatrix4x4 Matrix;           // 現在の最終ボーン行列
    aiMatrix4x4 AnimationMatrix;  // アニメーション変換行列（位置・回転・スケール）
    aiMatrix4x4 OffsetMatrix;     // モデル空間→ボーン空間への逆変換
};

class AnimationModel : public Component
{
private:
    // モデル・アニメーションデータ
    const aiScene* m_AiScene = nullptr;
    std::unordered_map<std::string, const aiScene*> m_Animation;
    int m_IdleCount = 0;
    int m_RunCount = 0;

    // GPUリソース
    //メッシュごとに持つGPUバッファ配列
    ID3D11Buffer** m_VertexBuffer = nullptr;
    ID3D11Buffer** m_IndexBuffer = nullptr;
    std::unordered_map<std::string, ID3D11ShaderResourceView*> m_Texture;

    // ボーンデータ
    std::unordered_map<std::string, BONE> m_Bone;
    std::unordered_map<std::string, unsigned int> m_BoneNameToIndex;
    ID3D11Buffer* m_BoneConstantBuffer = nullptr;

    // スキニング用バッファ
    ID3D11Buffer** m_SkinInputBuffer = nullptr;
    ID3D11ShaderResourceView** m_SkinInputSRV = nullptr;
    ID3D11Buffer** m_SkinOutputBuffer = nullptr;
    ID3D11UnorderedAccessView** m_SkinOutputUAV = nullptr;
    ID3D11ComputeShader* m_SkinningCS = nullptr;

    // インスタンス用
    ID3D11Buffer* m_InstanceBuffer = nullptr;
    std::vector<InstanceData> m_InstanceData;

    ID3D11Buffer* m_InstanceBufferIdle = nullptr;
    std::vector<InstanceData> m_InstanceDataIdle;
    ID3D11Buffer* m_InstanceBufferRun = nullptr;
    std::vector<InstanceData> m_InstanceDataRun;

    ID3D11Buffer* m_SkinOutputBuffer_Idle[MAX_MESHES];
    ID3D11Buffer* m_SkinOutputBuffer_Run[MAX_MESHES];

    //インスタンス描画評価用カウント
    int m_InstanceCount;

    // シェーダ関連
    ID3D11InputLayout* m_VertexLayout = nullptr;
    ID3D11VertexShader* m_VertexShader = nullptr;
    ID3D11PixelShader* m_PixelShader = nullptr;
    ID3D11Buffer** m_VertexBufferGPU = nullptr;

    //リソース
    ModelResource* m_Resource;

    // 内部処理関数
    //ボーンアニメーション関連
    void CreateBone(aiNode* Node);
    void UpdateBoneMatrix(aiNode* Node, aiMatrix4x4 ParentMatrix);
    void UpdateLocalAnimationMatrix(aiNode* node);

    //GPUスキニング関連
    void CreateComputeSkinningBuffers(VERTEX_3D* vertices, UINT vertexCount, unsigned int meshIndex);
    void LoadComputeShader(const char* FileName);
    XMMATRIX ConvertAiMatrixToXMMatrix(const aiMatrix4x4& m);

    VERTEX_3D* GetVerticesFromMesh(aiMesh* mesh, unsigned int meshIndex);
    AnimationPlayer* m_Player = nullptr;

    // AnimationModel クラス内に次のメンバ／メソッドを追加してください。
    // map: アニメーション名 -> [meshIndex] -> [frameIndex] -> ID3D11Buffer*
    std::unordered_map<std::string, std::vector<std::vector<ID3D11Buffer*>>> m_BakedBuffers;
    // アニメーション毎に最後に Update() でセットされたフレーム番号（描画で参照）
    std::unordered_map<std::string, int> m_LastBakedFrame;

    // 保持しているベイクバッファを解放
    void ReleaseBakedAnimations();

    int m_ComputeDispatchCount = 0; // 今フレームのCSディスパッチ回数（アニメーションUpdate内で増やす）
    std::unordered_map<std::string, bool> m_AnimationUsedBaked; // アニメ名 -> 

public:
    AnimationModel() :m_Resource(nullptr) {}

    //ロード
    void Load(const char* FileName);
    void LoadAnimation(const char* FileName, const char* Name);

    //更新
    void Update(const char* AnimationName1, int Frame1,
        const char* AnimationName2, int Frame2, float BlendRate);
    void UpdateInstanceData(const std::vector<Player*>& players);

    //描画
    void Draw() override;

    //解放
    void Uninit() override;

    //アニメーション情報取得
    int GetAnimationDuration(const char* AnimationName) const;
    float GetAnimationCurrentFramePercentage(const char* AnimationName, int CurrentFrame) const;

    //アニメーション操作
    void AdvanceFrame(const char* AnimationName, int& CurrentFrame);
    bool IsAnimationEnd(const char* AnimationName, int CurrentFrame);

    // インスタンス数取得
    int GetTotalInstanceCount() const { return (int)(m_InstanceDataIdle.size() + m_InstanceDataRun.size()); };

    //メッシュ数の取得
    int GetMeshCount() const;

    void BakeAnimationToDisk(const char* AnimName, const char* OutFilePath);
    // ベイクファイルを読み込む（ファイル形式は BakeAnimationToDisk と対応）
    bool LoadBakedAnimation(const char* FilePath, const char* AnimName);

    void ResetDebugCounters();
    int GetComputeDispatchCount() const;
    bool WasAnimationBakedThisFrame(const char* AnimName) const;

};