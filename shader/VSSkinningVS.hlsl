#include "common.hlsl"

// 全フレーム分のボーン行列をまとめたバッファ
// レイアウト: [frameIndex * MAX_BONE_COUNT + boneIndex]
StructuredBuffer<float4x4> g_AllBoneMatrices : register(t1);

#define MAX_BONE_COUNT 256

void main(in VS_IN In, out PS_IN Out)
{
    // -------------------------------------------------------
    // 1. インスタンスごとのフレームを取得
    //    InstanceExtra.y に Frame が入っている
    // -------------------------------------------------------
    int frame = (int) In.InstanceExtra.y;
    int duration = (int) In.InstanceExtra.z; // ← duration を渡す
    if (duration > 0)frame = frame % duration;
    
    // -------------------------------------------------------
    // 2. StructuredBuffer からそのフレームのボーン行列を合成
    //    インスタンスごとに異なる frame を参照するため
    //    全インスタンスが独立したポーズになる
    // -------------------------------------------------------
    float4x4 boneTransform = 0;
    for (int i = 0; i < 4; i++)
    {
        int matIndex = frame * MAX_BONE_COUNT + (int)In.BoneIndex[i];
        boneTransform += g_AllBoneMatrices[matIndex] * In.BoneWeight[i];
    }

    // -------------------------------------------------------
    // 3. スキニング（ボーン変換）
    // -------------------------------------------------------
    float4 skinnedPos    = mul(float4(In.Position.xyz, 1.0f), boneTransform);
    float3 skinnedNormal = normalize(mul(float4(In.Normal.xyz, 0.0f), boneTransform).xyz);

    // -------------------------------------------------------
    // 4. インスタンスのワールド行列を組み立て
    // -------------------------------------------------------
    matrix instanceWorld =
    {
        In.InstanceWorldRow0,
        In.InstanceWorldRow1,
        In.InstanceWorldRow2,
        In.InstanceWorldRow3
    };

    // -------------------------------------------------------
    // 5. ワールド → ビュー → プロジェクション変換
    // -------------------------------------------------------
    float4 worldPos = mul(skinnedPos, instanceWorld);
    Out.Position    = mul(worldPos, mul(View, Projection));

    float3x3 world3x3 = (float3x3)instanceWorld;
    Out.Normal   = float4(normalize(mul(world3x3, skinnedNormal)), 0.0f);
    Out.TexCoord = In.TexCoord;
    Out.Diffuse  = In.Diffuse * Material.Diffuse;
}
