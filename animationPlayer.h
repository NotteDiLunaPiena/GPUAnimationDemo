#pragma once

#include "assimp/cimport.h"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include "assimp/matrix4x4.h"
#include <unordered_map>
#include "animationModel.h"
#pragma comment (lib, "assimp-vc143-mt.lib")

#include <string>
#include <vector>
#include <DirectXMath.h>
#include <map>

class ModelResource;
class AnimationModel;

/*********************************************************************
時間的な状態を持つ再生担当
・現在のアニメーション
・フレーム
・ブレンド状態
・ボーン行列計算
**********************************************************************/

class AnimationPlayer {
private:
    ModelResource* m_Resource = nullptr;
    std::unordered_map<std::string, BONE>* m_BoneMap = nullptr;

    std::string m_CurrentAnim;
    float m_CurrentTime = 0.0f;
    bool m_Loop = true;

    std::vector<DirectX::XMFLOAT4X4> m_FinalBoneMatrices;
    

    void UpdateBoneMatrix(aiNode* Node, aiMatrix4x4 ParentMatrix);
    void UpdateLocalAnimationMatrix(aiNode* node);

    static const aiNodeAnim* FindNodeAnim(const aiAnimation* animation, const std::string& nodeName);
    static void CalcInterpolatedPosition(aiVector3D& out, float animationTime, const aiNodeAnim* nodeAnim);
    static void CalcInterpolatedRotation(aiQuaternion& out, float animationTime, const aiNodeAnim* nodeAnim);
    static void CalcInterpolatedScaling(aiVector3D& out, float animationTime, const aiNodeAnim* nodeAnim);
    static aiMatrix4x4 MakeTransformMatrix(const aiVector3D& translation, const aiQuaternion& rotation, const aiVector3D& scaling);
    static XMFLOAT4X4 ToXMFLOAT4X4(const aiMatrix4x4& m);


public:
    void init(ModelResource* resource);

    void Play(const char* animName, bool loop = true);
    void Update(float deltaTime);

    void CalculateBoneTransform(const std::string& animName, float time);
    void CalculateBoneTransformBlended(const char* anim1, int frame1, const char* anim2, int frame2, float blendRate);

    const std::vector<DirectX::XMFLOAT4X4>& GetFinalBoneMatrices() const;

    //アニメーション情報取得
    int GetAnimationDuration(const char* AnimationName) const;
    float GetAnimationCurrentFramePercentage(const char* AnimationName, int CurrentFrame) const;

    //アニメーション操作
    void AdvanceFrame(const char* AnimationName, int& CurrentFrame);
    bool IsAnimationEnd(const char* AnimationName, int CurrentFrame);


};