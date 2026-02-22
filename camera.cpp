#include  "main.h"
#include "renderer.h"

#include "camera.h"
#include "manager.h"
#include "player.h"
#include "input.h"
#include "scene.h"

void Camera::Init()
{
	m_Projection = XMMatrixPerspectiveFovLH(
		XMConvertToRadians(60.0f), // 視野角 60度
		(float)SCREEN_WIDTH / (float)SCREEN_HEIGHT, // アスペクト比
		0.1f, // NearZ (ニアクリップ)
		1000.0f // FarZ (ファークリップ)
	);

	m_Position = { 0.0f,1.0f,-5.0f };

}

void Camera::Uninit()
{

}

void Camera::Update()
{

	if (Input::GetKeyPress(VK_LEFT))
	{
		m_Rotation.y -= 0.1f;
	}
	if (Input::GetKeyPress(VK_RIGHT))
	{
		m_Rotation.y += 0.1f;
	}

	//Player* player = Manager::GetScene()->GetGameObject<Player>();
	//m_Target = player->GetPosition() + Vector3(0.0f, 1.0f, 0.0f); // 視点の中心（プレイヤーの頭のあたり）

	m_Target = Vector3(0.0f, 5.0f, -5.0f);

	//m_Target = { 0.0f,5.0f,5.0f };
	float distance = 5.0f;     // プレイヤーからの距離
	float height = 3.0f;       // 上方向の高さ

	// 斜め後ろ上から見る位置に設定
	m_Position = m_Target
		+ Vector3(-sinf(m_Rotation.y), 0.0f, -cosf(m_Rotation.y)) * distance
		+ Vector3(0.0f, height, 0.0f); // 高さを足す

	m_View = XMMatrixLookAtLH(
		XMLoadFloat3((XMFLOAT3*)&m_Position),
		XMLoadFloat3((XMFLOAT3*)&m_Target),
		XMLoadFloat3(&m_Up)
	);


}

void Camera::Draw()
{
}
