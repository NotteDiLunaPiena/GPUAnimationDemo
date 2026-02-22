#pragma once

#include <vector>
#include <list>
#include "gameObject.h"

class Scene 
{
private:
	std::list<GameObject*> m_GameObject[3];	//static関数内で使用するものにはstaticをつける

public:

	virtual void Init();		//初期化
	virtual void Uninit();	//終了
	virtual void Update();	//更新
	virtual void Draw();		//描画


	template <typename T>	//テンプレート debugしにくい＋コンパイル時間長くなる
	T* AddGameObject(int Layer) {		//ヘッダー側で作成→インライン関数
		T* gameObject = new T();
		gameObject->Init();
		m_GameObject[Layer].push_back(gameObject);

		return gameObject;
	}

	template <typename T>
	T* GetGameObject() {
		for (int i = 0; i < 3; i++) {	//レイヤー分ループ
			for (auto gameObject : m_GameObject[i]) {	//範囲forループ
				T* find = dynamic_cast<T*>(gameObject);	//ダイナミックキャスト
				if (find != nullptr) {
					return find;	//見つかったら返す
				}
			}
		}

		return nullptr;	//見つからなかったらnullptrを返す
	}

	template <typename T>
	std::vector<T*>GetGameObjects() {

		std::vector<T*>finds;

		for (int i = 0; i < 3; i++) {	//レイヤー分ループ
			for (auto gameObject : m_GameObject[i]) {	//範囲forループ
				T* find = dynamic_cast<T*>(gameObject);	//ダイナミックキャスト
				if (find != nullptr) {
					finds.push_back(find);	//見つかったものをvectorに追加
				}
			}
		}

		return finds;	//見つかったものを返す
	}

};