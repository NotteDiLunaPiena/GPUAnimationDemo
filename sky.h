#pragma once

#include "gameObject.h"
#include <d3d11.h>

class Sky : public GameObject
{
private:

	ID3D11InputLayout* m_VertexLayout;
	ID3D11VertexShader* m_VertexShader;
	ID3D11PixelShader* m_PixelShader;

	class ModelRenderer* m_ModelRenderer;

public:
	void Init() override;		//‰Šú‰»
	void Uninit() override;		//I—¹
	void Update() override;		//XV
	void Draw() override;		//•`‰æ

};