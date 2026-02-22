#pragma once
#include "vector3.h"

class GameObject
{
protected:
    bool m_Destroy = false;
    Vector3 m_Position{ 0.0f,0.0f,0.0f };
    Vector3 m_Rotation{ 0.0f,0.0f,0.0f };
    Vector3 m_Scale{ 1.0f,1.0f,1.0f };

public:
    virtual void Init() {}
    virtual void Uninit() {}
    virtual void Update() {}
    virtual void Draw() {}

    void SetDestroy() { m_Destroy = true; }
    bool Destroy()
    {
        if (m_Destroy) { Uninit(); delete this; return true; }
        return false;
    }

    Vector3 GetPosition() const { return m_Position; }
    Vector3 GetRotation() const { return m_Rotation; }
    Vector3 GetScale() const { return m_Scale; }

    void SetPosition(const Vector3 pos) { m_Position = pos; }
    void SetRotation(const Vector3 rot) { m_Rotation = rot; }
    void SetScale(const Vector3 scale) { m_Scale = scale; }

    // •ûŒüƒxƒNƒgƒ‹
    Vector3 GetForward() {
        XMMATRIX m = XMMatrixRotationRollPitchYaw(0.0f, m_Rotation.y, 0.0f);
        Vector3 f;
        XMStoreFloat3((XMFLOAT3*)&f, m.r[2]);
        return f;
    }

    Vector3 GetBackward() { return -GetForward(); }

    Vector3 GetRight() {
        XMMATRIX m = XMMatrixRotationRollPitchYaw(0.0f, m_Rotation.y, 0.0f);
        Vector3 r;
        XMStoreFloat3((XMFLOAT3*)&r, m.r[0]);
        return r;
    }

    Vector3 GetLeft() { return -GetRight(); }

    float GetDistance(Vector3 pos) { return (m_Position - pos).length(); }
};
