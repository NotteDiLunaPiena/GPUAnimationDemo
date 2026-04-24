#include "common.hlsl"

// 入力頂点
struct VertexIn
{
    float3 Position;
    float3 Normal;
    float2 TexCoord;
    float4 Diffuse;
    uint4 BoneIndex;
    float4 BoneWeight;
};

// 出力頂点（C++の VERTEX_SKIN_OUT と完全一致させる）
struct VertexOut
{
    float3 Position;
    float3 Normal;
    float4 Diffuse;
    float2 TexCoord;
};

// 入出力バッファ
StructuredBuffer<VertexIn> g_InputVertices : register(t0);
RWStructuredBuffer<VertexOut> g_OutputVertices : register(u0);

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    VertexIn vin = g_InputVertices[id];
    VertexOut vout;

    matrix boneTransform = 0;
    for (int i = 0; i < 4; i++)
    {
        boneTransform += g_BoneMatrix[vin.BoneIndex[i]] * vin.BoneWeight[i];
    }

    
    vout.Position = mul(boneTransform, float4(vin.Position, 1)).xyz;
    vout.Normal = normalize(mul(boneTransform, float4(vin.Normal, 0)).xyz);

    vout.Diffuse = vin.Diffuse;
    vout.TexCoord = vin.TexCoord;

    g_OutputVertices[id] = vout;
}
