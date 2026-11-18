#pragma once

// The text system's atlas pages as images in a backend's image set: each page an image made
// with GraphicsContext::CreateImage, at an index reserved for it. A page that grows is a new
// image under the same index, the old one let go once no frame draws with it. For backends
// whose image set reserves an index and points it at an image (ReserveIndex, SetImage).

#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/graphics/gui_graphics_context.h>
#include <gui_core/text/gui_text_atlas_manager.h>

#include <cassert>
#include <vector>

namespace tachyon
{
    /// @addtogroup graphics
    /// @{

    template <class ImageSetT, class ImageT>
    class ImageSetTextAtlasManager final : public TextAtlasManager
    {
    public:
        bool Initialize(GraphicsContext* graphicsContext, ImageSet* imageSet) override
        {
            m_graphics = graphicsContext;
            m_imageSet = static_cast<ImageSetT*>(imageSet);
            return m_graphics != nullptr && m_imageSet != nullptr;
        }

        TextureIndex CreateTexture(const uint8_t* imageData, uint2 imageSize, uint8_t bpp) override
        {
            assert(bpp == 1 || bpp == 4);
            size_t i = 0;
            while (i < m_pages.size() && m_pages[i].used)
                ++i;
            if (i == m_pages.size())
                m_pages.emplace_back();
            Page& page = m_pages[i];
            page.bpp  = bpp;
            page.used = true;
            Replace(page, imageData, imageSize);
            return TextureIndex(uint32_t(i) + 1);
        }

        void UpdateTexture(TextureIndex textureIndex, const uint8_t* imageData, uint2 imageSize) override
        {
            if (Page* page = Find(textureIndex))
                Replace(*page, imageData, imageSize);
        }

        ImageIndex GetImageIndex(TextureIndex textureIndex) override
        {
            Page* page = Find(textureIndex);
            if (page == nullptr)
                return ImageIndex::Invalid;
            if (page->index == ImageIndex::Invalid)
                page->index = m_imageSet->ReserveIndex();
            return page->index;
        }

        void SyncTextureIndices() override
        {
            for (Page& page : m_pages)
            {
                if (!page.used)
                    continue;
                if (page.index == ImageIndex::Invalid)
                    page.index = m_imageSet->ReserveIndex();
                m_imageSet->SetImage(page.index, static_cast<ImageT*>(page.image));
            }
        }

    private:
        struct Page
        {
            Image*     image = nullptr;
            uint8_t    bpp   = 1;
            ImageIndex index = ImageIndex::Invalid;
            bool       used  = false;
        };

        Page* Find(TextureIndex textureIndex)
        {
            const uint32_t i = uint32_t(textureIndex) - 1;
            if (textureIndex == TextureIndex::Invalid || i >= m_pages.size() || !m_pages[i].used)
            {
                TACHYON_LOG_ERROR(Diagnostics::MakeLogCategory("text"), "text atlas: no page {}", uint32_t(textureIndex));
                return nullptr;
            }
            return &m_pages[i];
        }

        // A new image for the page; the old one goes once no frame draws with it. Glyphs draw at
        // their rasterized size, so pages need no mip chain.
        void Replace(Page& page, const uint8_t* imageData, uint2 imageSize)
        {
            if (page.image != nullptr)
                m_graphics->DestroyImageAtEnd(page.image, DeviceScope::Frame);
            page.image = m_graphics->CreateImage(const_cast<uint8_t*>(imageData), imageSize,
                                                 page.bpp == 1 ? ImageFormat::R8 : ImageFormat::R8G8B8A8, Sampler::LinearClampToEdge);
        }

        GraphicsContext*  m_graphics = nullptr;
        ImageSetT*        m_imageSet = nullptr;
        std::vector<Page> m_pages;
    };
    /// @}
}
