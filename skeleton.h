#pragma once
#include "assimp/scene.h"

// ボーン情報構造体
struct BONE
{
    aiMatrix4x4 Matrix;           // 現在の最終ボーン行列
    aiMatrix4x4 AnimationMatrix;  // アニメーション変換行列（位置・回転・スケール）
    aiMatrix4x4 OffsetMatrix;     // モデル空間→ボーン空間への逆変換
};
