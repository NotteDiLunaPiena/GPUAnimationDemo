#include "animationModelRenderer.h"
#include "modelResource.h"
#include "assimp/types.h"
#include "assimp/material.h"

// マテリアルの適用
void AnimationModelRenderer::ApplyMaterial(aiMaterial* aiMat, ModelResource* resource)
{
    if (!aiMat || !resource) return;

    MATERIAL material{};
    material.Diffuse = DirectX::XMFLOAT4(1, 1, 1, 1);
    material.Ambient = material.Diffuse;
    material.TextureEnable = true;

    aiString textureName;
    aiColor3D diffuse(1.0f, 1.0f, 1.0f);
    float opacity = 1.0f;

    aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
    aiMat->Get(AI_MATKEY_OPACITY, opacity);

    ID3D11ShaderResourceView* srv = nullptr;
    if (aiMat->GetTexture(aiTextureType_DIFFUSE, 0, &textureName) == AI_SUCCESS)
    {
        srv = resource->GetTexture(textureName.C_Str());
    }

    if (srv)
    {
        Renderer::GetDeviceContext()->PSSetShaderResources(0, 1, &srv);
        material.TextureEnable = true;
    }
    else
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        Renderer::GetDeviceContext()->PSSetShaderResources(0, 1, &nullSRV);
        material.TextureEnable = false;
    }

    material.Diffuse = DirectX::XMFLOAT4(diffuse.r, diffuse.g, diffuse.b, opacity);
    material.Ambient = material.Diffuse;

    Renderer::SetMaterial(material);
}

// インスタンシング描画
void AnimationModelRenderer::DrawInstancedMesh(ID3D11Buffer* vertexBuffer, ID3D11Buffer* instanceBuffer, ID3D11Buffer* indexBuffer, UINT vertexStride, UINT instanceCount, UINT indexCount)
{
    if (!vertexBuffer || !instanceBuffer || !indexBuffer) return;
    if (instanceCount == 0 || indexCount == 0) return;

    ID3D11Buffer* buffers[2] = { vertexBuffer, instanceBuffer };
    UINT strides[2] = { vertexStride, sizeof(InstanceData) };
    UINT offsets[2] = { 0, 0 };

    Renderer::GetDeviceContext()->IASetVertexBuffers(0, 2, buffers, strides, offsets);
    Renderer::GetDeviceContext()->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);

    Renderer::AddDrawCall();
    Renderer::GetDeviceContext()->DrawIndexedInstanced(
        indexCount,
        instanceCount,
        0, 0, 0
    );
}
