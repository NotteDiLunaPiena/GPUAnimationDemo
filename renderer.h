#pragma once
#include <DirectXMath.h>
using namespace DirectX;

// ==============================
// GPUスキニング用構造体
// ==============================

// 入力データ（CPU→ComputeShader）
struct VERTEX_SKIN_IN
{
	XMFLOAT3 Position;
	XMFLOAT3 Normal;	
	XMFLOAT2 TexCoord;
	XMFLOAT4 Diffuse;

	UINT BoneIndex[4];     
	float BoneWeight[4];   
};

// 出力データ（ComputeShader→VertexShader）
struct VERTEX_SKIN_OUT
{
	XMFLOAT3 Position;
	XMFLOAT3 Normal;
	XMFLOAT4 Diffuse;
	XMFLOAT2 TexCoord;
};

// インスタンスデータ構造体
__declspec(align(16)) //16バイトアライメントを指定
struct InstanceData
{
	DirectX::XMFLOAT4X4 World; // 各インスタンスのワールド行列
	UINT AnimationIndex;       // アニメーション種類 (0=Idle, 1=Run)
	float Frame;               // 現在のフレーム番号
	float Duration;            // アニメーションの総フレーム数
	float Padding[2];          // 16バイトアライメントを保つためのダミー
};


// 従来型との互換保持
typedef VERTEX_SKIN_IN VERTEX_3D;


// ==============================
// 共通定数バッファ
// ==============================
struct CB_BONE_MATRIX
{
	XMFLOAT4X4 BoneMatrix[256];	// 最大256ボーン
};

struct MATERIAL
{
	XMFLOAT4	Ambient;
	XMFLOAT4	Diffuse;
	XMFLOAT4	Specular;
	XMFLOAT4	Emission;
	float		Shininess;
	BOOL		TextureEnable;
	float		Dummy[2];
};

struct LIGHT
{
	BOOL		Enable;
	BOOL		Dummy[3];
	XMFLOAT4	Direction;
	XMFLOAT4	Diffuse;
	XMFLOAT4	Ambient;
};


// ==============================
// Rendererクラス（変更なし）
// ==============================
class Renderer
{
private:

	static D3D_FEATURE_LEVEL       m_FeatureLevel;
	static ID3D11Device* m_Device;
	static ID3D11DeviceContext* m_DeviceContext;
	static IDXGISwapChain* m_SwapChain;
	static ID3D11RenderTargetView* m_RenderTargetView;
	static ID3D11DepthStencilView* m_DepthStencilView;

	static ID3D11Buffer* m_WorldBuffer;
	static ID3D11Buffer* m_ViewBuffer;
	static ID3D11Buffer* m_ProjectionBuffer;
	static ID3D11Buffer* m_MaterialBuffer;
	static ID3D11Buffer* m_LightBuffer;

	static ID3D11DepthStencilState* m_DepthStateEnable;
	static ID3D11DepthStencilState* m_DepthStateDisable;

	static ID3D11BlendState* m_BlendState;
	static ID3D11BlendState* m_BlendStateATC;

public:
	static void Init();
	static void Uninit();
	static void Begin();
	static void End();

	static void SetDepthEnable(bool Enable);
	static void SetATCEnable(bool Enable);
	static void SetWorldViewProjection2D();
	static void SetWorldMatrix(XMMATRIX WorldMatrix);
	static void SetViewMatrix(XMMATRIX ViewMatrix);
	static void SetProjectionMatrix(XMMATRIX ProjectionMatrix);
	static void SetMaterial(MATERIAL Material);
	static void SetLight(LIGHT Light);

	static ID3D11Device* GetDevice(void) { return m_Device; }
	static ID3D11DeviceContext* GetDeviceContext(void) { return m_DeviceContext; }

	static void CreateVertexShader(ID3D11VertexShader** VertexShader, ID3D11InputLayout** VertexLayout, const char* FileName);
	static void CreateVSSkinningVertexShader(ID3D11VertexShader** VertexShader, ID3D11InputLayout** VertexLayout, const char* FileName);
	static void CreatePixelShader(ID3D11PixelShader** PixelShader, const char* FileName);

	static void CreateCSSkinningVertexShader(ID3D11VertexShader** VertexShader, ID3D11InputLayout** VertexLayout, const char* FileName);

	static void CreateTexture(const char* FileName, ID3D11ShaderResourceView** TextureView);

	static void ResetDrawCount();
	static void AddDrawCall();
	static int  GetDrawCallCount();


};
