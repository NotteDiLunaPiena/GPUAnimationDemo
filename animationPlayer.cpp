#include "main.h"
#include "renderer.h"
#include "animationPlayer.h"
#include "utility.h"
#include <d3dcompiler.h>
#include <corecrt_io.h>
#include "vector3.h"
#include "player.h"
#include "animationModel.h"
#include "modelResource.h"

// -------------------------------------------------------
//  初期化
//  ModelResource からボーン構造をコピーしてインスタンス固有の
//  ボーンマップを作成する
// -------------------------------------------------------
void AnimationPlayer::Init(const aiScene* modelScene, ModelResource* resource)
{
    assert(modelScene && resource);

    m_ModelScene = modelScene;
    m_Resource = resource;

    // ボーンインデックスマップをリソースからコピー
    m_BoneNameToIndex = resource->GetBoneNameToIndex();

    // ボーン構造を自分用に構築（AnimationMatrix / Matrix は毎フレーム上書きされる）
    m_Bone.clear();
    auto& resBones = resource->GetBones();

    // ノードツリー全体をなめてボーンエントリを作成する
    std::function<void(aiNode*)> buildBone = [&](aiNode* node)
        {
            const std::string name = node->mName.C_Str();
            BONE bone{};

            // リソース側で計算済みの OffsetMatrix をコピー
            if (resBones.count(name))
                bone.OffsetMatrix = resBones.at(name).OffsetMatrix;

            m_Bone[name] = bone;

            for (unsigned int i = 0; i < node->mNumChildren; ++i)
                buildBone(node->mChildren[i]);
        };
    buildBone(m_ModelScene->mRootNode);
}

// -------------------------------------------------------
//  解放
// -------------------------------------------------------
void AnimationPlayer::Uninit()
{
    m_Bone.clear();
    m_BoneNameToIndex.clear();
    m_ModelScene = nullptr;
    m_Resource = nullptr;
}

// -------------------------------------------------------
//  アニメーション更新（メイン）
//
//  処理フロー:
//   1. 全ボーンの AnimationMatrix をノードのデフォルトに初期化
//   2. Anim1 / Anim2 のチャンネルからフレームの pos / rot を取得
//   3. LERP(pos) / SLERP(rot) でブレンド
//   4. bone->AnimationMatrix にセット
//   5. UpdateBoneMatrix で最終スキニング行列(bone->Matrix)を計算
// -------------------------------------------------------
void AnimationPlayer::Update(const char* Anim1, int Frame1,const char* Anim2, int Frame2,float BlendRate)
{
    if (!Anim1 || !Anim2) return;

    // ModelResource 側のアニメーションマップから取得
    const aiScene* scene1 = m_Resource->GetAnimationScene(Anim1);
    const aiScene* scene2 = m_Resource->GetAnimationScene(Anim2);

    if (!scene1 || !scene2) return;
    if (!scene1->HasAnimations() || !scene2->HasAnimations()) return;

    aiAnimation* animation1 = scene1->mAnimations[0];
    aiAnimation* animation2 = scene2->mAnimations[0];

    // 1. ローカル行列初期化（デフォルトポーズに戻す）
    UpdateLocalAnimationMatrix(m_ModelScene->mRootNode);

    // 2〜4. ボーンごとにブレンド計算
    for (auto& pair : m_Bone)
    {
        BONE& bone = pair.second;
        const std::string& boneName = pair.first;
        const aiString aiName(boneName);

        // 対応するアニメーションチャンネルを検索
        aiNodeAnim* nodeAnim1 = nullptr;
        aiNodeAnim* nodeAnim2 = nullptr;

        for (unsigned int c = 0; c < animation1->mNumChannels; ++c)
            if (animation1->mChannels[c]->mNodeName == aiName)
            {
                nodeAnim1 = animation1->mChannels[c]; break;
            }

        for (unsigned int c = 0; c < animation2->mNumChannels; ++c)
            if (animation2->mChannels[c]->mNodeName == aiName)
            {
                nodeAnim2 = animation2->mChannels[c]; break;
            }

        // デフォルト値（チャンネルがない場合はアイデンティティ相当）
        aiQuaternion rot1, rot2;
        aiVector3D   pos1(0, 0, 0), pos2(0, 0, 0);

        if (nodeAnim1 && nodeAnim1->mNumRotationKeys > 0)
        {
            int f1 = Frame1 % (int)nodeAnim1->mNumRotationKeys;
            rot1 = nodeAnim1->mRotationKeys[f1].mValue;
            pos1 = nodeAnim1->mPositionKeys[f1 % (int)nodeAnim1->mNumPositionKeys].mValue;
        }

        if (nodeAnim2 && nodeAnim2->mNumRotationKeys > 0)
        {
            int f2 = Frame2 % (int)nodeAnim2->mNumRotationKeys;
            rot2 = nodeAnim2->mRotationKeys[f2].mValue;
            pos2 = nodeAnim2->mPositionKeys[f2 % (int)nodeAnim2->mNumPositionKeys].mValue;
        }

        // LERP (位置) / SLERP (回転)
        aiVector3D   blendPos = pos1 * (1.0f - BlendRate) + pos2 * BlendRate;
        aiQuaternion blendRot;
        aiQuaternion::Interpolate(blendRot, rot1, rot2, BlendRate);
        blendRot.Normalize();

        // ブレンド後のアニメーション行列をセット
        bone.AnimationMatrix = aiMatrix4x4(aiVector3D(1, 1, 1), blendRot, blendPos);
    }

    // 5. グローバル行列 → スキニング行列を再帰計算
    aiMatrix4x4 identity;
    UpdateBoneMatrix(m_ModelScene->mRootNode, identity);
}

// -------------------------------------------------------
//  フレームを 1 進める（ループ）
// -------------------------------------------------------
void AnimationPlayer::AdvanceFrame(const char* AnimationName, int& CurrentFrame) const
{
    int duration = GetAnimationDuration(AnimationName);
    if (duration <= 0) return;
    CurrentFrame = (CurrentFrame + 1) % duration;
}

// -------------------------------------------------------
//  最終フレーム判定
// -------------------------------------------------------
bool AnimationPlayer::IsAnimationEnd(const char* AnimationName, int CurrentFrame) const
{
    int duration = GetAnimationDuration(AnimationName);
    if (duration <= 0) return true;
    return (CurrentFrame >= duration - 1);
}

// -------------------------------------------------------
//  アニメーションの総フレーム数
// -------------------------------------------------------
int AnimationPlayer::GetAnimationDuration(const char* AnimationName) const
{
    const aiScene* scene = m_Resource->GetAnimationScene(AnimationName);
    if (!scene || !scene->HasAnimations()) return 0;
    return (int)scene->mAnimations[0]->mDuration;
}

// -------------------------------------------------------
//  現在フレームの進行率（0.0〜1.0）
// -------------------------------------------------------
float AnimationPlayer::GetAnimationCurrentFramePercentage(const char* AnimationName, int CurrentFrame) const
{
    int duration = GetAnimationDuration(AnimationName);
    if (duration <= 0) return 0.0f;
    return (float)(CurrentFrame % duration) / (float)duration;
}

// -------------------------------------------------------
//  ローカルアニメーション行列初期化（内部）
//  アニメーション適用前に、ノードのデフォルト変換で上書きする
// -------------------------------------------------------
void AnimationPlayer::UpdateLocalAnimationMatrix(aiNode* node)
{
    const std::string name = node->mName.C_Str();
    if (m_Bone.count(name))
        m_Bone[name].AnimationMatrix = node->mTransformation;

    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        UpdateLocalAnimationMatrix(node->mChildren[i]);
}

// -------------------------------------------------------
//  グローバルボーン行列の再帰計算（内部）
//   bone->Matrix = (ParentMatrix × AnimationMatrix) × OffsetMatrix
// -------------------------------------------------------
void AnimationPlayer::UpdateBoneMatrix(aiNode* node, const aiMatrix4x4& parentMatrix)
{
    const std::string name = node->mName.C_Str();
    BONE& bone = m_Bone[name];

    aiMatrix4x4 globalTransform = parentMatrix * bone.AnimationMatrix;
    bone.Matrix = globalTransform * bone.OffsetMatrix;

    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        UpdateBoneMatrix(node->mChildren[i], globalTransform);
}