cbuffer WorldBuffer : register(b0)
{
	matrix World;
}

cbuffer ViewBuffer : register(b1)
{
	matrix View;
}

cbuffer ProjectionBuffer : register(b2)
{
	matrix Projection;
}

struct MATERIAL
{
	float4 Ambient;
	float4 Diffuse;
	float4 Specular;
	float4 Emission;
	float Shininess;
	bool TextureEnable;
	float2 Dummy;
};

cbuffer MaterialBuffer : register(b3)
{
	MATERIAL Material;
}

struct LIGHT
{
	bool Enable;
	bool3 Dummy;
	float4 Direction;
	float4 Diffuse;
	float4 Ambient;
};

cbuffer LightBuffer : register(b4)
{
	LIGHT Light;
}

// ボーン行列定数バッファ
cbuffer BoneMatrix : register(b5) 
{
    row_major matrix g_BoneMatrix[256]; // 最大256本のボーン行列
};


struct VS_IN
{
	float4 Position		: POSITION0;
	float4 Normal		: NORMAL0;
	float2 TexCoord		: TEXCOORD0;
    float4 Diffuse : COLOR0;
	
	// スキニング用
    uint4 BoneIndex : BONEINDEX; // ボーンインデックス
    float4 BoneWeight : BONEWEIGHT; // ボーンウェイト
	
	// インスタンシング用
    float4 InstanceWorldRow0 : INSTANCE0;
    float4 InstanceWorldRow1 : INSTANCE1;
    float4 InstanceWorldRow2 : INSTANCE2;
    float4 InstanceWorldRow3 : INSTANCE3;
};

struct PS_IN
{
	float4 Position		: SV_POSITION;
	float4 Diffuse		: COLOR0;
	float2 TexCoord		: TEXCOORD0;
	
	// スキニング用
    float4 Normal : NORMAL0;
};
