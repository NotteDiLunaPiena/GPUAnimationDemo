#include "main.h"
#include "manager.h"
#include "renderer.h"
#include "input.h"

#include "field.h"
#include "camera.h"
#include "player.h"
#include "scene.h"
#include "game.h"
#include <backends/imgui_impl_dx11.h>

	
Scene* Manager::m_Scene = nullptr;	//シーン管理
Scene* Manager::m_SceneNext = nullptr;	//次のシーン管理

void Manager::Init()
{
	Renderer::Init();
	Input::Init();

	m_Scene = new Game();	//ゲームシーンを作成
	m_Scene->Init();	//シーンの初期化

}


void Manager::Uninit()
{
	m_Scene->Uninit();	//シーンの終了
	delete m_Scene;	//シーンの破棄

	Input::Uninit();	
	Renderer::Uninit();

}

void Manager::Update()
{
	Input::Update();	//入力の更新

	m_Scene->Update();	//シーンの更新

	//画面遷移
	if (m_SceneNext != nullptr) {	//次のシーンが設定されている場合
		m_Scene->Uninit();	//現在のシーンを終了
		delete m_Scene;	//現在のシーンを破棄

		m_Scene = m_SceneNext;	//次のシーンに切り替え
		m_Scene->Init();	//新しいシーンを初期化

		m_SceneNext = nullptr;	//次のシーンをクリア

	}

}

void Manager::Draw()
{
	Renderer::Begin();

	m_Scene->Draw();	//シーンの描画

	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	Renderer::End();

}
