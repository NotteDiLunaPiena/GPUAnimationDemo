#include "computeSkinningManager.h"
#include "computeSkinningManager.h"
#include "animationModel.h"

#include <stdio.h>
#include <io.h>
#include <cassert>
#include "utility.h"

// コンピュートシェーダ読み込み
void ComputeSkinningManager::LoadComputeShader(const char* fileName)
{
	FILE* file = fopen(fileName, "rb");
	assert(file);
	long int fsize = _filelength(_fileno(file));
	unsigned char* buffer = new unsigned char[fsize];
	fread(buffer, fsize, 1, file);
	fclose(file);

	HRESULT hr = Renderer::GetDevice()->CreateComputeShader(buffer, fsize, nullptr, &m_SkinningCS);
	if (FAILED(hr)) MessageBox(nullptr, "ComputeShader の作成に失敗しました。", "Error", MB_OK);
	delete[] buffer;

}

// スキニング用バッファの作成
void ComputeSkinningManager::CreateSkinningBuffers(VERTEX_3D* vertices, UINT vertexCount, ID3D11Buffer** skinInputBuffer, ID3D11ShaderResourceView** skinInputSRV, ID3D11Buffer** skinOutputBuffer, ID3D11UnorderedAccessView** skinOutputUAV, ID3D11Buffer** csVertexBufferGPU)
{
    if (!vertices || vertexCount == 0)
    {
        assert(false && "頂点データが空です");
        return;
    }

    ID3D11Device* device = Renderer::GetDevice();

    if (!device)
    {
        assert(false && "DirectX Deviceが準備できていません");
        return;
    }

    // 入力バッファ
    D3D11_BUFFER_DESC inDesc{};
    inDesc.Usage = D3D11_USAGE_DEFAULT;
    inDesc.ByteWidth = sizeof(VERTEX_3D) * vertexCount;
    inDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    inDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    inDesc.StructureByteStride = sizeof(VERTEX_3D);

    D3D11_SUBRESOURCE_DATA initData{};
    initData.pSysMem = vertices;

    device->CreateBuffer(&inDesc, &initData, skinInputBuffer);

    // SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements = vertexCount;

    device->CreateShaderResourceView(
        *skinInputBuffer,
        &srvDesc,
        skinInputSRV
    );

    // 出力バッファ
    D3D11_BUFFER_DESC outDesc{};
    outDesc.Usage = D3D11_USAGE_DEFAULT;
    outDesc.ByteWidth = sizeof(VERTEX_SKIN_OUT) * vertexCount;
    outDesc.StructureByteStride = sizeof(VERTEX_SKIN_OUT);
    outDesc.BindFlags =
        D3D11_BIND_UNORDERED_ACCESS |
        D3D11_BIND_SHADER_RESOURCE;

    outDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

    HRESULT hr = device->CreateBuffer(
        &outDesc,
        nullptr,
        skinOutputBuffer
    );

    assert(SUCCEEDED(hr));

    // UAV
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.NumElements = vertexCount;

    hr = device->CreateUnorderedAccessView(
        *skinOutputBuffer,
        &uavDesc,
        skinOutputUAV
    );

    assert(SUCCEEDED(hr));

    // VertexBuffer
    D3D11_BUFFER_DESC vbDesc{};
    vbDesc.Usage = D3D11_USAGE_DEFAULT;
    vbDesc.ByteWidth = sizeof(VERTEX_SKIN_OUT) * vertexCount;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    hr = device->CreateBuffer(
        &vbDesc,
        nullptr,
        csVertexBufferGPU
    );

    assert(SUCCEEDED(hr));
}

// スキニングCSのディスパッチ
void ComputeSkinningManager::Dispatch(ID3D11Buffer* boneConstantBuffer, ID3D11ShaderResourceView* skinInputSRV, ID3D11UnorderedAccessView* skinOutputUAV, UINT vertexCount)
{
    if (!m_SkinningCS) return;
    if (!boneConstantBuffer || !skinInputSRV || !skinOutputUAV) return;

    UINT groupCount =
        (vertexCount + SKINNING_THREAD_GROUP_SIZE - 1) /
        SKINNING_THREAD_GROUP_SIZE;

    ID3D11DeviceContext* ctx = Renderer::GetDeviceContext();

    ctx->CSSetShader(m_SkinningCS, nullptr, 0);
    ctx->CSSetConstantBuffers(5, 1, &boneConstantBuffer);
    ctx->CSSetShaderResources(0, 1, &skinInputSRV);
    ctx->CSSetUnorderedAccessViews(0, 1, &skinOutputUAV, nullptr);

    ctx->Dispatch(groupCount, 1, 1);

    ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
    ID3D11ShaderResourceView* nullSRV[1] = { nullptr };

    ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    ctx->CSSetShaderResources(0, 1, nullSRV);
    ctx->CSSetShader(nullptr, nullptr, 0);
}

//解放
void ComputeSkinningManager::Uninit()
{
    SafeRelease(m_SkinningCS);
}