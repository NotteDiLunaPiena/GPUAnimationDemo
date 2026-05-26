#include "instanceDataManager.h"
#include "player.h"
#include "modelResource.h"
#include "vector3.h"
#include "utility.h"

using namespace DirectX;

// インスタンスデータの更新（視錐台カリングなし）
void InstanceDataManager::Update(const std::vector<Player*>& players, ModelResource* resource)
{
    m_InstanceDataIdle.clear();
    m_InstanceDataRun.clear();

    for (auto* player : players)
    {
        InstanceData inst{};

        Vector3 pos = player->GetPosition();
        Vector3 rot = player->GetRotation();
        Vector3 scale = player->GetScale();

        XMMATRIX world =
            XMMatrixScaling(scale.x, scale.y, scale.z) *
            XMMatrixRotationRollPitchYaw(rot.x, rot.y, rot.z) *
            XMMatrixTranslation(pos.x, pos.y, pos.z);

        XMStoreFloat4x4(&inst.World, world);
        inst.Frame = (float)player->GetFrame();

        const char* animName = player->IsRunning() ? "Run" : "Idle";
        const aiScene* animScene = resource->GetAnimationScene(animName);

        if (animScene && animScene->HasAnimations())
        {
            inst.Duration = (float)(int)animScene->mAnimations[0]->mDuration;
        }
        else
        {
            inst.Duration = 1.0f;
        }

        if (player->IsRunning())
        {
            inst.AnimationIndex = 1;
            m_InstanceDataRun.push_back(inst);
        }
        else
        {
            inst.AnimationIndex = 0;
            m_InstanceDataIdle.push_back(inst);
        }
    }

    UpdateBuffer(m_InstanceBufferIdle, m_InstanceDataIdle);
    UpdateBuffer(m_InstanceBufferRun, m_InstanceDataRun);
}

// インスタンスデータの更新（視錐台カリングあり）
void InstanceDataManager::UpdateWithCulling(const std::vector<Player*>& players, ModelResource* resource, const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj)
{
    m_InstanceDataIdle.clear();
    m_InstanceDataRun.clear();

    XMMATRIX vp = XMMatrixMultiply(view, proj);

    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, XMMatrixTranspose(vp));

    XMFLOAT4 planes[6];

    planes[0] = { m._41 + m._11, m._42 + m._12, m._43 + m._13, m._44 + m._14 };
    planes[1] = { m._41 - m._11, m._42 - m._12, m._43 - m._13, m._44 - m._14 };
    planes[2] = { m._41 + m._21, m._42 + m._22, m._43 + m._23, m._44 + m._24 };
    planes[3] = { m._41 - m._21, m._42 - m._22, m._43 - m._23, m._44 - m._24 };
    planes[4] = { m._41 + m._31, m._42 + m._32, m._43 + m._33, m._44 + m._34 };
    planes[5] = { m._41 - m._31, m._42 - m._32, m._43 - m._33, m._44 - m._34 };

    for (auto& p : planes)
    {
        float len = sqrtf(p.x * p.x + p.y * p.y + p.z * p.z);
        p.x /= len;
        p.y /= len;
        p.z /= len;
        p.w /= len;
    }

    for (auto* player : players)
    {
        Vector3 pos = player->GetPosition();
        Vector3 rot = player->GetRotation();
        Vector3 scale = player->GetScale();

        const float radius = 1.0f;
        bool culled = false;

        for (auto& plane : planes)
        {
            float dist =
                plane.x * pos.x +
                plane.y * pos.y +
                plane.z * pos.z +
                plane.w;

            if (dist < -radius)
            {
                culled = true;
                break;
            }
        }

        if (culled) continue;

        InstanceData inst{};

        XMMATRIX world =
            XMMatrixScaling(scale.x, scale.y, scale.z) *
            XMMatrixRotationRollPitchYaw(rot.x, rot.y, rot.z) *
            XMMatrixTranslation(pos.x, pos.y, pos.z);

        XMStoreFloat4x4(&inst.World, world);
        inst.Frame = (float)player->GetFrame();

        const char* animName = player->IsRunning() ? "Run" : "Idle";
        const aiScene* animScene = resource->GetAnimationScene(animName);

        if (animScene && animScene->HasAnimations())
        {
            inst.Duration = (float)(int)animScene->mAnimations[0]->mDuration;
        }
        else
        {
            inst.Duration = 1.0f;
        }

        if (player->IsRunning())
        {
            inst.AnimationIndex = 1;
            m_InstanceDataRun.push_back(inst);
        }
        else
        {
            inst.AnimationIndex = 0;
            m_InstanceDataIdle.push_back(inst);
        }
    }

    UpdateBuffer(m_InstanceBufferIdle, m_InstanceDataIdle);
    UpdateBuffer(m_InstanceBufferRun, m_InstanceDataRun);
}

// インスタンスデータの更新とバッファへの転送
void InstanceDataManager::UpdateBuffer(ID3D11Buffer*& buffer, const std::vector<InstanceData>& data)
{
    if (data.empty()) return;

    if (!buffer)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth = sizeof(InstanceData) * MAX_INSTANCE_COUNT;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        Renderer::GetDevice()->CreateBuffer(&desc, nullptr, &buffer);
    }

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    Renderer::GetDeviceContext()->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, data.data(), sizeof(InstanceData) * data.size());
    Renderer::GetDeviceContext()->Unmap(buffer, 0);
}

// 解放
void InstanceDataManager::Uninit()
{
    SafeRelease(m_InstanceBufferIdle);
    SafeRelease(m_InstanceBufferRun);

    m_InstanceDataIdle.clear();
    m_InstanceDataRun.clear();
}