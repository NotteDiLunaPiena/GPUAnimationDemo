#pragma once

#include "scene.h"

class Game : public Scene
{
private:
	class Audio* m_BGM;
	class AnimationModel* m_SharedModel; // 共有アニメーションモデル
	class AnimationPlayer* m_ModelPlayer;
	class ModelResource* m_ModelResource;

	bool m_SpacePrev = false; // 前フレームのスペースキー状態

private:
	void Init();	//初期化
	void Uninit();	//終了
	void Update();	//更新
	void Draw();	//描画

	int m_GlobalFrame = 0;

public:
	// 敵が破壊されたときに呼ばれる
	void OnEnemyDestroyed();
};
