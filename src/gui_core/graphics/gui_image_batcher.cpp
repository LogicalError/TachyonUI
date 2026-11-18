#include <gui_core/graphics/gui_image_batcher.h>

#include <algorithm>
#include <cstring>

namespace tachyon
{
    namespace
    {
        template <class T>
        T Load(const std::byte* bytes)
        {
            T value;
            std::memcpy(&value, bytes, sizeof(T));
            return value;
        }

        void Add(uint32_t image, std::span<uint32_t> images, uint32_t& count)
        {
            if (std::find(images.begin(), images.begin() + count, image) == images.begin() + count && count < images.size())
                images[count++] = image;
        }
    }

    void ImageBatcher::SetStyles(std::span<const VisualColorStyle> colorStyles, std::span<const VisualBoxStyle> boxStyles)
    {
        m_colorImages.resize(colorStyles.size());
        for (size_t i = 0; i < colorStyles.size(); ++i)
        {
            const VisualColorStyle& style = colorStyles[i];
            // Every color style reads its gradient page (its size, at least); texture types their
            // texture too.
            m_colorImages[i] = { style.gradientPage, style.textureIndex != ImageIndex::Invalid ? uint32_t(style.textureIndex) - 1 : kNone };
        }
        m_boxColors.resize(boxStyles.size());
        for (size_t i = 0; i < boxStyles.size(); ++i)
        {
            const VisualBoxStyle& style = boxStyles[i];
            m_boxColors[i] = { uint16_t(style.borderColorIndex[0]), uint16_t(style.borderColorIndex[1]), uint16_t(style.borderColorIndex[2]),
                               uint16_t(style.borderColorIndex[3]), uint16_t(style.backgroundColorIndex) };
        }
    }

    void ImageBatcher::AddColorStyle(int colorStyle, std::array<uint32_t, kMostPerPrimitive>& images, uint32_t& count) const
    {
        // 1-based; 0 is none.
        if (colorStyle < 1 || size_t(colorStyle) > m_colorImages.size())
            return;
        for (const uint32_t image : m_colorImages[size_t(colorStyle) - 1])
            if (image != kNone)
                Add(image, images, count);
    }

    uint32_t ImageBatcher::Read(PrimitiveKind kind, const std::byte* primitive, std::array<uint32_t, kMostPerPrimitive>& images) const
    {
        uint32_t count = 0;
        const auto texture = [&](ImageIndex index) {
            if (index != ImageIndex::Invalid)
                Add(uint32_t(index) - 1, images, count);
        };
        switch (kind)
        {
        case PrimitiveKind::Box:
        {
            const auto box = Load<BoxPrimitive>(primitive);
            texture(box.textureIndex);
            AddColorStyle(box.colorIndex, images, count);
            for (const uint32_t blended : box.styles)
            {
                // The GPU style index in the low 24 bits, its share in the top 8.
                const uint32_t style = blended & 0xFFFFFFu;
                if ((blended >> 24) == 0 || style >= m_boxColors.size())
                    continue;
                for (const uint16_t color : m_boxColors[style])
                    AddColorStyle(color, images, count);
            }
            break;
        }
        case PrimitiveKind::Glyph:
        {
            // Its atlas page, read whatever the index (an invalid one reads slot 0).
            const auto glyph = Load<GlyphPrimitive>(primitive);
            Add(glyph.textureIndex != ImageIndex::Invalid ? uint32_t(glyph.textureIndex) - 1 : 0, images, count);
            AddColorStyle(glyph.textColorIndex, images, count);
            break;
        }
        case PrimitiveKind::Line:
            AddColorStyle(Load<LinePrimitive>(primitive).colorIndex, images, count);
            break;
        case PrimitiveKind::Triangle:
        {
            const auto triangle = Load<TrianglePrimitive>(primitive);
            texture(triangle.textureIndex);
            AddColorStyle(triangle.colorIndex, images, count);
            break;
        }
        case PrimitiveKind::Quad:
        {
            const auto quad = Load<QuadPrimitive>(primitive);
            texture(quad.textureIndex);
            AddColorStyle(quad.colorIndex, images, count);
            break;
        }
        case PrimitiveKind::Circle:
            AddColorStyle(Load<CirclePrimitive>(primitive).colorIndex, images, count);
            break;
        default:   // Dot, Occluder: no image
            break;
        }
        return count;
    }

    void ImageBatcher::Split(PrimitiveKind kind, std::span<const std::byte> primitives, uint32_t firstPrimitive, uint32_t maxImages,
                             std::vector<Batch>& out) const
    {
        const uint32_t stride = kPrimitiveSpecs[size_t(kind)].stride;
        const uint32_t count  = uint32_t(primitives.size() / stride);
        if (count == 0)
            return;
        maxImages = std::clamp(maxImages, 1u, kMaxImages);

        Batch batch;
        batch.first = firstPrimitive;
        std::array<uint32_t, kMostPerPrimitive> read{};
        for (uint32_t p = 0; p < count; ++p)
        {
            const uint32_t reads = Read(kind, primitives.data() + size_t(p) * stride, read);
            // The images it adds to the batch.
            uint32_t added = 0;
            for (uint32_t i = 0; i < reads; ++i)
                if (std::find(batch.images.begin(), batch.images.begin() + batch.imageCount, read[i]) == batch.images.begin() + batch.imageCount)
                    read[added++] = read[i];
            if (batch.imageCount + added > maxImages && batch.count > 0)
            {
                // The batch is full: this primitive starts the next, with all it reads.
                out.push_back(batch);
                batch            = Batch{};
                batch.first      = firstPrimitive + p;
                added            = Read(kind, primitives.data() + size_t(p) * stride, read);
            }
            for (uint32_t i = 0; i < added && batch.imageCount < maxImages; ++i)
                batch.images[batch.imageCount++] = read[i];
            ++batch.count;
        }
        out.push_back(batch);
    }
}
