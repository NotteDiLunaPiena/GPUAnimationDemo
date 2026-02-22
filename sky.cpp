#include "main.h"
#include "manager.h"
#include "renderer.h"
#include "modelRenderer.h"
#include "input.h"
#include "scene.h"

#include "sky.h"
#include "camera.h"

void Sky::Init()
{
	m_ModelRenderer = new ModelRenderer();
	m_ModelRenderer->Load("asset\\model\\sky.obj");

	//シェーダの読み込み
	Renderer::CreateVertexShader(&m_VertexShader, &m_VertexLayout, "shader\\unlitTextureVS.cso");
	Renderer::CreatePixelShader(&m_PixelShader, "shader\\unlitTexturePS.cso");

}

void Sky::Uninit()
{
	delete m_ModelRenderer;

	if(m_VertexLayout) m_VertexLayout->Release();
	if (m_PixelShader) m_PixelShader->Release();
	if (m_VertexShader) m_VertexShader->Release();

}

void Sky::Update()
{
	Camera* camera = Manager::GetScene()->GetGameObject<Camera>();
	m_Position = camera->GetPosition(); // カメラの位置に合わせる

}

void Sky::Draw()
{
	//入力レイアウト設定
	Renderer::GetDeviceContext()->IASetInputLayout(m_VertexLayout);

	//シェーダ設定
	Renderer::GetDeviceContext()->VSSetShader(m_VertexShader, NULL, 0);
	Renderer::GetDeviceContext()->PSSetShader(m_PixelShader, NULL, 0);

	//マトリックス設定
	XMMATRIX world, scale, rot, trans;
	scale = XMMatrixScaling(m_Scale.x, m_Scale.y, m_Scale.z);
	rot = XMMatrixRotationRollPitchYaw(m_Rotation.x, m_Rotation.y + XM_PI, m_Rotation.z);
	trans = XMMatrixTranslation(m_Position.x, m_Position.y, m_Position.z);
	world = scale * rot * trans;

	Renderer::SetWorldMatrix(world);

	m_ModelRenderer->Draw();

}
