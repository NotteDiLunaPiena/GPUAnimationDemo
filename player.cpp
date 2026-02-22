#include "main.h"
#include "manager.h"
#include "renderer.h"
#include "input.h"
#include "player.h"
#include "camera.h"
#include "scene.h"

#include "animationModel.h"

void Player::Init()
{
	m_AnimationName = "Idle";
	m_AnimationNameNext = "Idle";
    m_Frame = rand() % 1000;
	m_IsRunning = false;

	m_Scale = { 0.01f,0.01f,0.01f };

}

void Player::Uninit()
{

}

void Player::Update()
{
    Camera* camera = Manager::GetScene()->GetGameObject<Camera>();
    bool move = false;

    Vector3 camForward = camera->GetForward();
    camForward.y = 0.0f;
    camForward.normalize();

    Vector3 camRight = camera->GetRight();
    camRight.y = 0.0f;
    camRight.normalize();

    Vector3 moveDir(0, 0, 0);

    if (m_ID == 0)
    {
        if (GetAsyncKeyState('W') & 0x8000) moveDir += camForward;
        if (GetAsyncKeyState('S') & 0x8000) moveDir -= camForward;
        if (GetAsyncKeyState('D') & 0x8000) moveDir += camRight;
        if (GetAsyncKeyState('A') & 0x8000) moveDir -= camRight;
    }
    else
    {
        if (GetAsyncKeyState('I') & 0x8000) moveDir += camForward;
        if (GetAsyncKeyState('K') & 0x8000) moveDir -= camForward;
        if (GetAsyncKeyState('L') & 0x8000) moveDir += camRight;
        if (GetAsyncKeyState('J') & 0x8000) moveDir -= camRight;
    }

    if (moveDir.length() > 0.0001f)
    {
        moveDir.normalize();
        m_Position += moveDir * 0.1f;
        move = true;

        // キャラクターの向き更新
        Vector3 lookDir = moveDir;
        lookDir.y = 0.0f;
        lookDir.normalize();

        // モデルの前方向が -Z の場合は yaw に PI を足す
        float yaw = atan2f(lookDir.x, lookDir.z) + XM_PI;

        m_Rotation = Vector3(0.0f, yaw, 0.0f);
    }

    m_IsRunning = move;

    
    m_Frame++;
}

