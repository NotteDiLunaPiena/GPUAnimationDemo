#include "main.h"
#include "renderer.h"
#include "animationModel.h"
#include "utility.h"
#include "player.h"
#include <d3dcompiler.h>
#include <corecrt_io.h>

using namespace DirectX;

// モデル読み込み
void AnimationModel::Load(const char* FileName)
{
    //モデルファイルパス
    const std::string modelPath(FileName);

    //アシンプでのモデル読み込み
    m_AiScene = aiImportFile(FileName, aiProcessPreset_TargetRealtime_MaxQuality | aiProcess_ConvertToLeftHanded);
    assert(m_AiScene);

    //メッシュ数分のバッファ配列の確保
    m_VertexBuffer = new ID3D11Buffer * [m_AiScene->mNumMeshes];
    m_IndexBuffer = new ID3D11Buffer * [m_AiScene->mNumMeshes];

    // ComputeShader用スキニング関連バッファ
    m_SkinInputBuffer = new ID3D11Buffer * [m_AiScene->mNumMeshes] {};
    m_SkinInputSRV = new ID3D11ShaderResourceView * [m_AiScene->mNumMeshes] {};
    m_SkinOutputBuffer = new ID3D11Buffer * [m_AiScene->mNumMeshes] {};
    m_SkinOutputUAV = new ID3D11UnorderedAccessView * [m_AiScene->mNumMeshes] {};
    m_VertexBufferGPU = new ID3D11Buffer * [m_AiScene->mNumMeshes] {};

    // Idle / Run 用のスキニング結果バッファ初期化
    for (unsigned int i = 0; i < m_AiScene->mNumMeshes; i++)
    {
        m_SkinOutputBuffer_Idle[i] = nullptr;
        m_SkinOutputBuffer_Run[i] = nullptr;
    }

    //ボーンツリー作成
    CreateBone(m_AiScene->mRootNode);

	//ボーン名とボーン配列インデックスを対応付け
    unsigned int globalBoneIndex = 0;
    for (auto& pair : m_Bone)
    {
        m_BoneNameToIndex[pair.first] = globalBoneIndex++;
    }

    //ボーン行列用定数バッファ作成
    {
		//ボーン行列用定数バッファ構造体 最大256本
        struct CB_BONE_MATRIX { XMFLOAT4X4 BoneMatrix[256]; };

        D3D11_BUFFER_DESC bd{};
        bd.Usage = D3D11_USAGE_DYNAMIC;             //毎フレーム更新
        bd.ByteWidth = sizeof(CB_BONE_MATRIX);
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;  //定数バッファ
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        Renderer::GetDevice()->CreateBuffer(&bd, nullptr, &m_BoneConstantBuffer);
        assert(m_BoneConstantBuffer);
    }

    //各メッシュの頂点・インデックス作成
    for (unsigned int m = 0; m < m_AiScene->mNumMeshes; m++)
    {
        aiMesh* mesh = m_AiScene->mMeshes[m];
        VERTEX_3D* vertex = new VERTEX_3D[mesh->mNumVertices];

        //頂点データの初期化
        for (unsigned int v = 0; v < mesh->mNumVertices; v++)
        {
            vertex[v].Position = XMFLOAT3(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);
            vertex[v].Normal = XMFLOAT3(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z);
            vertex[v].TexCoord = XMFLOAT2(mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y);
            vertex[v].Diffuse = XMFLOAT4(1, 1, 1, 1);

            for (int b = 0; b < 4; b++) { vertex[v].BoneIndex[b] = 0; vertex[v].BoneWeight[b] = 0.0f; }
        }

        //ボーンウェイトの適用
        for (unsigned int b = 0; b < mesh->mNumBones; b++)
        {
            aiBone* bone = mesh->mBones[b];
            //モデル空間→ボーン空間へ
            m_Bone[bone->mName.C_Str()].OffsetMatrix = bone->mOffsetMatrix;

            //ボーンインデックスの取得
            unsigned int boneIndex = m_BoneNameToIndex[bone->mName.C_Str()];
            for (unsigned int w = 0; w < bone->mNumWeights; w++)
            {
				//ボーンウェイト情報を登録
                aiVertexWeight weight = bone->mWeights[w];
                VERTEX_3D* target = &vertex[weight.mVertexId];

                for (int i = 0; i < 4; i++)
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

		//ComputeShader用スキニングバッファ作成
        CreateComputeSkinningBuffers(vertex, mesh->mNumVertices, m);

        //頂点バッファの作成
        {
            D3D11_BUFFER_DESC bd{};
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.ByteWidth = sizeof(VERTEX_3D) * mesh->mNumVertices;
            bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

            D3D11_SUBRESOURCE_DATA sd{};
            sd.pSysMem = vertex;

            Renderer::GetDevice()->CreateBuffer(&bd, &sd, &m_VertexBuffer[m]);
        }
        delete[] vertex;

        //インデックスバッファの作成
        {
            unsigned int* index = new unsigned int[mesh->mNumFaces * 3];
            for (unsigned int f = 0; f < mesh->mNumFaces; f++)
            {
                const aiFace* face = &mesh->mFaces[f];
                assert(face->mNumIndices == 3);
                index[f * 3 + 0] = face->mIndices[0];
                index[f * 3 + 1] = face->mIndices[1];
                index[f * 3 + 2] = face->mIndices[2];
            }

            D3D11_BUFFER_DESC bd{};
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.ByteWidth = sizeof(unsigned int) * mesh->mNumFaces * 3;
            bd.BindFlags = D3D11_BIND_INDEX_BUFFER;

            D3D11_SUBRESOURCE_DATA sd{};
            sd.pSysMem = index;

            Renderer::GetDevice()->CreateBuffer(&bd, &sd, &m_IndexBuffer[m]);
            delete[] index;
        }
    }

    //埋め込みテクスチャ読み込み
    for (int i = 0; i < m_AiScene->mNumTextures; i++)
    {
        aiTexture* aitexture = m_AiScene->mTextures[i];
        ID3D11ShaderResourceView* texture;

        TexMetadata metadata;
        ScratchImage image;
        LoadFromWICMemory(aitexture->pcData, aitexture->mWidth, WIC_FLAGS_NONE, &metadata, image);
        CreateShaderResourceView(Renderer::GetDevice(), image.GetImages(), image.GetImageCount(), metadata, &texture);
        assert(texture);

        m_Texture[aitexture->mFilename.data] = texture;
    }

    //シェーダーの設定
    LoadComputeShader("shader\\SkinningCS.cso");
    Renderer::CreateSkinningVertexShader(&m_VertexShader, &m_VertexLayout, "shader\\skinningVS.cso");
    Renderer::CreatePixelShader(&m_PixelShader, "shader\\skinningPS.cso");

}

// アニメーション読み込み
void AnimationModel::LoadAnimation(const char* FileName, const char* Name)
{
    m_Animation[Name] = aiImportFile(FileName, aiProcess_ConvertToLeftHanded);
    assert(m_Animation[Name]);
}

// アニメーション更新
void AnimationModel::Update(const char* Anim1, int Frame1, const char* Anim2, int Frame2, float BlendRate)
{
    std::string animName = Anim1 ? Anim1 : "";

    //アニメーションの存在チェック
    if (!Anim1 || !Anim2) return;
    if (m_Animation.count(Anim1) == 0 || m_Animation.count(Anim2) == 0) return;
    if (!m_Animation[Anim1]->HasAnimations() || !m_Animation[Anim2]->HasAnimations()) return;

    //ボーンノードのローカル行列を初期化
    UpdateLocalAnimationMatrix(m_AiScene->mRootNode);

    //ブレンドするアニメーションの取得
    aiAnimation* animation1 = m_Animation[Anim1]->mAnimations[0];
    aiAnimation* animation2 = m_Animation[Anim2]->mAnimations[0];

    //各ボーン更新
    for (auto& pair : m_Bone)
    {
        BONE* bone = &m_Bone[pair.first];

		//アニメーションチャネルの検索
        aiNodeAnim* nodeAnim1 = nullptr;
        aiNodeAnim* nodeAnim2 = nullptr;

        for (unsigned int c = 0; c < animation1->mNumChannels; c++)
            if (animation1->mChannels[c]->mNodeName == aiString(pair.first))
                nodeAnim1 = animation1->mChannels[c];

        for (unsigned int c = 0; c < animation2->mNumChannels; c++)
            if (animation2->mChannels[c]->mNodeName == aiString(pair.first))
                nodeAnim2 = animation2->mChannels[c];

        aiQuaternion rot1, rot2;
        aiVector3D pos1, pos2;

		//フレームの位置・回転を取得
        if (nodeAnim1)
        {
            int f1 = Frame1 % nodeAnim1->mNumRotationKeys;
            rot1 = nodeAnim1->mRotationKeys[f1].mValue;
            pos1 = nodeAnim1->mPositionKeys[f1 % nodeAnim1->mNumPositionKeys].mValue;
        }

        if (nodeAnim2)
        {
            int f2 = Frame2 % nodeAnim2->mNumRotationKeys;
            rot2 = nodeAnim2->mRotationKeys[f2].mValue;
            pos2 = nodeAnim2->mPositionKeys[f2 % nodeAnim2->mNumPositionKeys].mValue;
        }

		//ブレンド計算
        //LERP
        aiVector3D pos = pos1 * (1.0f - BlendRate) + pos2 * BlendRate;
		//SLERP
        aiQuaternion rot; aiQuaternion::Interpolate(rot, rot1, rot2, BlendRate);

		// アニメーション変換行列の作成
        bone->AnimationMatrix = aiMatrix4x4(aiVector3D(1, 1, 1), rot, pos);
    }

	//最終的なボーン行列を計算
    aiMatrix4x4 identityMatrix;
    UpdateBoneMatrix(m_AiScene->mRootNode, identityMatrix);

    //定数バッファの更新
    D3D11_MAPPED_SUBRESOURCE ms;
    Renderer::GetDeviceContext()->Map(m_BoneConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
    struct CB_BONE_MATRIX { XMFLOAT4X4 BoneMatrix[256]; };
    auto* cbBone = (CB_BONE_MATRIX*)ms.pData;

    for (auto& pair : m_BoneNameToIndex)
    {
        unsigned int index = pair.second;
        if (index >= 256) continue;

        XMMATRIX mat = ConvertAiMatrixToXMMatrix(m_Bone[pair.first].Matrix);
        XMStoreFloat4x4(&cbBone->BoneMatrix[index], XMMatrixTranspose(mat));
    }
    Renderer::GetDeviceContext()->Unmap(m_BoneConstantBuffer, 0);

	//各メッシュごとにスキニング処理を実行
    for (unsigned int m = 0; m < m_AiScene->mNumMeshes; m++)
    {
        UINT numVertices = m_AiScene->mMeshes[m]->mNumVertices;
        UINT groupCount = (numVertices + 255) / 256;

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
				desc.Usage = D3D11_USAGE_DYNAMIC;               //毎フレーム更新
                desc.ByteWidth = sizeof(InstanceData) * MAX_INSTANCE_COUNT;
				desc.BindFlags = D3D11_BIND_VERTEX_BUFFER; 
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;   //CPU書き込み可能   
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

        aiString texture;
        aiColor3D diffuse;
        float opacity;

		//マテリアル・テクスチャの取得
        aimaterial->GetTexture(aiTextureType_DIFFUSE, 0, &texture);
        aimaterial->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
        aimaterial->Get(AI_MATKEY_OPACITY, opacity);

		//テクスチャの設定
        if (texture != aiString("") && m_Texture.count(texture.data))
        {
            Renderer::GetDeviceContext()->PSSetShaderResources(0, 1, &m_Texture[texture.data]);
            material.TextureEnable = true;
        }
        else
        {
            material.TextureEnable = false;
        }

		//マテリアルの設定
        material.Diffuse = XMFLOAT4(diffuse.r, diffuse.g, diffuse.b, opacity);
        material.Ambient = material.Diffuse;
        Renderer::SetMaterial(material);

        // Idleキャラの描画
        if (!m_InstanceDataIdle.empty())
        {
			// スキニング後の頂点バッファとインスタンスバッファをセット
            ID3D11Buffer* buffersIdle[2] = { m_SkinOutputBuffer_Idle[m], m_InstanceBufferIdle };
            UINT stridesIdle[2] = { sizeof(VERTEX_SKIN_OUT), sizeof(InstanceData) };
            UINT offsetsIdle[2] = { 0, 0 };
            Renderer::GetDeviceContext()->IASetVertexBuffers(0, 2, buffersIdle, stridesIdle, offsetsIdle);
            Renderer::GetDeviceContext()->IASetIndexBuffer(m_IndexBuffer[m], DXGI_FORMAT_R32_UINT, 0);
            Renderer::AddDrawCall();
			// インスタンス描画
            Renderer::GetDeviceContext()->DrawIndexedInstanced(mesh->mNumFaces * 3, (UINT)m_InstanceDataIdle.size(), 0, 0, 0);
        
        }

        // Runキャラの描画
        if (!m_InstanceDataRun.empty())
        {
			// スキニング後の頂点バッファとインスタンスバッファをセット
            ID3D11Buffer* buffersRun[2] = { m_SkinOutputBuffer_Run[m], m_InstanceBufferRun };
            UINT stridesRun[2] = { sizeof(VERTEX_SKIN_OUT), sizeof(InstanceData) };
            UINT offsetsRun[2] = { 0, 0 };
            Renderer::GetDeviceContext()->IASetVertexBuffers(0, 2, buffersRun, stridesRun, offsetsRun);
            Renderer::GetDeviceContext()->IASetIndexBuffer(m_IndexBuffer[m], DXGI_FORMAT_R32_UINT, 0);
            Renderer::AddDrawCall();
			// インスタンス描画
            Renderer::GetDeviceContext()->DrawIndexedInstanced(mesh->mNumFaces * 3, (UINT)m_InstanceDataRun.size(), 0, 0, 0);
        
        }
    }

}

// 解放
void AnimationModel::Uninit()
{
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
    for (auto& pair : m_Animation) aiReleaseImport(pair.second);
    m_Animation.clear();
}

// ボーンデータ作成 ノード名＝ボーン名として登録
void AnimationModel::CreateBone(aiNode* node)
{
    BONE bone;
    m_Bone[node->mName.C_Str()] = bone;
    
	// 子ノードへ再帰
    for (unsigned int n = 0; n < node->mNumChildren; n++)
    {
        CreateBone(node->mChildren[n]);
    }
}

// ボーン行列更新
void AnimationModel::UpdateBoneMatrix(aiNode* node, aiMatrix4x4 ParentMatrix)
{
    BONE* bone = &m_Bone[node->mName.C_Str()];
    
    //親の行列と自身のアニメーション行列を合成
    aiMatrix4x4 GlobalTransform = ParentMatrix * bone->AnimationMatrix;

	//スキニング用の最終ボーン行列を計算
    bone->Matrix = GlobalTransform * bone->OffsetMatrix;

	//子ノードへ再帰
    for (unsigned int n = 0; n < node->mNumChildren; n++)
        UpdateBoneMatrix(node->mChildren[n], GlobalTransform);
}

// ローカルアニメーション行列更新　（アニメーション適用前の初期化）
void AnimationModel::UpdateLocalAnimationMatrix(aiNode* node)
{

    // ボーンがマップに存在する場合のみ
    if (m_Bone.count(node->mName.C_Str())) 
    {
        BONE& bone = m_Bone.at(node->mName.C_Str());

        bone.AnimationMatrix = node->mTransformation;
    }

    // 子ノードへ再帰
    for (unsigned int n = 0; n < node->mNumChildren; n++)
        UpdateLocalAnimationMatrix(node->mChildren[n]);
}

// コンピュートシェーダ用スキニングバッファ作成
void AnimationModel::CreateComputeSkinningBuffers(VERTEX_3D* vertices, UINT vertexCount, unsigned int meshIndex)
{
	// 入力（Vertex Shader → Compute Shader）バッファ
    ID3D11Device* device = Renderer::GetDevice();

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

// 指定アニメーションの総フレーム数を取得
int AnimationModel::GetAnimationDuration(const char* AnimationName) const
{
    if (m_Animation.count(AnimationName) == 0) return 0;
    const aiScene* scene = m_Animation.at(AnimationName);
    if (!scene->HasAnimations()) return 0;
    return (int)scene->mAnimations[0]->mDuration;
}

// 現在フレームの進行率（0.0〜1.0）を取得
float AnimationModel::GetAnimationCurrentFramePercentage(const char* AnimationName, int CurrentFrame) const
{
    int duration = GetAnimationDuration(AnimationName);
    if (duration <= 0) return 0.0f;
    int normalized = CurrentFrame % duration;
    return (float)normalized / duration;
}

// アニメーションフレームを1進める（ループ）
void AnimationModel::AdvanceFrame(const char* AnimationName, int& CurrentFrame)
{
    int duration = GetAnimationDuration(AnimationName);
    if (duration <= 0) return;

    CurrentFrame = (CurrentFrame + 1) % duration; 
}

// アニメーションが最終フレームに到達したか判定
bool AnimationModel::IsAnimationEnd(const char* AnimationName, int CurrentFrame)
{
    int duration = GetAnimationDuration(AnimationName);
    if (duration <= 0) return true;

    return (CurrentFrame >= duration - 1); 
}

//メッシュ数の取得
int AnimationModel::GetMeshCount() const
{
    if (!m_AiScene) return 0;
    return (int)m_AiScene->mNumMeshes;
}