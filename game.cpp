#include "main.h"
#include "manager.h"
#include "renderer.h"

#include "game.h"
#include "camera.h"
#include "field.h"
#include "player.h"
#include "sky.h"
#include "animationModel.h"

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

	// ImGuiによるデバッグUIの表示・   操作
    static bool isDebugMode = false; // デバッグモードのON/OFF
    static int debugFrame = 0;       // スライダーで操作するフレーム番号

    ImGui::Begin("Animation Debugger");

    // デバッグモードのスイッチ
    ImGui::Checkbox("Debug Mode (Force Run)", &isDebugMode);

    if (isDebugMode)
    {
        // Runアニメーションの長さを取得してスライダーを作る
        int maxRun = m_SharedModel->GetAnimationDuration("Run");
        ImGui::SliderInt("Run Frame Bar", &debugFrame, 0, maxRun - 1);

        // 全プレイヤーを「走っているポーズ」かつ「スライダーのフレーム」に強制書き換え
        for (auto& p : players)
        {
            p->SetRunning(true);    // 強制的にRunポーズにする
            p->SetFrame(debugFrame); // 強制的にスライダーのフレームにする
        }
    }

    ImGui::End();

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

    if (m_SharedModel) {
        ImGui::Text("Compute Dispatches this frame: %d", m_SharedModel->GetComputeDispatchCount());
        ImGui::Text("Idle: %s", m_SharedModel->WasAnimationBakedThisFrame("Idle") ? "BAKED" : "COMPUTE");
        ImGui::Text("Run : %s", m_SharedModel->WasAnimationBakedThisFrame("Run") ? "BAKED" : "COMPUTE");
    }

	ImGui::End();


    //アニメーション（行列）の更新
    float currentFrame = (float)players[0]->GetFrame();

    // フレーム開始時に AnimationModel のデバッグカウンタをリセット
    if (m_SharedModel) m_SharedModel->ResetDebugCounters();

    m_SharedModel->Update("Idle", currentFrame, "Idle", currentFrame, 0.0f);
    m_SharedModel->Update("Run", currentFrame, "Run", currentFrame, 0.0f);

    //描画データの更新
    m_SharedModel->UpdateInstanceData(players);
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
