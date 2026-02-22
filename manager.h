#pragma once


class Manager
{
private:
	static class Scene* m_Scene;	//シーン管理
	static class Scene* m_SceneNext;	//次のシーン管理

public:
	static void Init();		//初期化
	static void Uninit();	//終了
	static void Update();	//更新
	static void Draw();		//描画

	static Scene* GetScene() { return m_Scene; }	//シーン取得

	template <typename T>
	static void SetScene() {
		m_SceneNext = new T();	//シーンを設定
	}

};