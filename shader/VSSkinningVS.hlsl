#include "common.hlsl"

// VS_IN は common.hlsl で定義済み（BoneIndex, BoneWeight, InstanceWorld 含む）

void main(in VS_IN In, out PS_IN Out)
{
    // -------------------------------------------------------
    // 1. ボーン行列の合成（スキニング）
    //    BoneIndex[i] と BoneWeight[i] を使って
    //    最大4本のボーン行列を加重合成する
    // -------------------------------------------------------
    matrix boneTransform = 0;
    for (int i = 0; i < 4; i++)
    {
        boneTransform += g_BoneMatrix[In.BoneIndex[i]] * In.BoneWeight[i];
    }

    // ボーン変換後のローカル座標・法線
    float4 skinnedPos = mul(boneTransform, float4(In.Position.xyz, 1.0f));
    float3 skinnedNormal = normalize(mul(float4(In.Normal.xyz, 0.0f), boneTransform).xyz);

    // -------------------------------------------------------
    // 2. インスタンスのワールド行列を組み立てる
    //    （CSSkinningVS.hlsl と同じ方式）
    // -------------------------------------------------------
    matrix instanceWorld =
    {
        In.InstanceWorldRow0,
        In.InstanceWorldRow1,
        In.InstanceWorldRow2,
        In.InstanceWorldRow3
    };

    // -------------------------------------------------------
    // 3. ワールド → ビュー → プロジェクション変換
    // -------------------------------------------------------
    float4 worldPos  = mul(skinnedPos, instanceWorld);
    Out.Position     = mul(worldPos, mul(View, Projection));

    // 法線をワールド空間へ変換
    float3x3 world3x3 = (float3x3) instanceWorld;
    Out.Normal = float4(normalize(mul(world3x3, skinnedNormal)), 0.0f);

    // -------------------------------------------------------
    // 4. その他パススルー
    // -------------------------------------------------------
    Out.TexCoord = In.TexCoord;
    Out.Diffuse  = In.Diffuse * Material.Diffuse;
}
