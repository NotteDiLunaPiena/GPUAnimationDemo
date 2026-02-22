#include "main.h"
#include "texture.h"
#include "renderer.h"
#include "modelResource.h"

//モデルの読み込み
const aiScene* ModelResource::LoadModel(const char* fileName)
{
    //アシンプでのモデル読み込み
    m_AiScene = aiImportFile(fileName, aiProcessPreset_TargetRealtime_MaxQuality | aiProcess_ConvertToLeftHanded);
    assert(m_AiScene);

    for (unsigned int i = 0; i < m_AiScene->mNumMaterials; i++) {
        aiString path;
        if (m_AiScene->mMaterials[i]->GetTexture(aiTextureType_DIFFUSE, 0, &path) == AI_SUCCESS) {

            ID3D11ShaderResourceView* srv = nullptr;

            // ★FBX埋め込みテクスチャか確認
            const aiTexture* embedded = m_AiScene->GetEmbeddedTexture(path.C_Str());
            if (embedded) {
                // メモリから読み込む処理
                TexMetadata metadata;
                ScratchImage image;
                // mWidth はバイト数（圧縮形式の場合）
                if (SUCCEEDED(LoadFromWICMemory(embedded->pcData, embedded->mWidth, WIC_FLAGS_NONE, &metadata, image))) {
                    CreateShaderResourceView(Renderer::GetDevice(), image.GetImages(), image.GetImageCount(), metadata, &srv);
                }
            }
            else {
                // 外部ファイル読み込み
                Renderer::CreateTexture(path.C_Str(), &srv);
            }

            m_Textures[std::string(path.C_Str())] = srv;
        }
    }

    //ボーン情報の初期化
    CreateBone(m_AiScene->mRootNode);

    // ボーン名とインデックスの紐付け
    unsigned int globalBoneIndex = 0;
    for (auto& pair : m_Bone) {
        m_BoneNameToIndex[pair.first] = globalBoneIndex++;
    }

    //静的な頂点・インデックスバッファの作成
    CreateBuffers();

    return m_AiScene;

}

//アニメーションの読み込み
void ModelResource::LoadAnimation(const char* fileName, const char* animName) {
    // 既に同じ名前で登録されていたら何もしない（二重ロード防止）
    if (m_Animations.count(animName) > 0) return;

    // アシンプでアニメーションファイルを読み込む
    const aiScene* scene = aiImportFile(fileName, aiProcessPreset_TargetRealtime_MaxQuality | aiProcess_ConvertToLeftHanded);

    if (scene && scene->HasAnimations()) {
        m_Animations[animName] = scene;
    }
    else {
        assert(false && "アニメーションの読み込みに失敗、またはアニメーションが含まれていません");
    }
}

//ボーンの作成
void ModelResource::CreateBone(aiNode* node)
{
    if (m_Bone.count(node->mName.C_Str()) == 0)
    {
        UINT index = (UINT)m_Bone.size();
        m_BoneNameToIndex[node->mName.C_Str()] = index;
        m_Bone[node->mName.C_Str()] = BONE{};
    }

    for (unsigned int i = 0; i < node->mNumChildren; i++)
        CreateBone(node->mChildren[i]);
}

// バッファの作成
void ModelResource::CreateBuffers()
{
    m_VertexBuffer = new ID3D11Buffer * [m_AiScene->mNumMeshes];
    m_IndexBuffer = new ID3D11Buffer * [m_AiScene->mNumMeshes];

    for (unsigned int m = 0; m < m_AiScene->mNumMeshes; m++)
    {
        aiMesh* mesh = m_AiScene->mMeshes[m];
        VERTEX_3D* vertices = new VERTEX_3D[mesh->mNumVertices];

        // 1. 頂点データの初期化
        for (unsigned int v = 0; v < mesh->mNumVertices; v++)
        {
            vertices[v].Position = XMFLOAT3(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);
            vertices[v].Normal = XMFLOAT3(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z);

            if (mesh->mTextureCoords[0]) {
                vertices[v].TexCoord = XMFLOAT2(mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y);
            }
            else {
                vertices[v].TexCoord = XMFLOAT2(0, 0);
            }
            vertices[v].Diffuse = XMFLOAT4(1, 1, 1, 1);

            for (int b = 0; b < 4; b++) { vertices[v].BoneIndex[b] = 0; vertices[v].BoneWeight[b] = 0.0f; }
        }
        ApplyBoneWeights(mesh, vertices);

        // 2. 頂点バッファの作成
        D3D11_BUFFER_DESC vbd{};
        vbd.Usage = D3D11_USAGE_DEFAULT;
        vbd.ByteWidth = sizeof(VERTEX_3D) * mesh->mNumVertices;
        vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vsd{};
        vsd.pSysMem = vertices;
        Renderer::GetDevice()->CreateBuffer(&vbd, &vsd, &m_VertexBuffer[m]);

        // 3. インデックスデータの作成
        unsigned int* indices = new unsigned int[mesh->mNumFaces * 3];
        for (unsigned int f = 0; f < mesh->mNumFaces; f++)
        {
            indices[f * 3 + 0] = mesh->mFaces[f].mIndices[0];
            indices[f * 3 + 1] = mesh->mFaces[f].mIndices[1];
            indices[f * 3 + 2] = mesh->mFaces[f].mIndices[2];
        }

        // 4. インデックスバッファの作成
        D3D11_BUFFER_DESC ibd{};
        ibd.Usage = D3D11_USAGE_DEFAULT;
        ibd.ByteWidth = sizeof(unsigned int) * mesh->mNumFaces * 3;
        ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA isd{};
        isd.pSysMem = indices;
        Renderer::GetDevice()->CreateBuffer(&ibd, &isd, &m_IndexBuffer[m]);

        delete[] vertices;
        delete[] indices;
    }
}
//ボーンウェイト適用
void ModelResource::ApplyBoneWeights(aiMesh* mesh, VERTEX_3D* vertices)
{
    for (unsigned int b = 0; b < mesh->mNumBones; b++)
    {
        aiBone* bone = mesh->mBones[b];

        if (m_BoneNameToIndex.count(bone->mName.C_Str()) == 0)
            continue;

        m_Bone[bone->mName.C_Str()].OffsetMatrix = bone->mOffsetMatrix;
        UINT boneIndex = m_BoneNameToIndex[bone->mName.C_Str()];

        for (unsigned int w = 0; w < bone->mNumWeights; w++)
        {
            aiVertexWeight weight = bone->mWeights[w];
            VERTEX_3D& v = vertices[weight.mVertexId];

            for (int i = 0; i < 4; i++)
            {
                if (v.BoneWeight[i] == 0.0f)
                {
                    v.BoneIndex[i] = boneIndex;
                    v.BoneWeight[i] = weight.mWeight;
                    break;
                }
            }
        }
    }
}

void ModelResource::Uninit()
{
    for (auto& pair : m_Animations) {
        aiReleaseImport(pair.second);
    }
    m_Animations.clear();
}