#include <gui_core/style/gui_style_gradient.h>
#include <gui_core/style/gui_color_conversions.h>

#include <algorithm>
#include <cmath>
#include <array>
#include <vector>

namespace tachyon
{
using namespace ColorConvert;

namespace
{
    // The color `local_t` (0..1) of the way from `af` to `bf`, blended in `colorSpace`. Alpha
    // goes straight; the colors are weighted by it, as CSS blends them premultiplied, so
    // fading to a transparent stop keeps the color instead of darkening it.
    color4f Mix(color4f af, color4f bf, float local_t, ColorSpace colorSpace)
    {
        const float outA = af.a * (1.0f - local_t) + bf.a * local_t;
        {
            const float wa = af.a * (1.0f - local_t);
            const float wb = bf.a * local_t;
            if (wa + wb > 0.0f) local_t = wb / (wa + wb);
        }

        switch (colorSpace)
        {
            case ColorSpace::sRGB:
                return color4f{
                    af.r * (1.0f - local_t) + bf.r * local_t,
                    af.g * (1.0f - local_t) + bf.g * local_t,
                    af.b * (1.0f - local_t) + bf.b * local_t,
                    outA
                };

            case ColorSpace::LinearRGB:
            {
                float lr = SRGBToLinear(af.r) * (1.0f - local_t) + SRGBToLinear(bf.r) * local_t;
                float lg = SRGBToLinear(af.g) * (1.0f - local_t) + SRGBToLinear(bf.g) * local_t;
                float lb = SRGBToLinear(af.b) * (1.0f - local_t) + SRGBToLinear(bf.b) * local_t;
                return color4f{ LinearToSRGB(lr), LinearToSRGB(lg), LinearToSRGB(lb), outA };
            }

            case ColorSpace::HSL:
            {
                float ha, sa, la, hb_, sb, lb_;
                RGBtoHSL(af.r, af.g, af.b, ha, sa, la);
                RGBtoHSL(bf.r, bf.g, bf.b, hb_, sb, lb_);

                float dh = hb_ - ha;
                if (dh > 0.5f) dh -= 1.0f;
                else if (dh < -0.5f) dh += 1.0f;
                float h = ha + dh * local_t;
                if (h < 0.0f) h += 1.0f; else if (h > 1.0f) h -= 1.0f;
                float s = sa * (1.0f - local_t) + sb * local_t;
                float l = la * (1.0f - local_t) + lb_ * local_t;

                auto rgb = HSLtoRGB(h, s, l);
                return color4f{ rgb[0], rgb[1], rgb[2], outA };
            }

            case ColorSpace::HSV:
            {
                float ha, sa, va, hb_, sb, vb;
                RGBtoHSV(af.r, af.g, af.b, ha, sa, va);
                RGBtoHSV(bf.r, bf.g, bf.b, hb_, sb, vb);

                float dh = hb_ - ha;
                if (dh > 0.5f) dh -= 1.0f;
                else if (dh < -0.5f) dh += 1.0f;
                float h = ha + dh * local_t;
                if (h < 0.0f) h += 1.0f; else if (h > 1.0f) h -= 1.0f;
                float s = sa * (1.0f - local_t) + sb * local_t;
                float v = va * (1.0f - local_t) + vb * local_t;

                auto rgb = HSVtoRGB(h, s, v);
                return color4f{ rgb[0], rgb[1], rgb[2], outA };
            }

            case ColorSpace::Lab:
            {
                float La, aa_, ba_, Lb, ab_, bb_;
                RGBtoLab(af.r, af.g, af.b, La, aa_, ba_);
                RGBtoLab(bf.r, bf.g, bf.b, Lb, ab_, bb_);

                float L  = La  * (1.0f - local_t) + Lb  * local_t;
                float aa = aa_ * (1.0f - local_t) + ab_ * local_t;
                float bb = ba_ * (1.0f - local_t) + bb_ * local_t;

                auto rgb = LabToRGB(L, aa, bb);
                return color4f{ rgb[0], rgb[1], rgb[2], outA };
            }

            case ColorSpace::Oklab:
            {
                float La, aa_, ba_, Lb, ab_, bb_;
                RGBtoOklab(af.r, af.g, af.b, La, aa_, ba_);
                RGBtoOklab(bf.r, bf.g, bf.b, Lb, ab_, bb_);

                float L  = La  * (1.0f - local_t) + Lb  * local_t;
                float aa = aa_ * (1.0f - local_t) + ab_ * local_t;
                float bb = ba_ * (1.0f - local_t) + bb_ * local_t;

                auto rgb = OklabToRGB(L, aa, bb);
                return color4f{ rgb[0], rgb[1], rgb[2], outA };
            }

            default:
                return color4f{
                    af.r * (1.0f - local_t) + bf.r * local_t,
                    af.g * (1.0f - local_t) + bf.g * local_t,
                    af.b * (1.0f - local_t) + bf.b * local_t,
                    outA
                };
        }
    }

    // A float from 0..1 as 8 bits, rounded.
    uint8_t Pack(float v)
    {
        v = std::max(0.0f, std::min(1.0f, v));
        int iv = static_cast<int>(std::lround(v * 255.0f));
        if (iv < 0) iv = 0;
        else if (iv > 255) iv = 255;
        return static_cast<uint8_t>(iv);
    }

    // The color at `t` of stops sorted by offset: the two around it blended, `local_t` of the
    // way, eased as the earlier one says.
    color4f SampleSorted(std::span<const GradientStop> sorted, ColorSpace colorSpace, float t)
    {
        const size_t count = sorted.size();
        if (count == 0)
            return color4f{ 0, 0, 0, 0 };
        t = std::max(0.0f, std::min(1.0f, t));
        if (t <= sorted[0].offset)
            return sorted[0].color;
        if (t >= sorted[count - 1].offset)
            return sorted[count - 1].color;

        size_t idx = 0;
        for (size_t i = 0; i + 1 < count; ++i)
            if (t >= sorted[i].offset && t <= sorted[i + 1].offset)
            {
                idx = i;
                break;
            }
        const GradientStop& a = sorted[idx];
        const GradientStop& b = sorted[idx + 1];

        const float range = b.offset - a.offset;
        float local_t = (range <= 0.0f) ? 0.0f : (t - a.offset) / range;
        local_t = std::max(0.0f, std::min(1.0f, local_t));
        switch (a.interpolation)
        {
            case StopInterpolation::Smooth:   local_t = local_t * local_t * (3.0f - 2.0f * local_t); break;
            case StopInterpolation::Constant: local_t = t >= b.offset ? 1.0f : 0.0f;                 break;
            default: break;
        }
        return Mix(a.color, b.color, local_t, colorSpace);
    }

    // The stops in use, sorted by offset (equal ones keep their order), offsets clamped.
    std::vector<GradientStop> Sorted(std::span<const GradientStop> stops)
    {
        std::vector<GradientStop> sorted(stops.begin(), stops.end());
        for (GradientStop& stop : sorted)
            stop.offset = std::max(0.0f, std::min(1.0f, stop.offset));
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const GradientStop& a, const GradientStop& b) { return a.offset < b.offset; });
        return sorted;
    }
}

void Gradient::WriteTo(std::span<color4b> out) const
{
    if (out.empty())
        return;

    const size_t size = out.size();
    const size_t count = StopCount();   // never past the stop array

    if (count == 0)
    {
        // Fill transparent black if no stops
        for (size_t i = 0; i < size; ++i)
            out[i] = color4b{ 0, 0, 0, 0 };
        return;
    }

    // Sorted by offset, as 0..1 floats, blending linearly.
    std::array<ColorStop, kMaxStops> sortedStops = stops;
    std::sort(sortedStops.begin(), sortedStops.begin() + count,
              [](const ColorStop& a, const ColorStop& b) { return a.GetOffset() < b.GetOffset(); });
    std::array<GradientStop, kMaxStops> sorted{};
    for (size_t i = 0; i < count; ++i)
        sorted[i] = GradientStop{ sortedStops[i].GetOffset(), sortedStops[i].GetColor4f(), StopInterpolation::Linear };

    const std::span<const GradientStop> used(sorted.data(), count);
    for (size_t i = 0; i < size; ++i)
    {
        const float t = (size == 1) ? 0.0f : static_cast<float>(i) / static_cast<float>(size - 1);
        const color4f c = SampleSorted(used, colorSpace, t);
        out[i] = color4b{ Pack(c.r), Pack(c.g), Pack(c.b), Pack(c.a) };
    }
}

bool GradientF::AddStop(const color4f& color, float offset, StopInterpolation interpolation)
{
    if (StopCount() >= kMaxStops)
        return false;
    stops.push_back(GradientStop{ std::max(0.0f, std::min(1.0f, offset)), color, interpolation });
    return true;
}

color4f GradientF::Sample(float t) const
{
    const std::vector<GradientStop> sorted = Sorted(std::span<const GradientStop>(stops.data(), size_t(StopCount())));
    return SampleSorted(sorted, colorSpace, t);
}

void GradientF::WriteTo(std::span<color4f> out) const
{
    const std::vector<GradientStop> sorted = Sorted(std::span<const GradientStop>(stops.data(), size_t(StopCount())));
    const size_t size = out.size();
    for (size_t i = 0; i < size; ++i)
        out[i] = SampleSorted(sorted, colorSpace, size == 1 ? 0.0f : float(i) / float(size - 1));
}

void GradientF::WriteTo(std::span<color4b> out) const
{
    const std::vector<GradientStop> sorted = Sorted(std::span<const GradientStop>(stops.data(), size_t(StopCount())));
    const size_t size = out.size();
    for (size_t i = 0; i < size; ++i)
    {
        const color4f c = SampleSorted(sorted, colorSpace, size == 1 ? 0.0f : float(i) / float(size - 1));
        out[i] = color4b{ Pack(c.r), Pack(c.g), Pack(c.b), Pack(c.a) };
    }
}

GradientF GradientF::From(const Gradient& gradient)
{
    GradientF out;
    out.colorSpace = gradient.colorSpace;
    for (uint8_t i = 0; i < gradient.StopCount(); ++i)
        out.stops.push_back(GradientStop{ gradient.stops[i].GetOffset(), gradient.stops[i].GetColor4f(), StopInterpolation::Linear });
    return out;
}

Gradient GradientF::ToGradient() const
{
    Gradient out;
    out.type = GradientType::Linear;
    out.colorSpace = colorSpace;
    for (int i = 0; i < StopCount(); ++i)
        if (!out.AddStop(stops[size_t(i)].color, stops[size_t(i)].offset))   // SetColor clamps to 0..1
            break;   // full: the first Gradient::kMaxStops
    return out;
}
} // namespace tachyon
