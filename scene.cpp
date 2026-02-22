#include "main.h"
#include "manager.h"
#include "renderer.h"

#include "camera.h"
#include "scene.h"
#include <iostream>


void Scene::Init()
{

}


void Scene::Uninit()
{
	for (int i = 0; i < 3; i++) //レイヤー分ループ
	{
		for (auto gameObject : m_GameObject[i]) //範囲forループ
		{
			gameObject->Uninit();	//ポリモフィズム
			//	→同じものを呼んでいるように見えて中身は別々の関数が呼ばれてる。
			delete gameObject;
		}
		m_GameObject[i].clear();
	}

}

void Scene::Update()
{
	for (int i = 0; i < 3; i++) //レイヤー分ループ
	{
		for (auto gameObject : m_GameObject[i]) //範囲forループ
		{
			gameObject->Update();	//ポリモフィズム
			//	→同じものを呼んでいるように見えて中身は別々の関数が呼ばれてる。


		}
	}
	for (int i = 0; i < 3; i++) //レイヤー分ループ
	{
		//ラムダ式
		m_GameObject[i].remove_if([](GameObject* gameObject)
		{
			return gameObject->Destroy(); //破棄フラグが立っているものを削除
		});
	}
}

void Scene::Draw()
{

	//Zソート
	Camera* camera = GetGameObject<Camera>();

	if (camera == nullptr) {
		std::cout << "camera is nullptr" << std::endl;
	}
	else {
		std::cout << "camera exists: " << typeid(*camera).name() << std::endl;
		Vector3 cameraPosition = camera->GetPosition(); // ← ここでクラッシュしてた
		
		if (camera != nullptr) {
			m_GameObject[1].sort([&](GameObject* object1, GameObject* object2) {
				return object1->GetDistance(cameraPosition)
							> object2->GetDistance(cameraPosition);

			});
		}
	}

	

	for (int i = 0; i < 3; i++) //レイヤー分ループ
	{
		for (auto gameObject : m_GameObject[i]) //範囲forループ
		{
			gameObject->Draw();	//ポリモフィズム
			//	→同じものを呼んでいるように見えて中身は別々の関数が呼ばれてる。

		}
	}
	
}
