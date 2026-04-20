#include "main.h"   
#include "manager.h"
#include "renderer.h"
#include "texture.h"
#include <io.h>

std::unordered_map<std::string, ID3D11ShaderResourceView*> Texture::m_TexturePool;

ID3D11ShaderResourceView* Texture::Load(const char* FileName)
{
    if (m_TexturePool.count(FileName) > 0) {
        return m_TexturePool[FileName]; // 既にロード済みのテクスチャがあればそれを返す

    }

    wchar_t wFileName[512];
    mbstowcs(wFileName, FileName, strlen(FileName) + 1);

    TexMetadata metadata;
    ScratchImage image;
    ID3D11ShaderResourceView* texture = nullptr;

    if (_access(FileName, 0) == 0) {
        LoadFromWICFile(wFileName, WIC_FLAGS_NONE, &metadata, image);
        CreateShaderResourceView(Renderer::GetDevice(), image.GetImages(), image.GetImageCount(), metadata, &texture);
    }

    if (!texture) {
        // デバッグ用ログ
        OutputDebugStringA("Texture not found or embedded: ");
        OutputDebugStringA(FileName);
        OutputDebugStringA("\n");
        return nullptr; 
    }

    return texture;

}
