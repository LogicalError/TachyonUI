#include <gui_core/graphics/d3d12/d3d12_text_atlas.h>

#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/graphics/gui_graphics_context.h>

#include <cassert>

static constexpr tachyon::Diagnostics::LogCategory kLogD3D12 = tachyon::Diagnostics::MakeLogCategory("d3d12");

namespace tachyon::d3d12
{
    D3D12TextAtlasManager::~D3D12TextAtlasManager()
    {
        // The atlas outlives the graphics context's Cleanup, which freed what it handed over.
    }

    bool D3D12TextAtlasManager::Initialize(GraphicsContext* graphicsContext, ImageSet* imageSet)
    {
        m_graphics = graphicsContext;
        m_imageSet = static_cast<D3D12ImageSet*>(imageSet);
        return m_graphics != nullptr && m_imageSet != nullptr;
    }

    D3D12TextAtlasManager::Page* D3D12TextAtlasManager::Find(TextureIndex textureIndex)
    {
        const uint32_t i = uint32_t(textureIndex) - 1;
        if (textureIndex == TextureIndex::Invalid || i >= m_pages.size() || !m_pages[i].used)
        {
            TACHYON_LOG_ERROR(kLogD3D12, "text atlas: no page {}", uint32_t(textureIndex));
            return nullptr;
        }
        return &m_pages[i];
    }

    void D3D12TextAtlasManager::Replace(Page& page, const uint8_t* imageData, uint2 imageSize)
    {
        if (page.image != nullptr)
            m_graphics->DestroyImageAtEnd(page.image, DeviceScope::Frame);
        // Glyphs draw at their rasterized size, so pages need no mip chain.
        page.image = m_graphics->CreateImage(const_cast<uint8_t*>(imageData), imageSize,
                                             page.bpp == 1 ? ImageFormat::R8 : ImageFormat::R8G8B8A8, Sampler::LinearClampToEdge);
    }

    TextureIndex D3D12TextAtlasManager::CreateTexture(const uint8_t* imageData, uint2 imageSize, uint8_t bpp)
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

    void D3D12TextAtlasManager::UpdateTexture(TextureIndex textureIndex, const uint8_t* imageData, uint2 imageSize)
    {
        if (Page* page = Find(textureIndex))
            Replace(*page, imageData, imageSize);
    }

    ImageIndex D3D12TextAtlasManager::GetImageIndex(TextureIndex textureIndex)
    {
        Page* page = Find(textureIndex);
        if (page == nullptr)
            return ImageIndex::Invalid;
        if (page->index == ImageIndex::Invalid)
            page->index = m_imageSet->ReserveIndex();
        return page->index;
    }

    void D3D12TextAtlasManager::SyncTextureIndices()
    {
        for (Page& page : m_pages)
        {
            if (!page.used)
                continue;
            if (page.index == ImageIndex::Invalid)
                page.index = m_imageSet->ReserveIndex();
            m_imageSet->SetImage(page.index, static_cast<D3D12Image*>(page.image));
        }
    }
}
