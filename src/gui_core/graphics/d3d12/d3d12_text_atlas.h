#pragma once

// Inside the Direct3D 12 backend: the text system's atlas pages, as images in the image set.
// A page that grows is a new image under the same index, as on Vulkan.

#include <gui_core/graphics/d3d12/d3d12_device.h>
#include <gui_core/text/gui_text_atlas_manager.h>

#include <vector>

namespace tachyon::d3d12
{
    class D3D12TextAtlasManager final : public TextAtlasManager
    {
    public:
        ~D3D12TextAtlasManager() override;

        bool         Initialize(GraphicsContext* graphicsContext, ImageSet* imageSet) override;
        TextureIndex CreateTexture(const uint8_t* imageData, uint2 imageSize, uint8_t bpp) override;
        void         UpdateTexture(TextureIndex textureIndex, const uint8_t* imageData, uint2 imageSize) override;
        ImageIndex   GetImageIndex(TextureIndex textureIndex) override;
        void         SyncTextureIndices() override;

    private:
        struct Page
        {
            Image*     image = nullptr;
            uint8_t    bpp   = 1;
            ImageIndex index = ImageIndex::Invalid;
            bool       used  = false;
        };
        Page* Find(TextureIndex textureIndex);
        /// A new image for the page, the old one let go once no frame draws with it.
        void  Replace(Page& page, const uint8_t* imageData, uint2 imageSize);

        GraphicsContext*  m_graphics = nullptr;
        D3D12ImageSet*    m_imageSet = nullptr;
        std::vector<Page> m_pages;
    };
}
