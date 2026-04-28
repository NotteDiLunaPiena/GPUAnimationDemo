#include "main.h"
#include "manager.h"
#include "renderer.h"

#include "game.h"
#include "camera.h"
#include "field.h"
#include "player.h"
#include "sky.h"
#include "animationModel.h"
#include "animationPlayer.h"

#include "imgui.h"

void Game::Init()
{
    AddGameObject<Camera>(0);
    AddGameObject<Sky>(0)->SetScale({ 50.0f, 50.0f, 50.0f });
    AddGameObject<Field>(0);

    // 共有アニメーションモデルを1回だけ作成
    m_SharedModel = new AnimationModel();
    m_SharedModel->Load("asset\\model\\Akai.fbx");
    m_SharedModel->LoadAnimation("asset\\model\\Akai_Run.fbx", "Run");
    m_SharedModel->LoadAnimation("asset\\model\\Akai_Idle.fbx", "Idle");


    // 出力ディレクトリがなければ作る（Winsdk が含まれている前提）
    CreateDirectoryA("baked", NULL);

    // 一度だけベイクする（重い処理なので本番では無効にするか、存在チェックを行う）
    bool doBake = true;
    if (doBake) {
        m_SharedModel->BakeAnimationToDisk("Idle", "baked\\Idle.baked");
        m_SharedModel->BakeAnimationToDisk("Run", "baked\\Run.baked");
    }

    m_SharedModel->LoadBakedAnimation("baked\\Idle.baked", "Idle");
    m_SharedModel->LoadBakedAnimation("baked\\Run.baked", "Run");


    //プレイヤーを生成して共有モデルを渡す
    for (int i = 0; i < MAX_INSTANCE_COUNT; i++) {
        auto p = AddGameObject<Player>(1);
		p->SetID(i);
        p->SetPosition({ (float)(i % 50) * 2.0f, 0.0f, (float)(i / 50) * 2.0f });
        p->SetSharedModel(m_SharedModel);
    }

}

void Game::Uninit()
{    delete m_SharedModel;
    m_SharedModel = nullptr;

    Scene::Uninit();
}


void Game::Update()
{
    Scene::Update();

    auto players = GetGameObjects<Player>();
    if (players.empty() || !m_SharedModel) return;

    static int s_ActiveCount = MAX_INSTANCE_COUNT;

	// ImGuiによるデバッグUIの表示・   操作
    static bool isDebugMode = false; // デバッグモードのON/OFF
    static int debugFrame = 0;       // スライダーで操作するフレーム番号

	// レンダリング統計情報の表示
    ImGui::Begin("GPU Skinning Demo");
    //FPS
    ImGui::Text("FPS            : %d", dwCurrentFPS);
    //インスタンス数
    ImGui::Text("Instance Count : %d", m_SharedModel->GetTotalInstanceCount());
    //メッシュ数
    ImGui::Text("Mesh Count     : %d", m_SharedModel->GetMeshCount());
    //Drawの呼び出し数　（メッシュ数*アニメーションの種類）
    ImGui::Text("Draw Calls     : %d", Renderer::GetDrawCallCount());
    //モデル読み込み対数
    ImGui::Text("Model Count    : %d", m_SharedModel ? 1 : 0);

    ImGui::Separator();
    ImGui::SliderInt("Active Instances", &s_ActiveCount, 1, MAX_INSTANCE_COUNT);
    ImGui::Text("Active: %d / %d", s_ActiveCount, MAX_INSTANCE_COUNT);

    if (m_SharedModel) {
        ImGui::Text("Compute Dispatches this frame: %d", m_SharedModel->GetComputeDispatchCount());
        ImGui::Text("Idle: %s", m_SharedModel->WasAnimationBakedThisFrame("Idle") ? "BAKED" : "COMPUTE");
        ImGui::Text("Run : %s", m_SharedModel->WasAnimationBakedThisFrame("Run") ? "BAKED" : "COMPUTE");
    
        //スキニング方式の切り替えUI
        ImGui::Separator();
        ImGui::Text("Skinning Mode:");

        SkinningMode currentMode = m_SharedModel->GetSkinningMode();
        bool useCS = (currentMode == SkinningMode::ComputeShader);
        bool useVS = (currentMode == SkinningMode::VertexShader);

        // ラジオボタンで排他的に選択
        if (ImGui::RadioButton("ComputeShader", useCS))
            m_SharedModel->SetSkinningMode(SkinningMode::ComputeShader);

        ImGui::SameLine();

        if (ImGui::RadioButton("VertexShader", useVS))
            m_SharedModel->SetSkinningMode(SkinningMode::VertexShader);

        // ★ 現在のモードを文字で表示
        ImGui::Text("Current: %s",
            currentMode == SkinningMode::ComputeShader ? "CS Skinning" : "VS Skinning");
    }

	ImGui::End();


    //アニメーション（行列）の更新
    float currentFrame = (float)players[0]->GetFrame();

    // フレーム開始時に AnimationModel のデバッグカウンタをリセット
    if (m_SharedModel) m_SharedModel->ResetDebugCounters();

    m_SharedModel->Update("Idle", (int)currentFrame, "Idle", (int)currentFrame, 0.0f);
    m_SharedModel->Update("Run", (int)currentFrame, "Run", (int)currentFrame, 0.0f);

    int activeCount = std::min(s_ActiveCount, (int)players.size());

    // 末尾（奥）ではなく、先頭（手前）を残すため末尾から削る
    std::vector<Player*> activePlayers(
        players.end() - activeCount,  
        players.end()
    );

    Camera* camera = GetGameObject<Camera>();
    if (camera)
    {
        m_SharedModel->UpdateInstanceData(
            activePlayers,
            camera->GetViewMatrix(),
            camera->GetProjectionMatrix()
        );
    }
}

void Game::Draw()
{
    Scene::Draw();

	// 共有アニメーションモデルの描画
    if (m_SharedModel)
    {
        Camera* camera = GetGameObject<Camera>();
        if (camera)
        {
            Renderer::SetViewMatrix(camera->GetViewMatrix());
            Renderer::SetProjectionMatrix(camera->GetProjectionMatrix());
        }
        m_SharedModel->Draw();
    }

}
