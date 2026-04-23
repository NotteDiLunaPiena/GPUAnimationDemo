#include "main.h"
#include "renderer.h"
#include "animationModel.h"
#include "utility.h"
#include <d3dcompiler.h>
#include <corecrt_io.h>
#include "vector3.h"
#include "player.h"
#include "modelResource.h"
#include "animationPlayer.h"

using namespace DirectX;

void AnimationModel::Load(const char* FileName)
{
	// リソースの実体を作成
	if (!m_Resource) m_Resource = new ModelResource();
	m_AiScene = m_Resource->LoadModel(FileName);
	assert(m_AiScene);

	if (!m_Player)m_Player = new AnimationPlayer();

	UINT numMeshes = m_AiScene->mNumMeshes;

	//インスタンス固有のバッファ配列の確保
	m_VertexBuffer = new ID3D11Buffer * [numMeshes];
	m_IndexBuffer = new ID3D11Buffer * [numMeshes];
	m_SkinInputBuffer = new ID3D11Buffer * [numMeshes] {};
	m_SkinInputSRV = new ID3D11ShaderResourceView * [numMeshes] {};
	m_SkinOutputBuffer = new ID3D11Buffer * [numMeshes] {};
	m_SkinOutputUAV = new ID3D11UnorderedAccessView * [numMeshes] {};
	m_VertexBufferGPU = new ID3D11Buffer * [numMeshes] {};

	// ボーン構造の構築
	m_Player->Init(m_AiScene, m_Resource);

	// 定数バッファの作成
	{
		struct CB_BONE_MATRIX { XMFLOAT4X4 BoneMatrix[256]; };
		D3D11_BUFFER_DESC bd{};
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.ByteWidth = sizeof(CB_BONE_MATRIX);
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		Renderer::GetDevice()->CreateBuffer(&bd, nullptr, &m_BoneConstantBuffer);
	}

	// メッシュごとの固有バッファ（スキニング用）作成
	for (unsigned int m = 0; m < numMeshes; m++)
	{
		aiMesh* mesh = m_AiScene->mMeshes[m];

		// リソースから頂点バッファ・インデックスバッファの参照を取得
		m_VertexBuffer[m] = m_Resource->GetVertexBuffer(m);
		m_IndexBuffer[m] = m_Resource->GetIndexBuffer(m);

		// スキニング用バッファは「動く頂点」を保持するため、インスタンスごとに作成
		// (本来は頂点データが必要なため、一時的に頂点配列を構築するか、Resourceから取得する)
		VERTEX_3D* tempVertices = GetVerticesFromMesh(mesh, m);
		CreateComputeSkinningBuffers(tempVertices, mesh->mNumVertices, m);
		delete[] tempVertices;

		m_SkinOutputBuffer_Idle[m] = nullptr;
		m_SkinOutputBuffer_Run[m] = nullptr;
	}

	// シェーダーのロード
	LoadComputeShader("shader\\skinningCS.cso");
	Renderer::CreateSkinningVertexShader(&m_VertexShader, &m_VertexLayout, "shader\\skinningVS.cso");
	Renderer::CreatePixelShader(&m_PixelShader, "shader\\skinningPS.cso");
}

// アニメーション読み込み
void AnimationModel::LoadAnimation(const char* FileName, const char* Name)
{
	m_Resource->LoadAnimation(FileName, Name);
}

// アニメーション更新
void AnimationModel::Update(const char* Anim1, int Frame1, const char* Anim2, int Frame2, float BlendRate)
{
	// ベイク済みアニメーションがあればそちらを使用
	if (Anim1 && m_BakedBuffers.count(Anim1))
	{
		m_LastBakedFrame[Anim1] = Frame1;
		m_AnimationUsedBaked[Anim1] = true; 
		return;
	}

	std::string animName = Anim1 ? Anim1 : "";

	//アニメーションの存在チェック
	if (!Anim1 || !Anim2) return;
	const aiScene* s1 = m_Resource->GetAnimationScene(Anim1);
	const aiScene* s2 = m_Resource->GetAnimationScene(Anim2);
	if (!s1 || !s2) return;
	if (!s1->HasAnimations() || !s2->HasAnimations()) return;

	m_Player->Update(Anim1, Frame1, Anim2, Frame2, BlendRate);


	//定数バッファの更新
	D3D11_MAPPED_SUBRESOURCE ms;
	Renderer::GetDeviceContext()->Map(m_BoneConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
	struct CB_BONE_MATRIX { XMFLOAT4X4 BoneMatrix[MAX_BONE_COUNT]; };
	auto* cbBone = (CB_BONE_MATRIX*)ms.pData;

	const auto& bones = m_Player->GetBones();
	const auto& boneNameToIndex = m_Player->GetBoneNameToIndex();

	for (auto& pair : boneNameToIndex) 
	{
		unsigned int index = pair.second;
		if (index >= MAX_BONE_COUNT) continue;
		XMMATRIX mat = ConvertAiMatrixToXMMatrix(bones.at(pair.first).Matrix);
		XMStoreFloat4x4(&cbBone->BoneMatrix[index], XMMatrixTranspose(mat));

	}
	Renderer::GetDeviceContext()->Unmap(m_BoneConstantBuffer, 0);

	//各メッシュごとにスキニング処理を実行
	for (unsigned int m = 0; m < m_AiScene->mNumMeshes; m++)
	{
		UINT numVertices = m_AiScene->mMeshes[m]->mNumVertices;
		UINT groupCount = (numVertices + SKINNING_THREAD_GROUP_SIZE - 1) / SKINNING_THREAD_GROUP_SIZE;

		m_ComputeDispatchCount++;                 // CS を使った回数をカウント
		if (Anim1) m_AnimationUsedBaked[Anim1] = false; // このアニメは CS 実行で更新された（ベイクではない）
	
		//ComputeShader と定数バッファをセット
		Renderer::GetDeviceContext()->CSSetShader(m_SkinningCS, nullptr, 0);
		Renderer::GetDeviceContext()->CSSetConstantBuffers(5, 1, &m_BoneConstantBuffer);
		Renderer::GetDeviceContext()->CSSetShaderResources(0, 1, &m_SkinInputSRV[m]);
		Renderer::GetDeviceContext()->CSSetUnorderedAccessViews(0, 1, &m_SkinOutputUAV[m], nullptr);

		Renderer::GetDeviceContext()->Dispatch(groupCount, 1, 1);

		//アニメーション別にスキニング結果を保存
		std::string anim = Anim1 ? Anim1 : "";

		ID3D11Buffer** targetBuffer = nullptr;

		if (anim == "Idle")
		{
			if (!m_SkinOutputBuffer_Idle[m])
			{
				D3D11_BUFFER_DESC bd{};
				bd.Usage = D3D11_USAGE_DEFAULT;
				bd.ByteWidth = sizeof(VERTEX_SKIN_OUT) * numVertices;
				bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
				Renderer::GetDevice()->CreateBuffer(&bd, nullptr, &m_SkinOutputBuffer_Idle[m]);
			}
			targetBuffer = &m_SkinOutputBuffer_Idle[m];
		}
		else if (anim == "Run")
		{
			if (!m_SkinOutputBuffer_Run[m])
			{
				D3D11_BUFFER_DESC bd{};
				bd.Usage = D3D11_USAGE_DEFAULT;
				bd.ByteWidth = sizeof(VERTEX_SKIN_OUT) * numVertices;
				bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
				Renderer::GetDevice()->CreateBuffer(&bd, nullptr, &m_SkinOutputBuffer_Run[m]);
			}
			targetBuffer = &m_SkinOutputBuffer_Run[m];
		}

		if (targetBuffer && *targetBuffer)
		{
			Renderer::GetDeviceContext()->CopyResource(*targetBuffer, m_SkinOutputBuffer[m]);
		}

		//CSリソースのバインド解除
		ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
		ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
		Renderer::GetDeviceContext()->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		Renderer::GetDeviceContext()->CSSetShaderResources(0, 1, nullSRV);
	}

	// CS解除
	Renderer::GetDeviceContext()->CSSetShader(nullptr, nullptr, 0);
}

// インスタンスデータ更新
void AnimationModel::UpdateInstanceData(const std::vector<Player*>& players)
{
	//インスタンス配列の初期化
	m_InstanceDataIdle.clear();
	m_InstanceDataRun.clear();

	//全プレイヤー分のインスタンスデータの作成
	for (auto* player : players)
	{
		InstanceData inst{};

		Vector3 pos = player->GetPosition();
		Vector3 rot = player->GetRotation();
		Vector3 scale = player->GetScale();

		//ワールド行列の作成
		XMMATRIX world =
			XMMatrixScaling(scale.x, scale.y, scale.z) *
			XMMatrixRotationRollPitchYaw(rot.x, rot.y, rot.z) *
			XMMatrixTranslation(pos.x, pos.y, pos.z);

		//GPU用のインスタンスデータに格納
		XMStoreFloat4x4(&inst.World, world);
		inst.Frame = (float)player->GetFrame();

		//プレイヤーのアニメーション状態に応じて振り分け　（同じアニメーションをまとめて描画するため）
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

	//インスタンスバッファの作成・更新
	auto updateBuffer = [](ID3D11Buffer*& buffer, const std::vector<InstanceData>& data)
		{
			if (data.empty()) return;

			//バッファが未作成なら作成
			if (!buffer)
			{
				D3D11_BUFFER_DESC desc = {};
				desc.Usage = D3D11_USAGE_DYNAMIC;	//毎フレーム更新
				desc.ByteWidth = sizeof(InstanceData) * MAX_INSTANCE_COUNT;
				desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;	//GPU書き込み可能
				Renderer::GetDevice()->CreateBuffer(&desc, nullptr, &buffer);
			}

			//GPUへのデータ転送
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			Renderer::GetDeviceContext()->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
			memcpy(mapped.pData, data.data(), sizeof(InstanceData) * data.size());
			Renderer::GetDeviceContext()->Unmap(buffer, 0);
		};

	//アニメーションごとにインスタンスバッファ更新
	updateBuffer(m_InstanceBufferIdle, m_InstanceDataIdle);
	updateBuffer(m_InstanceBufferRun, m_InstanceDataRun);
}

// 描画
void AnimationModel::Draw()
{
	//シェーダーと入力レイアウトの設定
	Renderer::GetDeviceContext()->VSSetShader(m_VertexShader, NULL, 0);
	Renderer::GetDeviceContext()->PSSetShader(m_PixelShader, NULL, 0);
	Renderer::GetDeviceContext()->IASetInputLayout(m_VertexLayout);
	Renderer::GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	//マテリアル設定
	MATERIAL material{};
	material.Diffuse = XMFLOAT4(1, 1, 1, 1);
	material.Ambient = material.Diffuse;
	material.TextureEnable = true;
	Renderer::SetMaterial(material);

	//メッシュ単位で描画
	for (unsigned int m = 0; m < m_AiScene->mNumMeshes; m++)
	{
		aiMesh* mesh = m_AiScene->mMeshes[m];
		aiMaterial* aimaterial = m_AiScene->mMaterials[mesh->mMaterialIndex];

		// --- テクスチャ設定セクション ---
		aiString textureName;
		aiColor3D diffuse(1.0f, 1.0f, 1.0f); // 初期値を白に
		float opacity = 1.0f;

		// マテリアルから情報を取得
		aimaterial->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
		aimaterial->Get(AI_MATKEY_OPACITY, opacity);

		// テクスチャ名の取得とセット
		ID3D11ShaderResourceView* srv = nullptr;
		if (aimaterial->GetTexture(aiTextureType_DIFFUSE, 0, &textureName) == AI_SUCCESS) {
			srv = m_Resource->GetTexture(textureName.C_Str());
		}

		if (srv) {
			Renderer::GetDeviceContext()->PSSetShaderResources(0, 1, &srv);
			material.TextureEnable = true;
		}
		else {
			// テクスチャがない時は null をセットして前のメッシュのテクスチャを解除する
			ID3D11ShaderResourceView* nullSRV = nullptr;
			Renderer::GetDeviceContext()->PSSetShaderResources(0, 1, &nullSRV);
			material.TextureEnable = false;
		}

		// マテリアルの最終適用（1回でOK）
		material.Diffuse = XMFLOAT4(diffuse.r, diffuse.g, diffuse.b, opacity);
		material.Ambient = material.Diffuse;
		Renderer::SetMaterial(material);

		// Idleキャラの描画
		if (!m_InstanceDataIdle.empty())
		{
			// スキニング後の頂点バッファ（ベイクがあればそれを使う）
			ID3D11Buffer* skinVB = nullptr;
			if (m_BakedBuffers.count("Idle"))
			{
				auto& perMesh = m_BakedBuffers["Idle"][m];
				int frameCount = (int)perMesh.size();
				if (frameCount > 0)
				{
					int frameIndex = 0;
					if (m_LastBakedFrame.count("Idle")) frameIndex = m_LastBakedFrame["Idle"] % frameCount;
					skinVB = perMesh[frameIndex];
				}
			}
			else
			{
				skinVB = m_SkinOutputBuffer_Idle[m];
			}

			if (skinVB) {
				ID3D11Buffer* buffersIdle[2] = { skinVB, m_InstanceBufferIdle };
				UINT stridesIdle[2] = { sizeof(VERTEX_SKIN_OUT), sizeof(InstanceData) };
				UINT offsetsIdle[2] = { 0, 0 };
				Renderer::GetDeviceContext()->IASetVertexBuffers(0, 2, buffersIdle, stridesIdle, offsetsIdle);
				Renderer::GetDeviceContext()->IASetIndexBuffer(m_IndexBuffer[m], DXGI_FORMAT_R32_UINT, 0);
				Renderer::AddDrawCall();
				Renderer::GetDeviceContext()->DrawIndexedInstanced(mesh->mNumFaces * 3, (UINT)m_InstanceDataIdle.size(), 0, 0, 0);
			}
		}

		// Runキャラの描画
		if (!m_InstanceDataRun.empty())
		{
			ID3D11Buffer* skinVB = nullptr;
			if (m_BakedBuffers.count("Run"))
			{
				auto& perMesh = m_BakedBuffers["Run"][m];
				int frameCount = (int)perMesh.size();
				if (frameCount > 0)
				{
					int frameIndex = 0;
					if (m_LastBakedFrame.count("Run")) frameIndex = m_LastBakedFrame["Run"] % frameCount;
					skinVB = perMesh[frameIndex];
				}
			}
			else
			{
				skinVB = m_SkinOutputBuffer_Run[m];
			}

			if (skinVB) {
				ID3D11Buffer* buffersRun[2] = { skinVB, m_InstanceBufferRun };
				UINT stridesRun[2] = { sizeof(VERTEX_SKIN_OUT), sizeof(InstanceData) };
				UINT offsetsRun[2] = { 0, 0 };
				Renderer::GetDeviceContext()->IASetVertexBuffers(0, 2, buffersRun, stridesRun, offsetsRun);
				Renderer::GetDeviceContext()->IASetIndexBuffer(m_IndexBuffer[m], DXGI_FORMAT_R32_UINT, 0);
				Renderer::AddDrawCall();
				Renderer::GetDeviceContext()->DrawIndexedInstanced(mesh->mNumFaces * 3, (UINT)m_InstanceDataRun.size(), 0, 0, 0);
			}
		}
	}

}

// 解放
void AnimationModel::Uninit()
{
	m_Player->Uninit();

	//GPUリソースの解放
	if (m_VertexBuffer)
	{
		for (unsigned int m = 0; m < m_AiScene->mNumMeshes; m++)
		{
			SafeRelease(m_VertexBuffer[m]);
			if (m_IndexBuffer) SafeRelease(m_IndexBuffer[m]);
			if (m_SkinInputBuffer) SafeRelease(m_SkinInputBuffer[m]);
			if (m_SkinInputSRV) SafeRelease(m_SkinInputSRV[m]);
			if (m_SkinOutputBuffer) SafeRelease(m_SkinOutputBuffer[m]);
			if (m_SkinOutputUAV) SafeRelease(m_SkinOutputUAV[m]);
		}
		SafeDeleteArray(m_VertexBuffer);
	}

	SafeDeleteArray(m_IndexBuffer);
	SafeDeleteArray(m_SkinInputBuffer);
	SafeDeleteArray(m_SkinInputSRV);
	SafeDeleteArray(m_SkinOutputBuffer);
	SafeDeleteArray(m_SkinOutputUAV);

	ReleaseBakedAnimations();

	if (m_VertexBufferGPU)
	{
		for (unsigned int m = 0; m < m_AiScene->mNumMeshes; m++)
			SafeRelease(m_VertexBufferGPU[m]);
		SafeDeleteArray(m_VertexBufferGPU);
	}

	for (unsigned int m = 0; m < m_AiScene->mNumMeshes; m++)
	{
		SafeRelease(m_SkinOutputBuffer_Idle[m]);
		SafeRelease(m_SkinOutputBuffer_Run[m]);
	}

	//シェーダー・定数バッファの解放
	SafeRelease(m_BoneConstantBuffer);
	SafeRelease(m_SkinningCS);
	SafeRelease(m_VertexShader);
	SafeRelease(m_PixelShader);
	SafeRelease(m_VertexLayout);

	//テクスチャの解放
	for (auto& pair : m_Texture) SafeRelease(pair.second);
	m_Texture.clear();

	//アシンプデータの解放
	if (m_AiScene) aiReleaseImport(m_AiScene);
}

// ボーンデータ作成 ノード名＝ボーン名として登録
void AnimationModel::CreateBone(aiNode* node)
{
	BONE bone;
	auto Bones = m_Player->GetBones();
	Bones[node->mName.C_Str()] = bone;

	// 子ノードへ再帰
	for (unsigned int n = 0; n < node->mNumChildren; n++)
	{
		CreateBone(node->mChildren[n]);
	}
}

// コンピュートシェーダ用スキニングバッファ作成
void AnimationModel::CreateComputeSkinningBuffers(VERTEX_3D* vertices, UINT vertexCount, unsigned int meshIndex)
{
	if (vertices == nullptr || vertexCount == 0) {
		// ここで止まるなら、頂点データの抽出に失敗しています
		assert(false && "頂点データが空です");
		return;
	}

	// 入力（Vertex Shader → Compute Shader）バッファ
	ID3D11Device* device = Renderer::GetDevice();
	if (!device) {
		// ここで止まるなら、Rendererの初期化漏れです
		assert(false && "DirectX Deviceが準備できていません");
		return;
	}


	D3D11_BUFFER_DESC inDesc{};
	inDesc.Usage = D3D11_USAGE_DEFAULT;
	inDesc.ByteWidth = sizeof(VERTEX_3D) * vertexCount;
	inDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	inDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	inDesc.StructureByteStride = sizeof(VERTEX_3D);

	D3D11_SUBRESOURCE_DATA initData{};
	initData.pSysMem = vertices;
	device->CreateBuffer(&inDesc, &initData, &m_SkinInputBuffer[meshIndex]);

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.NumElements = vertexCount;
	device->CreateShaderResourceView(m_SkinInputBuffer[meshIndex], &srvDesc, &m_SkinInputSRV[meshIndex]);

	// 出力（Compute Shader → Vertex Shader）バッファ
	D3D11_BUFFER_DESC outDesc{};
	outDesc.Usage = D3D11_USAGE_DEFAULT;
	outDesc.ByteWidth = sizeof(VERTEX_SKIN_OUT) * vertexCount;
	outDesc.StructureByteStride = sizeof(VERTEX_SKIN_OUT);
	outDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	outDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	outDesc.CPUAccessFlags = 0;

	HRESULT hr = device->CreateBuffer(&outDesc, nullptr, &m_SkinOutputBuffer[meshIndex]);
	assert(SUCCEEDED(hr));

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.NumElements = vertexCount;
	hr = device->CreateUnorderedAccessView(m_SkinOutputBuffer[meshIndex], &uavDesc, &m_SkinOutputUAV[meshIndex]);
	assert(SUCCEEDED(hr));

	// IA入力用頂点バッファを作成
	D3D11_BUFFER_DESC vbDesc{};
	vbDesc.Usage = D3D11_USAGE_DEFAULT;
	vbDesc.ByteWidth = sizeof(VERTEX_SKIN_OUT) * vertexCount;
	vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbDesc.CPUAccessFlags = 0;
	vbDesc.MiscFlags = 0;

	HRESULT hr2 = device->CreateBuffer(&vbDesc, nullptr, &m_VertexBufferGPU[meshIndex]);
	assert(SUCCEEDED(hr2));
}

// コンピュートシェーダ読み込み
void AnimationModel::LoadComputeShader(const char* FileName)
{
	FILE* file = fopen(FileName, "rb");
	assert(file);
	long int fsize = _filelength(_fileno(file));
	unsigned char* buffer = new unsigned char[fsize];
	fread(buffer, fsize, 1, file);
	fclose(file);

	HRESULT hr = Renderer::GetDevice()->CreateComputeShader(buffer, fsize, nullptr, &m_SkinningCS);
	if (FAILED(hr)) MessageBox(nullptr, "ComputeShader の作成に失敗しました。", "Error", MB_OK);
	delete[] buffer;
}

// aiMatrix4x4をXMMATRIXに変換
XMMATRIX AnimationModel::ConvertAiMatrixToXMMatrix(const aiMatrix4x4& m)
{
	return XMMATRIX(
		m.a1, m.b1, m.c1, m.d1,
		m.a2, m.b2, m.c2, m.d2,
		m.a3, m.b3, m.c3, m.d3,
		m.a4, m.b4, m.c4, m.d4
	);
}

//頂点構造体の生成
VERTEX_3D* AnimationModel::GetVerticesFromMesh(aiMesh* mesh, unsigned int meshIndex)
{
	VERTEX_3D* vertices = new VERTEX_3D[mesh->mNumVertices];

	for (unsigned int v = 0; v < mesh->mNumVertices; v++)
	{
		// 基本データのコピー
		vertices[v].Position = XMFLOAT3(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);
		vertices[v].Normal = XMFLOAT3(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z);
		vertices[v].TexCoord = XMFLOAT2(mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y);
		vertices[v].Diffuse = XMFLOAT4(1, 1, 1, 1);

		// ボーン情報の初期化
		for (int b = 0; b < MAX_BONE_INFLUENCE; b++) {
			vertices[v].BoneIndex[b] = 0;
			vertices[v].BoneWeight[b] = 0.0f;
		}
	}

	// ボーンウェイトの適用
	for (unsigned int b = 0; b < mesh->mNumBones; b++)
	{
		aiBone* bone = mesh->mBones[b];
		auto BoneNameToIndex = m_Player->GetBoneNameToIndex();
		unsigned int boneIndex = BoneNameToIndex[bone->mName.C_Str()];

		for (unsigned int w = 0; w < bone->mNumWeights; w++)
		{
			aiVertexWeight weight = bone->mWeights[w];
			VERTEX_3D* target = &vertices[weight.mVertexId];

			for (int i = 0; i < MAX_BONE_INFLUENCE; i++)
			{
				if (target->BoneWeight[i] == 0.0f)
				{
					target->BoneIndex[i] = boneIndex;
					target->BoneWeight[i] = weight.mWeight;
					break;
				}
			}
		}
	}

	return vertices;
}

//メッシュ数の取得
int AnimationModel::GetMeshCount() const
{
	if (!m_AiScene) return 0;
	return (int)m_AiScene->mNumMeshes;
}

void AnimationModel::BakeAnimationToDisk(const char* AnimName, const char* OutFilePath)
{
	if (!AnimName) return;
	const aiScene* animScene = m_Resource->GetAnimationScene(AnimName);
	if (!animScene || !animScene->HasAnimations()) return;

	aiAnimation* animation = animScene->mAnimations[0];
	int duration = (int)animation->mDuration;
	if (duration <= 0) return;

	UINT numMeshes = m_AiScene->mNumMeshes;
	ID3D11Device* device = Renderer::GetDevice();
	ID3D11DeviceContext* ctx = Renderer::GetDeviceContext();

	// ステージングバッファをメッシュごとに作成（Mapして読み出すため）
	std::vector<ID3D11Buffer*> staging(numMeshes, nullptr);
	for (UINT m = 0; m < numMeshes; ++m)
	{
		UINT numVertices = m_AiScene->mMeshes[m]->mNumVertices;
		D3D11_BUFFER_DESC sd{};
		sd.Usage = D3D11_USAGE_STAGING;
		sd.ByteWidth = sizeof(VERTEX_SKIN_OUT) * numVertices;
		sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		sd.BindFlags = 0;
		sd.MiscFlags = 0;
		HRESULT hr = device->CreateBuffer(&sd, nullptr, &staging[m]);
		assert(SUCCEEDED(hr));
	}

	// 出力ファイルを開く（バイナリ）
	FILE* f = fopen(OutFilePath, "wb");
	if (!f) {
		for (auto b : staging) if (b) b->Release();
		return;
	}

	// ヘッダ: メッシュ数
	uint32_t meshCountU32 = (uint32_t)numMeshes;
	fwrite(&meshCountU32, sizeof(meshCountU32), 1, f);

	// 各メッシュの基本情報を書き出す（vertexCount, frameCount, stride）
	for (UINT m = 0; m < numMeshes; ++m)
	{
		uint32_t vertexCount = (uint32_t)m_AiScene->mMeshes[m]->mNumVertices;
		uint32_t frameCount = (uint32_t)duration;
		uint32_t stride = (uint32_t)sizeof(VERTEX_SKIN_OUT);
		fwrite(&vertexCount, sizeof(vertexCount), 1, f);
		fwrite(&frameCount, sizeof(frameCount), 1, f);
		fwrite(&stride, sizeof(stride), 1, f);
	}

	// フレームごとにボーン計算 → CS Dispatch → ステージングへコピー → ファイル書き込み
	for (int frame = 0; frame < duration; ++frame)
	{
		m_Player->Update(AnimName, frame, AnimName, frame, 0.0f); 

		// 定数バッファ更新（ボーン行列）
		D3D11_MAPPED_SUBRESOURCE ms{};
		ctx->Map(m_BoneConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
		struct CB_BONE_MATRIX { XMFLOAT4X4 BoneMatrix[MAX_BONE_COUNT]; };
		auto* cbBone = (CB_BONE_MATRIX*)ms.pData;
		auto BoneNameToIndex = m_Player->GetBoneNameToIndex();

		for (auto& pair : BoneNameToIndex)
		{
			unsigned int index = pair.second;
			if (index >= MAX_BONE_COUNT) continue;
			auto Bones = m_Player->GetBones();
			XMMATRIX mat = ConvertAiMatrixToXMMatrix(Bones[pair.first].Matrix);
			XMStoreFloat4x4(&cbBone->BoneMatrix[index], XMMatrixTranspose(mat));
		}
		ctx->Unmap(m_BoneConstantBuffer, 0);

		// 各メッシュで CS を実行して結果をステージングへコピーして書き出す
		for (UINT m = 0; m < numMeshes; ++m)
		{
			UINT numVertices = m_AiScene->mMeshes[m]->mNumVertices;
			UINT groupCount = (numVertices + SKINNING_THREAD_GROUP_SIZE - 1) / SKINNING_THREAD_GROUP_SIZE;

			// CS セット
			ctx->CSSetShader(m_SkinningCS, nullptr, 0);
			ctx->CSSetConstantBuffers(5, 1, &m_BoneConstantBuffer);
			ctx->CSSetShaderResources(0, 1, &m_SkinInputSRV[m]);
			ctx->CSSetUnorderedAccessViews(0, 1, &m_SkinOutputUAV[m], nullptr);

			ctx->Dispatch(groupCount, 1, 1);

			// GPU -> CPU ステージングへコピー
			ctx->CopyResource(staging[m], m_SkinOutputBuffer[m]);

			// Map してファイルへ書き出し
			D3D11_MAPPED_SUBRESOURCE mapped{};
			HRESULT hr = ctx->Map(staging[m], 0, D3D11_MAP_READ, 0, &mapped);
			assert(SUCCEEDED(hr));
			size_t bytes = sizeof(VERTEX_SKIN_OUT) * numVertices;
			fwrite(mapped.pData, 1, bytes, f);
			ctx->Unmap(staging[m], 0);

			// CS のバインド解除
			ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
			ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
			ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
			ctx->CSSetShaderResources(0, 1, nullSRV);
			ctx->CSSetShader(nullptr, nullptr, 0);
		}
	}

	fclose(f);

	// クリーンアップ
	for (auto b : staging) if (b) b->Release();
}

//アニメーションベイクのロード
bool AnimationModel::LoadBakedAnimation(const char* FilePath, const char* AnimName)
{
	if (!FilePath || !AnimName) return false;
	FILE* f = fopen(FilePath, "rb");
	if (!f) return false;

	uint32_t meshCount = 0;
	if (fread(&meshCount, sizeof(meshCount), 1, f) != 1) { fclose(f); return false; }

	// ヘッダ情報を読む（各メッシュの vertexCount, frameCount, stride）
	std::vector<uint32_t> vertexCounts(meshCount), frameCounts(meshCount), strides(meshCount);
	for (uint32_t m = 0; m < meshCount; ++m)
	{
		if (fread(&vertexCounts[m], sizeof(uint32_t), 1, f) != 1) { fclose(f); return false; }
		if (fread(&frameCounts[m], sizeof(uint32_t), 1, f) != 1) { fclose(f); return false; }
		if (fread(&strides[m], sizeof(uint32_t), 1, f) != 1) { fclose(f); return false; }
	}

	// 簡易チェック：meshCount と現在のモデルのメッシュ数が一致すること（必須ではないが安全）
	if (!m_AiScene || meshCount != (uint32_t)m_AiScene->mNumMeshes) {
		// 不一致だと読み込みは危険なので中断
		fclose(f);
		return false;
	}

	ID3D11Device* device = Renderer::GetDevice();
	if (!device) { fclose(f); return false; }

	// メモリ確保：アニメーション名用の二重配列（mesh -> frames）
	m_BakedBuffers[AnimName].assign(meshCount, std::vector<ID3D11Buffer*>());

	// 全フレーム数はすべてのメッシュで同じはず（Bake時の実装に依存）
	uint32_t frameCount = frameCounts[0];
	for (uint32_t m = 0; m < meshCount; ++m) {
		// 各メッシュについてフレーム数が一致することを確認
		if (frameCounts[m] != frameCount) {
			// 異なる場合はシンプルには失敗扱い
			ReleaseBakedAnimations();
			fclose(f);
			return false;
		}
		// バッファ格納領域確保
		m_BakedBuffers[AnimName][m].resize(frameCount, nullptr);
	}

	// ファイルは frame-major で書かれている（Bake時の実装と同順序）
	for (uint32_t frame = 0; frame < frameCount; ++frame)
	{
		for (uint32_t m = 0; m < meshCount; ++m)
		{
			size_t bytes = sizeof(VERTEX_SKIN_OUT) * vertexCounts[m];
			std::vector<uint8_t> tmp(bytes);
			if (fread(tmp.data(), bytes, 1, f) != 1) {
				ReleaseBakedAnimations();
				fclose(f);
				return false;
			}

			D3D11_BUFFER_DESC bd{};
			bd.Usage = D3D11_USAGE_DEFAULT;
			bd.ByteWidth = (UINT)bytes;
			bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			D3D11_SUBRESOURCE_DATA sd{};
			sd.pSysMem = tmp.data();

			HRESULT hr = device->CreateBuffer(&bd, &sd, &m_BakedBuffers[AnimName][m][frame]);
			if (FAILED(hr)) {
				ReleaseBakedAnimations();
				fclose(f);
				return false;
			}
		}
	}

	fclose(f);

	// 初期フレームを 0 にセット
	m_LastBakedFrame[AnimName] = 0;
	return true;
}

void AnimationModel::ReleaseBakedAnimations()
{
	for (auto& pair : m_BakedBuffers)
	{
		for (auto& meshVec : pair.second)
		{
			for (auto* buf : meshVec) { if (buf) buf->Release(); }
		}
	}
	m_BakedBuffers.clear();
	m_LastBakedFrame.clear();
}

void AnimationModel::ResetDebugCounters()
{
	m_ComputeDispatchCount = 0;
	m_AnimationUsedBaked.clear();
}

int AnimationModel::GetComputeDispatchCount() const
{
	return m_ComputeDispatchCount;
}

bool AnimationModel::WasAnimationBakedThisFrame(const char* AnimName) const
{
	if (!AnimName) return false;
	auto it = m_AnimationUsedBaked.find(AnimName);
	if (it == m_AnimationUsedBaked.end()) return false;
	return it->second;
}

