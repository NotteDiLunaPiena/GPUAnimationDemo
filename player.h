#pragma once

#include "gameObject.h"
#include <string>

class Player : public GameObject
{
private:
    class AnimationModel* m_SharedModel = nullptr;   // 共有アニメーションモデル
    std::string m_AnimationName;                     // 再生アニメーション名
    std::string m_AnimationNameNext;                 // 次に再生するアニメーション名

    int m_ID = 0;                                    // プレイヤーID
    int m_Frame = 0;
    bool m_IsRunning = false;

public:
    void Init() override;
    void Uninit() override;
    void Update() override;

    void SetSharedModel(AnimationModel* sharedModel) { m_SharedModel = sharedModel; }

    const std::string& GetAnimationName() const { return m_AnimationName; }
    const std::string& GetNextAnimationName() const { return m_AnimationNameNext; }

    void SetFrame(int frame) { m_Frame = frame; }
    int GetFrame() const { return m_Frame; }

    bool IsRunning() const { return m_IsRunning; }

    void SetRunning(bool run) { m_IsRunning = run; }

    void SetID(int id) { m_ID = id; }
};
