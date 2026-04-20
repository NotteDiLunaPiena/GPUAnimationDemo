#include "animationPlayer.h"
#include "modelResource.h"


void AnimationPlayer::CalculateBoneTransform(const std::string& animName, float time)
{
    if (!m_Resource) return;

    const aiScene* scene = m_Resource->GetAnimationScene(animName.c_str());
    if (!scene || !scene->HasAnimations()) {
        // アニメーションが無ければノードのデフォルト変換を使って更新
        UpdateLocalAnimationMatrix(m_Resource->GetScene()->mRootNode);
        UpdateBoneMatrix(m_Resource->GetScene()->mRootNode, aiMatrix4x4());
    }
    else {
        const aiAnimation* animation = scene->mAnimations[0];
        // ticks per second の取得（0 の場合デフォルト値を使用）
        double ticksPerSecond = animation->mTicksPerSecond != 0.0 ? animation->mTicksPerSecond : 25.0;
        // time は秒単位と仮定。必要に応じて呼び出し側を合わせる
        double timeInTicks = time * ticksPerSecond;
        double animationTime = fmod(timeInTicks, animation->mDuration);

        // まず全ノードをデフォルトで初期化（アニメ無いノードのため）
        UpdateLocalAnimationMatrix(m_Resource->GetScene()->mRootNode);

        // チャネル毎に補間してローカル行列を設定
        for (unsigned int i = 0; i < animation->mNumChannels; ++i) {
            const aiNodeAnim* channel = animation->mChannels[i];
            if (!channel) continue;
            std::string nodeName = channel->mNodeName.C_Str();

            // 補間値を算出
            aiVector3D translation;
            aiQuaternion rotation;
            aiVector3D scaling;
            CalcInterpolatedPosition(translation, (float)animationTime, channel);
            CalcInterpolatedRotation(rotation, (float)animationTime, channel);
            CalcInterpolatedScaling(scaling, (float)animationTime, channel);

            // 合成行列を作り、ボーンマップが持つ該当ノードの AnimationMatrix をセット
            aiMatrix4x4 localTransform = MakeTransformMatrix(translation, rotation, scaling);

            if (m_BoneMap->count(nodeName)) {
                BONE& bone = m_BoneMap->at(nodeName);
                bone.AnimationMatrix = localTransform;
            }
            else {
                // ノードがボーンに対応しない場合は何もしない（または必要なら記録）
            }
        }

        // ルートから再帰して最終行列を計算
        UpdateBoneMatrix(m_Resource->GetScene()->mRootNode, aiMatrix4x4());
    }

    // m_FinalBoneMatrices を更新（ボーン名 -> インデックス を使う）
    const auto& boneNameToIndex = m_Resource->GetBoneNameToIndex();
    for (const auto& kv : *m_BoneMap) {
        const std::string& name = kv.first;
        const BONE& bone = kv.second;
        auto it = boneNameToIndex.find(name);
        if (it == boneNameToIndex.end()) continue;
        unsigned int index = it->second;
        if (index < m_FinalBoneMatrices.size()) {
            m_FinalBoneMatrices[index] = ToXMFLOAT4X4(bone.Matrix);
        }
    }
}

void AnimationPlayer::CalculateBoneTransformBlended(const char* anim1, int frame1, const char* anim2, int frame2, float blendRate)
{
    if (!m_Resource || !m_BoneMap) return;
    // フレーム指定なのでアニメシーンから aiAnimation を取得
    const aiScene* scene1 = (anim1 && *anim1) ? m_Resource->GetAnimationScene(anim1) : nullptr;
    const aiScene* scene2 = (anim2 && *anim2) ? m_Resource->GetAnimationScene(anim2) : nullptr;
    const aiAnimation* a1 = (scene1 && scene1->HasAnimations()) ? scene1->mAnimations[0] : nullptr;
    const aiAnimation* a2 = (scene2 && scene2->HasAnimations()) ? scene2->mAnimations[0] : nullptr;

    // 初期化：ノードのデフォルトを入れておく
    UpdateLocalAnimationMatrix(m_Resource->GetScene()->mRootNode);

    // 各ボーンについてフレームインデックスに基づく値を取り出してブレンド
    for (auto& pair : *m_BoneMap) {
        const std::string& boneName = pair.first;
        BONE& bone = pair.second;

        aiQuaternion rot1, rot2;
        aiVector3D pos1(0, 0, 0), pos2(0, 0, 0);
        aiNodeAnim* nodeAnim1 = nullptr;
        aiNodeAnim* nodeAnim2 = nullptr;

        if (a1) {
            nodeAnim1 = const_cast<aiNodeAnim*>(FindNodeAnim(a1, boneName));
            if (nodeAnim1 && nodeAnim1->mNumRotationKeys > 0) {
                int idx = (nodeAnim1->mNumRotationKeys > 0) ? (frame1 % (int)nodeAnim1->mNumRotationKeys) : 0;
                rot1 = nodeAnim1->mRotationKeys[idx].mValue;
            }
            if (nodeAnim1 && nodeAnim1->mNumPositionKeys > 0) {
                int pidx = nodeAnim1->mNumPositionKeys > 0 ? (frame1 % (int)nodeAnim1->mNumPositionKeys) : 0;
                pos1 = nodeAnim1->mPositionKeys[pidx].mValue;
            }
        }

        if (a2) {
            nodeAnim2 = const_cast<aiNodeAnim*>(FindNodeAnim(a2, boneName));
            if (nodeAnim2 && nodeAnim2->mNumRotationKeys > 0) {
                int idx = (nodeAnim2->mNumRotationKeys > 0) ? (frame2 % (int)nodeAnim2->mNumRotationKeys) : 0;
                rot2 = nodeAnim2->mRotationKeys[idx].mValue;
            }
            if (nodeAnim2 && nodeAnim2->mNumPositionKeys > 0) {
                int pidx = nodeAnim2->mNumPositionKeys > 0 ? (frame2 % (int)nodeAnim2->mNumPositionKeys) : 0;
                pos2 = nodeAnim2->mPositionKeys[pidx].mValue;
            }
        }

        // ブレンド（欠けている側はもう片方の値を使う）
        aiVector3D posBlend = pos1 * (1.0f - blendRate) + pos2 * blendRate;
        aiQuaternion rotBlend;
        aiQuaternion::Interpolate(rotBlend, rot1, rot2, blendRate);
        rotBlend.Normalize();

        bone.AnimationMatrix = aiMatrix4x4(aiVector3D(1, 1, 1), rotBlend, posBlend);
    }

    // 最終行列を計算
    aiMatrix4x4 identity;
    UpdateBoneMatrix(m_Resource->GetScene()->mRootNode, identity);

    // m_FinalBoneMatrices 更新
    const auto& mapNameToIndex = m_Resource->GetBoneNameToIndex();
    for (const auto& kv : *m_BoneMap) {
        const std::string& name = kv.first;
        const BONE& b = kv.second;
        auto it = mapNameToIndex.find(name);
        if (it == mapNameToIndex.end()) continue;
        unsigned int idx = it->second;
        if (idx < m_FinalBoneMatrices.size()) m_FinalBoneMatrices[idx] = ToXMFLOAT4X4(b.Matrix);
    }
}

void AnimationPlayer::init(ModelResource* resource)
{
	m_Resource = resource;
	m_BoneMap = &m_Resource->GetBones();
	const auto& bones = m_Resource->GetBones();
	m_FinalBoneMatrices.resize(bones.size());
}

void AnimationPlayer::Play(const char* animName, bool loop)
{
}

void AnimationPlayer::Update(float deltaTime)
{
}

const std::vector<DirectX::XMFLOAT4X4>& AnimationPlayer::GetFinalBoneMatrices() const
{
	return m_FinalBoneMatrices;
}

// アニメーションが最終フレームに到達したか判定
bool AnimationPlayer ::IsAnimationEnd(const char* AnimationName, int CurrentFrame)
{
	int duration = GetAnimationDuration(AnimationName);
	if (duration <= 0) return true;

	return (CurrentFrame >= duration - 1);
}

// 指定アニメーションの総フレーム数を取得
int AnimationPlayer::GetAnimationDuration(const char* AnimationName) const
{
	const aiScene* scene = m_Resource->GetAnimationScene(AnimationName);
	if (!scene || !scene->HasAnimations()) return 0;
	return (int)scene->mAnimations[0]->mDuration;
}

// 現在フレームの進行率（0.0〜1.0）を取得
float AnimationPlayer::GetAnimationCurrentFramePercentage(const char* AnimationName, int CurrentFrame) const
{
	int duration = GetAnimationDuration(AnimationName);
	if (duration <= 0) return 0.0f;
	int normalized = CurrentFrame % duration;
	return (float)normalized / duration;
}

// アニメーションフレームを1進める（ループ）
void AnimationPlayer::AdvanceFrame(const char* AnimationName, int& CurrentFrame)
{
	int duration = GetAnimationDuration(AnimationName);
	if (duration <= 0) return;

	CurrentFrame = (CurrentFrame + 1) % duration;
}

// ボーン行列更新
void AnimationPlayer::UpdateBoneMatrix(aiNode* node, aiMatrix4x4 ParentMatrix)
{
	BONE* bone = &(*m_BoneMap)[node->mName.C_Str()];

	//親の行列と自身のアニメーション行列を合成
	aiMatrix4x4 GlobalTransform = ParentMatrix * bone->AnimationMatrix;

	//スキニング用の最終ボーン行列を計算
	bone->Matrix = GlobalTransform * bone->OffsetMatrix;

	//子ノードへ再帰
	for (unsigned int n = 0; n < node->mNumChildren; n++)
		UpdateBoneMatrix(node->mChildren[n], GlobalTransform);
}

// ローカルアニメーション行列更新　（アニメーション適用前の初期化）
void AnimationPlayer::UpdateLocalAnimationMatrix(aiNode* node)
{

	// ボーンがマップに存在する場合のみ
	if (m_BoneMap->count(node->mName.C_Str()))
	{
		BONE& bone = m_BoneMap->at(node->mName.C_Str());

		bone.AnimationMatrix = node->mTransformation;
	}

	// 子ノードへ再帰
	for (unsigned int n = 0; n < node->mNumChildren; n++)
		UpdateLocalAnimationMatrix(node->mChildren[n]);
}

// 指定ノード名に対応する aiNodeAnim を探す
 const aiNodeAnim* AnimationPlayer::FindNodeAnim(const aiAnimation* animation, const std::string& nodeName)
{
    if (!animation) return nullptr;
    for (unsigned int i = 0; i < animation->mNumChannels; ++i) {
        const aiNodeAnim* channel = animation->mChannels[i];
        if (channel && nodeName == channel->mNodeName.C_Str()) return channel;
    }
    return nullptr;
}

// 位置補間
void AnimationPlayer::CalcInterpolatedPosition(aiVector3D& out, float animationTime, const aiNodeAnim* nodeAnim)
{
    if (!nodeAnim || nodeAnim->mNumPositionKeys == 0) { out = aiVector3D(0,0,0); return; }
    if (nodeAnim->mNumPositionKeys == 1) { out = nodeAnim->mPositionKeys[0].mValue; return; }

    unsigned int index = 0;
    for (unsigned int i = 0; i + 1 < nodeAnim->mNumPositionKeys; ++i) {
        if (animationTime < (float)nodeAnim->mPositionKeys[i+1].mTime) { index = i; break; }
    }
    unsigned int nextIndex = index + 1;
    float t1 = (float)nodeAnim->mPositionKeys[index].mTime;
    float t2 = (float)nodeAnim->mPositionKeys[nextIndex].mTime;
    float factor = (t2 - t1) > 0.0f ? (animationTime - t1) / (t2 - t1) : 0.0f;
    const aiVector3D& start = nodeAnim->mPositionKeys[index].mValue;
    const aiVector3D& end = nodeAnim->mPositionKeys[nextIndex].mValue;
    out = start + (end - start) * factor;
}

// 回転補間
void AnimationPlayer::CalcInterpolatedRotation(aiQuaternion& out, float animationTime, const aiNodeAnim* nodeAnim)
{
    if (!nodeAnim || nodeAnim->mNumRotationKeys == 0) { out = aiQuaternion(); return; }
    if (nodeAnim->mNumRotationKeys == 1) { out = nodeAnim->mRotationKeys[0].mValue; return; }

    unsigned int index = 0;
    for (unsigned int i = 0; i + 1 < nodeAnim->mNumRotationKeys; ++i) {
        if (animationTime < (float)nodeAnim->mRotationKeys[i+1].mTime) { index = i; break; }
    }
    unsigned int nextIndex = index + 1;
    float t1 = (float)nodeAnim->mRotationKeys[index].mTime;
    float t2 = (float)nodeAnim->mRotationKeys[nextIndex].mTime;
    float factor = (t2 - t1) > 0.0f ? (animationTime - t1) / (t2 - t1) : 0.0f;
    const aiQuaternion& start = nodeAnim->mRotationKeys[index].mValue;
    const aiQuaternion& end = nodeAnim->mRotationKeys[nextIndex].mValue;
    aiQuaternion::Interpolate(out, start, end, factor);
    out.Normalize();
}

// スケール補間
void AnimationPlayer::CalcInterpolatedScaling(aiVector3D& out, float animationTime, const aiNodeAnim* nodeAnim)
{
    if (!nodeAnim || nodeAnim->mNumScalingKeys == 0) { out = aiVector3D(1,1,1); return; }
    if (nodeAnim->mNumScalingKeys == 1) { out = nodeAnim->mScalingKeys[0].mValue; return; }

    unsigned int index = 0;
    for (unsigned int i = 0; i + 1 < nodeAnim->mNumScalingKeys; ++i) {
        if (animationTime < (float)nodeAnim->mScalingKeys[i+1].mTime) { index = i; break; }
    }
    unsigned int nextIndex = index + 1;
    float t1 = (float)nodeAnim->mScalingKeys[index].mTime;
    float t2 = (float)nodeAnim->mScalingKeys[nextIndex].mTime;
    float factor = (t2 - t1) > 0.0f ? (animationTime - t1) / (t2 - t1) : 0.0f;
    const aiVector3D& start = nodeAnim->mScalingKeys[index].mValue;
    const aiVector3D& end = nodeAnim->mScalingKeys[nextIndex].mValue;
    out = start + (end - start) * factor;
}

// aiMatrix3x3 -> aiMatrix4x4 の埋め込み
aiMatrix4x4 AnimationPlayer::MakeTransformMatrix(const aiVector3D& translation, const aiQuaternion& rotation, const aiVector3D& scaling)
{
    aiMatrix4x4 T; // identity
    T.a1 = 1.f; T.b2 = 1.f; T.c3 = 1.f; T.d4 = 1.f;

    // Translation
    T.a4 = translation.x;
    T.b4 = translation.y;
    T.c4 = translation.z;

    // Rotation 3x3
    aiMatrix3x3 rot = rotation.GetMatrix();
    aiMatrix4x4 R;
    R.a1 = rot.a1; R.a2 = rot.a2; R.a3 = rot.a3; R.a4 = 0.f;
    R.b1 = rot.b1; R.b2 = rot.b2; R.b3 = rot.b3; R.b4 = 0.f;
    R.c1 = rot.c1; R.c2 = rot.c2; R.c3 = rot.c3; R.c4 = 0.f;
    R.d1 = 0.f;    R.d2 = 0.f;    R.d3 = 0.f;    R.d4 = 1.f;

    // Scaling
    aiMatrix4x4 S;
    S.a1 = scaling.x; S.a2 = 0.f;       S.a3 = 0.f;       S.a4 = 0.f;
    S.b1 = 0.f;       S.b2 = scaling.y; S.b3 = 0.f;       S.b4 = 0.f;
    S.c1 = 0.f;       S.c2 = 0.f;       S.c3 = scaling.z; S.c4 = 0.f;
    S.d1 = 0.f;       S.d2 = 0.f;       S.d3 = 0.f;       S.d4 = 1.f;

    // 合成: T * R * S を返す（ノードローカル変換）
    return T * R * S;
}

// aiMatrix4x4 -> DirectX::XMFLOAT4X4 変換
XMFLOAT4X4 AnimationPlayer::ToXMFLOAT4X4(const aiMatrix4x4& m)
{
    DirectX::XMFLOAT4X4 xm;
    xm._11 = m.a1; xm._12 = m.a2; xm._13 = m.a3; xm._14 = m.a4;
    xm._21 = m.b1; xm._22 = m.b2; xm._23 = m.b3; xm._24 = m.b4;
    xm._31 = m.c1; xm._32 = m.c2; xm._33 = m.c3; xm._34 = m.c4;
    xm._41 = m.d1; xm._42 = m.d2; xm._43 = m.d3; xm._44 = m.d4;
    return xm;
}

