#include "common.hlsl"

struct VS_IN_SKINNED
{
    float3 Position : POSITION0;
    float3 Normal : NORMAL0;
    float4 Diffuse : COLOR0;
    float2 TexCoord : TEXCOORD0;

    float4 InstanceWorldRow0 : INSTANCE0;
    float4 InstanceWorldRow1 : INSTANCE1;
    float4 InstanceWorldRow2 : INSTANCE2;
    float4 InstanceWorldRow3 : INSTANCE3;
};

void main(in VS_IN_SKINNED In, out PS_IN Out)
{
    matrix instanceWorld =
    {
        In.InstanceWorldRow0,
        In.InstanceWorldRow1,
        In.InstanceWorldRow2,
        In.InstanceWorldRow3
    };

    // à íuïœä∑
    float4 worldPos = mul(float4(In.Position, 1.0f), instanceWorld);
    Out.Position = mul(worldPos, mul(View, Projection));

    // ñ@ê¸ïœä∑
    float3x3 world3x3 = (float3x3) instanceWorld;
    float3 normalW = normalize(mul(world3x3, In.Normal));

    Out.Normal = float4(normalW, 0.0f);

    // ÇªÇÃëº
    Out.TexCoord = In.TexCoord;
    Out.Diffuse = In.Diffuse * Material.Diffuse;
}
