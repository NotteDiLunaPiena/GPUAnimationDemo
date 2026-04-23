#pragma once
#include "animationModel.h"

/*********************************************************************
時間的な状態を持つ再生担当
・現在のアニメーション
・フレーム
・ブレンド状態
・ボーン行列計算
**********************************************************************/


class AnimationPlayer {
private:
	const aiScene* m_ModelScene = nullptr;
	ModelResource* m_Resource = nullptr;

	std::unordered_map<std::string, BONE>         m_Bone;
	std::unordered_map<std::string, unsigned int> m_BoneNameToIndex;

	void UpdateLocalAnimationMatrix(aiNode* node);
	void UpdateBoneMatrix(aiNode* node, const aiMatrix4x4& parentMatrix);

public:
	void Init(const aiScene* modelScene, ModelResource* resource);
	void Uninit();

	void Update(const char* Anim1, int Frame1, const char* Anim2, int Frame2, float BlendRate);
	void AdvanceFrame(const char* AnimationName, int& CurrentFrame) const;
	bool IsAnimationEnd(const char* AnimationName, int CurrentFrame) const;
	int  GetAnimationDuration(const char* AnimationName) const;
	float GetAnimationCurrentFramePercentage(const char* AnimationName, int CurrentFrame) const;
	
	const std::unordered_map<std::string, BONE>& GetBones() const { return m_Bone; }
	const std::unordered_map<std::string, unsigned int>& GetBoneNameToIndex() const { return m_BoneNameToIndex; }


};