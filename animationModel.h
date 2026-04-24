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

//スキニング方式の選択
enum class SkinningMode
{
    ComputeShader,
    VertexShader
};


class AnimationModel : public Component
{
private:
    // モデル・アニメーションデータ
    const aiScene* m_AiScene = nullptr;
    int m_IdleCount = 0;
    int m_RunCount = 0;

    // GPUリソース
    //メッシュごとに持つGPUバッファ配列
    ID3D11Buffer** m_VertexBuffer = nullptr;
    ID3D11Buffer** m_IndexBuffer = nullptr;
    std::unordered_map<std::string, ID3D11ShaderResourceView*> m_Texture;

    // ボーンデータ
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

    // -------------------------------------------------------
    // CS パス用シェーダー（既存）
    // -------------------------------------------------------
    ID3D11InputLayout* m_CS_VertexLayout = nullptr;  // CS出力頂点(VERTEX_SKIN_OUT)用
    ID3D11VertexShader* m_CS_VertexShader = nullptr;  // CSSkinningVS
    ID3D11PixelShader* m_CS_PixelShader = nullptr;  // CSSkinningPS
    ID3D11Buffer** m_CS_VertexBufferGPU = nullptr;

    // -------------------------------------------------------
    // VS パス用シェーダー（新規）
    // -------------------------------------------------------
    ID3D11InputLayout* m_VS_VertexLayout = nullptr; // VERTEX_3D（BoneIndex/Weight付き）用
    ID3D11VertexShader* m_VS_VertexShader = nullptr; // VSSkinningVS
    ID3D11PixelShader* m_VS_PixelShader = nullptr; // VSSkinningPS
    ID3D11Buffer* m_BoneConstantBuffer_Idle = nullptr;
    ID3D11Buffer* m_BoneConstantBuffer_Run = nullptr;

    // -------------------------------------------------------
    // 現在のスキニング方式（デフォルトは CS）
    // -------------------------------------------------------
    SkinningMode m_SkinningMode = SkinningMode::ComputeShader;

    //リソース
    ModelResource* m_Resource = nullptr;
    AnimationPlayer* m_Player = nullptr;

    // 内部処理関数
    //ボーンアニメーション関連
    void CreateBone(aiNode* Node);

    //GPUスキニング関連
    void CreateComputeSkinningBuffers(VERTEX_3D* vertices, UINT vertexCount, unsigned int meshIndex);
    void LoadComputeShader(const char* FileName);
    XMMATRIX ConvertAiMatrixToXMMatrix(const aiMatrix4x4& m);

    VERTEX_3D* GetVerticesFromMesh(aiMesh* mesh, unsigned int meshIndex);

    // AnimationModel クラス内に次のメンバ／メソッドを追加してください。
    std::unordered_map<std::string, std::vector<std::vector<ID3D11Buffer*>>> m_BakedBuffers;
    // アニメーション毎に最後に Update() でセットされたフレーム番号（描画で参照）
    std::unordered_map<std::string, int> m_LastBakedFrame;

    // 保持しているベイクバッファを解放
    void ReleaseBakedAnimations();

    int m_ComputeDispatchCount = 0; // 今フレームのCSディスパッチ回数（アニメーションUpdate内で増やす）
    std::unordered_map<std::string, bool> m_AnimationUsedBaked; 

    // 内部描画ヘルパー（モード別）
    void DrawCS();  // CS パスの描画処理
    void DrawVS();  // VS パスの描画処理


public:
    AnimationModel() :m_Resource(nullptr) {}

    //ロード
    void Load(const char* FileName);
    void LoadAnimation(const char* FileName, const char* Name);

    //更新
    void Update(const char* AnimationName1, int Frame1, const char* AnimationName2, int Frame2, float BlendRate);
    void UpdateInstanceData(const std::vector<Player*>& players);

    //描画
    void Draw() override;

    //解放
    void Uninit() override;

    // スキニング方式の切り替え
    void SetSkinningMode(SkinningMode mode) { m_SkinningMode = mode; }
    SkinningMode GetSkinningMode() const { return m_SkinningMode; }

    // インスタンス数取得
    int GetTotalInstanceCount() const { return (int)(m_InstanceDataIdle.size() + m_InstanceDataRun.size()); };

    //メッシュ数の取得
    int GetMeshCount() const;

    void BakeAnimationToDisk(const char* AnimName, const char* OutFilePath);
    // ベイクファイルを読み込む
    bool LoadBakedAnimation(const char* FilePath, const char* AnimName);

    void ResetDebugCounters();
    int GetComputeDispatchCount() const;
    bool WasAnimationBakedThisFrame(const char* AnimName) const;



};