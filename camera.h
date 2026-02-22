#pragma once  

#include "gameObject.h"  

class Camera : public GameObject
{
private:
	XMMATRIX m_Projection;
	XMMATRIX m_View;

	Vector3 m_Target{ 0.0f,0.0f,0.0f };

	XMFLOAT3 m_Up{ 0.0f,1.0f,0.0f };

public:
	void Init() override;
	void Uninit() override;
	void Update() override;
	void Draw() override;

	// Add the missing GetViewMatrix method  
	XMMATRIX GetViewMatrix() const { return m_View; }
	XMMATRIX GetProjectionMatrix() const { return m_Projection; }

};
