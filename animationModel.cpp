#include "main.h"
#include "renderer.h"
#include "animationModel.h"
#include "utility.h"
#include <d3dcompiler.h>
#include <io.h>
#include "vector3.h"
#include "player.h"
#include "modelResource.h"
#include "animationPlayer.h"
#include "instanceDataManager.h"
#include "animationModelRenderer.h"
#include "computeSkinningManager.h"
#include "animationBakeManager.h"

using namespace DirectX;

void AnimationModel::Load(const char* FileName)
{
	// リソースの実体を作成
	if (!m_Resource) m_Resource = new ModelResource();
	m_AiScene = m_Resource->LoadModel(FileName);
	assert(m_AiScene);

	if (!m_Player)m_Player = new AnimationPlayer();
	if (!m_InstanceManager) m_InstanceManager = new InstanceDataManager();
	if (!m_ComputeSkinningManager) m_ComputeSkinningManager = new ComputeSkinningManager();
	if (!m_BakeManager) m_BakeManager = new AnimationBakeManager();	

	UINT numMeshes = m_AiScene->mNumMeshes;

	//インスタンス固有のバッファ配列の確保
	m_VertexBuffer = new ID3D11Buffer * [numMeshes];
	m_IndexBuffer = new ID3D11Buffer * [numMeshes];
	m_SkinInputBuffer = new ID3D11Buffer * [numMeshes] {};
	m_SkinInputSRV = new ID3D11ShaderResourceView * [numMeshes] {};
	m_SkinOutputBuffer = new ID3D11Buffer * [numMeshes] {};
	m_SkinOutputUAV = new ID3D11UnorderedAccessView * [numMeshes] {};
	m_CS_VertexBufferGPU = new ID3D11Buffer * [numMeshes] {};

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

	m_ComputeSkinningManager->LoadComputeShader("shader\\CSSkinningCS.cso");
	Renderer::CreateCSSkinningVertexShader(&m_CS_VertexShader, &m_CS_VertexLayout, "shader\\CSSkinningVS.cso");
	Renderer::CreateVSSkinningVertexShader(&m_VS_VertexShader, &m_VS_VertexLayout, "shader\\VSSkinningVS.cso");
	Renderer::CreatePixelShader(&m_CS_PixelShader, "shader\\SkinningPS.cso");
	Renderer::CreatePixelShader(&m_VS_PixelShader, "shader\\SkinningPS.cso");

}

// アニメーション読み込み
void AnimationModel::LoadAnimation(const char* FileName, const char* Name)
{
	m_Resource->LoadAnimation(FileName, Name);

	if (m_Player) 
	{ 
		BuildVSBoneMatrixBuffer(Name); 
	}
}

// アニメーション更新
void AnimationModel::Update(const char* Anim1, int Frame1, const char* Anim2, int Frame2, float BlendRate)
{
	if (m_SkinningMode == SkinningMode::ComputeShader)
	{
		if (Anim1 && m_BakedBuffers.count(Anim1))
		{
			m_LastBakedFrame[Anim1] = Frame1;
			m_AnimationUsedBaked[Anim1] = true;
			return;
		}
	}
	else
	{
		if (Anim1) m_LastBakedFrame[Anim1] = Frame1;
		return;
	}

	// アニメーションシーンの取得と更新
	if (!Anim1 || !Anim2) return;
	const aiScene* s1 = m_Resource->GetAnimationScene(Anim1);
	const aiScene* s2 = m_Resource->GetAnimationScene(Anim2);
	if (!s1 || !s2) return;
	if (!s1->HasAnimations() || !s2->HasAnimations()) return;

	m_Player->Update(Anim1, Frame1, Anim2, Frame2, BlendRate);

	// ボーン行列を定数バッファに転送
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

	std::string animName = Anim1 ? Anim1 : "";

	// 各メッシュごとにスキニングCSを実行して頂点変形
	for (unsigned int m = 0; m < m_AiScene->mNumMeshes; m++)
	{
		UINT numVertices = m_AiScene->mMeshes[m]->mNumVertices;

		m_ComputeDispatchCount++;
		if (Anim1) m_AnimationUsedBaked[Anim1] = false;

		m_ComputeSkinningManager->Dispatch(
			m_BoneConstantBuffer,
			m_SkinInputSRV[m],
			m_SkinOutputUAV[m],
			numVertices
		);

		ID3D11Buffer** targetBuffer = nullptr;
		if (animName == "Idle") targetBuffer = &m_SkinOutputBuffer_Idle[m];
		else if (animName == "Run")  targetBuffer = &m_SkinOutputBuffer_Run[m];

		if (targetBuffer)
		{
			if (!*targetBuffer)
			{
				D3D11_BUFFER_DESC bd{};
				bd.Usage = D3D11_USAGE_DEFAULT;
				bd.ByteWidth = sizeof(VERTEX_SKIN_OUT) * numVertices;
				bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
				Renderer::GetDevice()->CreateBuffer(&bd, nullptr, targetBuffer);
			}
			Renderer::GetDeviceContext()->CopyResource(*targetBuffer, m_SkinOutputBuffer[m]);
		}

	}
}

// インスタンスデータ更新(視錐台カリングあり)
void AnimationModel::UpdateInstanceData(const std::vector<Player*>& players, const XMMATRIX& view, const XMMATRIX& proj)
{
	if (!m_InstanceManager) return;
	m_InstanceManager->UpdateWithCulling(players, m_Resource, view, proj);
}

// インスタンスデータ更新（視錐台カリングなし）
void AnimationModel::UpdateInstanceData(const std::vector<Player*>& players)
{
	if (!m_InstanceManager) return;
	m_InstanceManager->Update(players, m_Resource);
}

// 描画
void AnimationModel::Draw() {
	switch (m_SkinningMode) {
	case SkinningMode::ComputeShader: DrawCS(); break;
	case SkinningMode::VertexShader:  DrawVS(); break;
	}
}

// CS描画
void AnimationModel::DrawCS()
{
	if (!m_InstanceManager) return;

	//シェーダーと入力レイアウトの設定
	Renderer::GetDeviceContext()->VSSetShader(m_CS_VertexShader, NULL, 0);
	Renderer::GetDeviceContext()->PSSetShader(m_CS_PixelShader, NULL, 0);
	Renderer::GetDeviceContext()->IASetInputLayout(m_CS_VertexLayout);
	Renderer::GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


	//メッシュ単位で描画
	for (unsigned int m = 0; m < m_AiScene->mNumMeshes; m++)
	{
		aiMesh* mesh = m_AiScene->mMeshes[m];
		aiMaterial* aimaterial = m_AiScene->mMaterials[mesh->mMaterialIndex];

		AnimationModelRenderer::ApplyMaterial(aimaterial, m_Resource);

		// Idleキャラの描画
		DrawCSAnimation(
			m,
			mesh,
			"Idle",
			m_InstanceManager->GetIdleBuffer(),
			(UINT)m_InstanceManager->GetIdleData().size()
		);
		// Runキャラの描画
		DrawCSAnimation(
			m,
			mesh,
			"Run",
			m_InstanceManager->GetRunBuffer(),
			(UINT)m_InstanceManager->GetRunData().size()
		);
	}
}

// VS描画
void AnimationModel::DrawVS()
{
	if (!m_InstanceManager) return;

	Renderer::GetDeviceContext()->VSSetShader(m_VS_VertexShader, NULL, 0);
	Renderer::GetDeviceContext()->PSSetShader(m_VS_PixelShader, NULL, 0);
	Renderer::GetDeviceContext()->IASetInputLayout(m_VS_VertexLayout);
	Renderer::GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


	for (unsigned int m = 0; m < m_AiScene->mNumMeshes; m++)
	{
		aiMesh* mesh = m_AiScene->mMeshes[m];
		aiMaterial* aimaterial = m_AiScene->mMaterials[mesh->mMaterialIndex];

		AnimationModelRenderer::ApplyMaterial(aimaterial, m_Resource);


		//Idle 描画
		DrawVSAnimation(
			m,
			mesh,
			m_InstanceManager->GetIdleBuffer(),
			(UINT)m_InstanceManager->GetIdleData().size(),
			m_VS_BoneMatrixSRV_Idle
		);
		//Run 描画
		DrawVSAnimation(
			m,
			mesh,
			m_InstanceManager->GetRunBuffer(),
			(UINT)m_InstanceManager->GetRunData().size(),
			m_VS_BoneMatrixSRV_Run
		);
	}

	// SRV のバインド解除
	ID3D11ShaderResourceView* nullSRV = nullptr;
	Renderer::GetDeviceContext()->VSSetShaderResources(1, 1, &nullSRV);
}

void AnimationModel::DrawCSAnimation(unsigned int meshIndex, aiMesh* mesh, const char* animName, ID3D11Buffer* instanceBuffer, UINT instanceCount)
{
	if (!mesh || !animName) return;
	if (!instanceBuffer || instanceCount == 0) return;

	ID3D11Buffer* skinVB = nullptr;
	std::string name = animName;

	if (m_BakedBuffers.count(name))
	{
		auto& perMesh = m_BakedBuffers[name][meshIndex];
		int frameCount = (int)perMesh.size();

		if (frameCount > 0)
		{
			int frameIndex = 0;

			if (m_LastBakedFrame.count(name))
			{
				frameIndex = m_LastBakedFrame[name] % frameCount;
			}

			skinVB = perMesh[frameIndex];
		}
	}
	else
	{
		if (name == "Idle")
		{
			skinVB = m_SkinOutputBuffer_Idle[meshIndex];
		}
		else if (name == "Run")
		{
			skinVB = m_SkinOutputBuffer_Run[meshIndex];
		}
	}

	if (!skinVB) return;

	AnimationModelRenderer::DrawInstancedMesh(
		skinVB,
		instanceBuffer,
		m_IndexBuffer[meshIndex],
		sizeof(VERTEX_SKIN_OUT),
		instanceCount,
		mesh->mNumFaces * 3
	);
}

void AnimationModel::DrawVSAnimation(unsigned int meshIndex, aiMesh* mesh, ID3D11Buffer* instanceBuffer, UINT instanceCount, ID3D11ShaderResourceView* boneSRV)
{
	if (!mesh) return;
	if (!instanceBuffer || instanceCount == 0) return;
	if (!boneSRV) return;

	Renderer::GetDeviceContext()->VSSetShaderResources(1, 1, &boneSRV);

	AnimationModelRenderer::DrawInstancedMesh(
		m_VertexBuffer[meshIndex],
		instanceBuffer,
		m_IndexBuffer[meshIndex],
		sizeof(VERTEX_3D),
		instanceCount,
		mesh->mNumFaces * 3
	);
}

// 解放
void AnimationModel::Uninit()
{
	if (m_AiScene)
	{
		for (unsigned int m = 0; m < m_AiScene->mNumMeshes; m++)
		{
			SafeRelease(m_VertexBuffer[m]);
			SafeRelease(m_IndexBuffer[m]);

			SafeRelease(m_SkinInputBuffer[m]);
			SafeRelease(m_SkinInputSRV[m]);

			SafeRelease(m_SkinOutputBuffer[m]);
			SafeRelease(m_SkinOutputUAV[m]);

			SafeRelease(m_CS_VertexBufferGPU[m]);

			SafeRelease(m_SkinOutputBuffer_Idle[m]);
			SafeRelease(m_SkinOutputBuffer_Run[m]);
		}
	}

	SafeDeleteArray(m_VertexBuffer);
	SafeDeleteArray(m_IndexBuffer);
	SafeDeleteArray(m_SkinInputBuffer);
	SafeDeleteArray(m_SkinInputSRV);
	SafeDeleteArray(m_SkinOutputBuffer);
	SafeDeleteArray(m_SkinOutputUAV);
	SafeDeleteArray(m_CS_VertexBufferGPU);

	ReleaseBakedAnimations();

	SafeRelease(m_BoneConstantBuffer);

	SafeRelease(m_CS_VertexShader);
	SafeRelease(m_CS_PixelShader);
	SafeRelease(m_CS_VertexLayout);

	SafeRelease(m_VS_VertexLayout);
	SafeRelease(m_VS_VertexShader);
	SafeRelease(m_VS_PixelShader);

	SafeRelease(m_VS_BoneMatrixBuffer_Idle);
	SafeRelease(m_VS_BoneMatrixSRV_Idle);
	SafeRelease(m_VS_BoneMatrixBuffer_Run);
	SafeRelease(m_VS_BoneMatrixSRV_Run);

	if (m_InstanceManager)
	{
		m_InstanceManager->Uninit();
		delete m_InstanceManager;
		m_InstanceManager = nullptr;
	}

	if (m_ComputeSkinningManager)
	{
		m_ComputeSkinningManager->Uninit();
		delete m_ComputeSkinningManager;
		m_ComputeSkinningManager = nullptr;
	}

	if (m_BakeManager)
	{
		delete m_BakeManager;
		m_BakeManager = nullptr;
	}

	if (m_Player)
	{
		m_Player->Uninit();
		delete m_Player;
		m_Player = nullptr;
	}

	if (m_Resource)
	{
		m_Resource->Uninit();
		delete m_Resource;
		m_Resource = nullptr;
	}

	m_AiScene = nullptr;
}

// コンピュートシェーダ用スキニングバッファ作成
void AnimationModel::CreateComputeSkinningBuffers(VERTEX_3D* vertices, UINT vertexCount, unsigned int meshIndex)
{
	m_ComputeSkinningManager->CreateSkinningBuffers(
		vertices,
		vertexCount,
		&m_SkinInputBuffer[meshIndex],
		&m_SkinInputSRV[meshIndex],
		&m_SkinOutputBuffer[meshIndex],
		&m_SkinOutputUAV[meshIndex],
		&m_CS_VertexBufferGPU[meshIndex]
	);
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

// VS用のボーン行列バッファを作成
void AnimationModel::BuildVSBoneMatrixBuffer(const char* AnimName)
{
	const aiScene* animScene = m_Resource->GetAnimationScene(AnimName);
	if (!animScene || !animScene->HasAnimations()) return;

	int duration = (int)animScene->mAnimations[0]->mDuration;
	if (duration <= 0) return;

	// 全フレーム × MAX_BONE_COUNT 分の行列を確保
	// レイアウト: allMatrices[frame * MAX_BONE_COUNT + boneIndex]
	std::vector<XMFLOAT4X4> allMatrices((size_t)duration * MAX_BONE_COUNT);

	// フレームごとにボーン行列を計算
	for (int frame = 0; frame < duration; frame++)
	{
		m_Player->Update(AnimName, frame, AnimName, frame, 0.0f);

		const auto& bones = m_Player->GetBones();
		const auto& boneNameToIndex = m_Player->GetBoneNameToIndex();

		for (auto& pair : boneNameToIndex)
		{
			unsigned int boneIdx = pair.second;
			if (boneIdx >= MAX_BONE_COUNT) continue;

			XMMATRIX mat = ConvertAiMatrixToXMMatrix(bones.at(pair.first).Matrix);
			XMStoreFloat4x4(
				&allMatrices[(size_t)frame * MAX_BONE_COUNT + boneIdx],
				XMMatrixTranspose(mat)
			);
		}
	}

	// 書き込み先のバッファ・SRV ポインタをアニメ名で選ぶ
	ID3D11Buffer** targetBuf = nullptr;
	ID3D11ShaderResourceView** targetSRV = nullptr;
	std::string name = AnimName;

	if (name == "Idle") { targetBuf = &m_VS_BoneMatrixBuffer_Idle; targetSRV = &m_VS_BoneMatrixSRV_Idle; }
	else if (name == "Run") { targetBuf = &m_VS_BoneMatrixBuffer_Run;  targetSRV = &m_VS_BoneMatrixSRV_Run; }

	if (!targetBuf) return; // 未対応のアニメ名なら何もしない

	// 既存バッファを解放（再ロード対応）
	if (*targetBuf) { (*targetBuf)->Release(); *targetBuf = nullptr; }
	if (*targetSRV) { (*targetSRV)->Release(); *targetSRV = nullptr; }

	// StructuredBuffer 作成
	D3D11_BUFFER_DESC bd{};
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = (UINT)(sizeof(XMFLOAT4X4) * duration * MAX_BONE_COUNT);
	bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bd.StructureByteStride = sizeof(XMFLOAT4X4);

	D3D11_SUBRESOURCE_DATA sd{};
	sd.pSysMem = allMatrices.data();

	HRESULT hr = Renderer::GetDevice()->CreateBuffer(&bd, &sd, targetBuf);
	assert(SUCCEEDED(hr));

	// SRV 作成
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = (UINT)(duration * MAX_BONE_COUNT);

	hr = Renderer::GetDevice()->CreateShaderResourceView(*targetBuf, &srvDesc, targetSRV);
	assert(SUCCEEDED(hr));
}

//メッシュ数の取得
int AnimationModel::GetMeshCount() const
{
	if (!m_AiScene) return 0;
	return (int)m_AiScene->mNumMeshes;
}

void AnimationModel::BakeAnimationToDisk(const char* AnimName, const char* OutFilePath)
{
	if (!m_BakeManager) return;

	const aiScene* animScene = m_Resource->GetAnimationScene(AnimName);
	if (!animScene) return;

	m_BakeManager->BakeAnimation(
		AnimName,
		OutFilePath,

		m_Player,

		m_AiScene,
		animScene,

		m_BoneConstantBuffer,
		m_ComputeSkinningManager,

		m_SkinInputSRV,
		m_SkinOutputUAV,
		m_SkinOutputBuffer,

		MAX_BONE_COUNT
	);
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

//ベイクしたアニメーションの解放
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

//今フレームで何回コンピュートシェーダを呼んだか、どのアニメーションをベイク版で描画したか
void AnimationModel::ResetDebugCounters()
{
	m_ComputeDispatchCount = 0;
	m_AnimationUsedBaked.clear();
}

// コンピュートシェーダ呼び出し回数をインクリメント
int AnimationModel::GetComputeDispatchCount() const
{
	return m_ComputeDispatchCount;
}

// アニメーション名を指定して、そのアニメーションでベイク版を使用したかどうかを取得
bool AnimationModel::WasAnimationBakedThisFrame(const char* AnimName) const
{
	if (!AnimName) return false;
	auto it = m_AnimationUsedBaked.find(AnimName);
	if (it == m_AnimationUsedBaked.end()) return false;
	return it->second;
}

int AnimationModel::GetTotalInstanceCount() const
{
	if (!m_InstanceManager) return 0;
	return m_InstanceManager->GetTotalInstanceCount();
}