#pragma once
#include "main.h"
#include "renderer.h"

class Player;
class ModelResource;

class InstanceDataManager 
{
private:
    // インスタンスバッファ
	// アニメーション状態ごとにバッファを分ける
    //Idleバッファ
    ID3D11Buffer* m_InstanceBufferIdle = nullptr;
    std::vector<InstanceData> m_InstanceDataIdle;
	//Runバッファ
    ID3D11Buffer* m_InstanceBufferRun = nullptr;
    std::vector<InstanceData> m_InstanceDataRun;

	// インスタンスデータの更新とバッファへの転送
	void UpdateBuffer(ID3D11Buffer*& buffer, const std::vector<InstanceData>& data);

	
public:
	// インスタンスデータの更新（視錐台カリングなし）
    void Update(const std::vector<Player*>& players, ModelResource* resource);
	// インスタンスデータの更新（視錐台カリングあり）
    void UpdateWithCulling(
        const std::vector<Player*>& players,
        ModelResource* resource,
        const DirectX::XMMATRIX& view,
        const DirectX::XMMATRIX& proj
    );

    //解放
    void Uninit();

	// ゲッター
	//インスタンスバッファ
    ID3D11Buffer* GetIdleBuffer() const { return m_InstanceBufferIdle; }
    ID3D11Buffer* GetRunBuffer() const { return m_InstanceBufferRun; }
    //インスタンスデータ
    const std::vector<InstanceData>& GetIdleData() const { return m_InstanceDataIdle; }
    const std::vector<InstanceData>& GetRunData() const { return m_InstanceDataRun; }
	//インスタンスの総数
    int GetTotalInstanceCount() const {
        return (int)(m_InstanceDataIdle.size() + m_InstanceDataRun.size());
    }
};