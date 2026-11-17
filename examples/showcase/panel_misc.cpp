#include "application.h"
#include <gui_controls/gui_file_thumbnails.h>
#include <gui_controls/gui_node_view_helpers.h>
#include <gui_core/diagnostics/gui_logging.h>

//
// The Misc panel: drawing, icon buttons, rich text, image thumbnails and the clipboard's image
// wells, each in a card with a caption; then cards of further text and image demos.
//

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <iterator>
#include <numbers>
#include <span>
#include <vector>

static constexpr tachyon::Diagnostics::LogCategory kLogShowcase = tachyon::Diagnostics::MakeLogCategory("showcase");

namespace tachyon
{
    namespace
    {
        color4b ToBytes(color4f c)
        {
            auto b = [](float v) { return uint8_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
            return color4b{ b(c.r), b(c.g), b(c.b), b(c.a) };
        }
    }

    void Application::CopyDemoImage()
    {
        const ClipboardImage image{ MiscPanel::kPictureWidth, MiscPanel::kPictureHeight, misc.picturePixels };
        if (!clipboard || !clipboard->SetImage(image))
            ShowToast(ToastDesc{ .severity = Severity::Warning, .title = "Image not copied", .body = "This clipboard takes no images." });
    }

    void Application::PasteImage()
    {
        const ClipboardImage image = clipboard ? clipboard->GetImage() : ClipboardImage{};
        if (image.Empty())
        {
            ShowToast(ToastDesc{ .severity = Severity::Info, .title = "No image to paste", .body = "The clipboard holds no image." });
            return;
        }
        ShowPastedImage(image.rgba.data(), uint2{ image.width, image.height });
    }

    void Application::PasteImageFile(const std::string& path)
    {
        // The shell's picture of the file, as large as the well can use.
        static const FileThumbnails::Reader reader = FileThumbnails::ShellReader();
        FileThumbnails::Picture picture;
        if (!reader || !reader(std::filesystem::path(reinterpret_cast<const char8_t*>(path.c_str())), 512, picture) || picture.rgba.empty())
        {
            ShowToast(ToastDesc{ .severity = Severity::Info, .title = "No image to paste",
                                 .body = "That file has no picture to show." });
            return;
        }
        ShowPastedImage(picture.rgba.data(), picture.size);
    }

    void Application::ShowPastedImage(const uint8_t* rgba, uint2 size)
    {
        tachyon::Image* created = graphicsContext->CreateImage(const_cast<uint8_t*>(rgba), size,
                                                               ImageFormat::R8G8B8A8, Sampler::LinearClampToEdge, true);
        if (created == nullptr)
        {
            ShowToast(ToastDesc{ .severity = Severity::Error, .title = "Image not pasted",
                                 .body = std::format("A {} x {} image can't be made here.", size.x, size.y) });
            return;
        }
        // The one pasted before goes once the GPU is done with it.
        if (pastedImage != nullptr)
        {
            graphicsContext->UnregisterImage(pastedImageIndex);
            graphicsContext->DestroyImageAtEnd(pastedImage, DeviceScope::Frame);
        }
        pastedImage      = created;
        pastedImageIndex = graphicsContext->RegisterImage(created);
        pastedImageSize  = size;
    }

    void Application::RenderMiscPanel()
    {
        BeginHorizontalWrap(idManager.AllocateDynamicID("miscPanel"), misc.panel);
        MiscVectorDrawingCard();
        MiscIconButtonsCard();
        MiscRichTextCard();
        MiscThumbnailsCard();
        MiscClipboardWellsCard();
        MiscExtraCards();
        EndHorizontalWrap();
    }

    // Paths, handles, fills, gradients and dashes on a canvas with a dot grid. Laid out on a
    // 420 x 200 board, scaled to fit the canvas and centred in it.
    void Application::MiscVectorDrawingCard()
    {
        BeginVertical(idManager.AllocateDynamicID("drawingCard"), misc.card, LayoutOverride::FixedSize(420, 300));
        Text("Vector drawing · paths, handles, fills, gradients, dashes", misc.caption);

        tachyon::Element canvas{ .id = idManager.AllocateDynamicID("drawingCanvas"), .styleRef = misc.canvas };
        const ElementRef ref = RegisterElement(std::move(canvas), LayoutOverride::Auto().FlexGrow(1));
        if (ref.IsValid())
        {
            VERIFY(elementStorage.AddCustomDraw(ref, [this](tachyon::CustomDrawContext& ctx)
            {
                const StyleManager& sm = styleManager;
                const IntRect inner = ctx.ElementInnerRect();
                RenderCanvasGrid(ctx, sm.Px(16), theme.GetColor("--canvas-dot"), sm.Px(2.0f), int2{ sm.Px(8), sm.Px(8) },
                                 IntRect{ 0, 0, inner.w, inner.h });

                const float s  = std::min(float(inner.w) / 420.0f, float(inner.h) / 200.0f);
                const float ox = (float(inner.w) - 420.0f * s) * 0.5f;
                const float oy = (float(inner.h) - 200.0f * s) * 0.5f;
                auto at  = [&](float x, float y) { return float2{ ox + x * s, oy + y * s }; };
                auto atI = [&](float x, float y) { const float2 p = at(x, y); return int2{ int32_t(std::lround(p.x)), int32_t(std::lround(p.y)) }; };

                // An arc through two anchors, its handles straight up from them.
                const LineStyle path   { .colorStyleIndex = misc.ink,    .lineWidth = sm.Px(3.0f) };
                const LineStyle handle { .colorStyleIndex = misc.accent, .lineWidth = sm.Px(1.0f), .dashSize = sm.Px(3.0f) };
                const LineStyle anchor { .colorStyleIndex = misc.accent, .lineWidth = sm.Px(2.0f) };
                ctx.RenderLine(atI(24, 160), atI(24, 40), atI(140, 40), atI(140, 160), path);
                for (const float x : { 24.0f, 140.0f })
                {
                    ctx.RenderLine(atI(x, 160), atI(x, 60), handle);
                    const int2 knob = atI(x, 60);
                    const int32_t half = sm.Px(4);
                    ctx.RenderQuad(IntRect{ knob.x - half, knob.y - half, half * 2, half * 2 }, misc.accent);
                    ctx.RenderDisc(atI(x, 160), sm.Px(6.0f), misc.canvasFill);
                    ctx.RenderCircle(at(x, 160), sm.Px(6.0f), anchor);
                }

                // A pentagon filled with a gradient, outlined.
                const float2 pentagon[] = { at(215, 40), at(262, 74), at(244, 130), at(186, 130), at(168, 74) };
                int2 outline[std::size(pentagon)];
                std::ranges::transform(pentagon, outline, [](float2 p) { return int2{ int32_t(std::lround(p.x)), int32_t(std::lround(p.y)) }; });
                ctx.RenderFilledPolygon(pentagon, misc.pentagonFill);
                ctx.RenderPolyline(outline, true, LineStyle{ .colorStyleIndex = misc.ink, .lineWidth = sm.Px(2.0f) });

                // A triangle, a dashed ring round it.
                ctx.RenderTriangle(at(310, 160), at(350, 40), at(390, 160), misc.triangleFill);
                const float radius = 40.0f * s;
                const float dash   = 2.0f * std::numbers::pi_v<float> * radius / 10.0f;   // five dashes
                ctx.RenderCircle(at(350, 120), radius, LineStyle{ .colorStyleIndex = misc.ring, .lineWidth = sm.Px(6.0f), .dashSize = dash });
            }));
        }
        EndVertical();
    }

    // Square icon buttons: the states, the four sizes, a group in a well, one with a label,
    // and the primary and destructive kinds.
    void Application::MiscIconButtonsCard()
    {
        BeginVertical(idManager.AllocateDynamicID("iconButtonsCard"), misc.card, LayoutOverride::FixedSize(330, 300));
        Text("Icon buttons · square hit area, state layer, never stretched", misc.caption);
        const StyleRef usual = misc.iconButton[1];

        // Each state shown on its own button, its name under it: hover, pressed and focus held,
        // on and off as set.
        BeginHorizontal(idManager.AllocateDynamicID("statesRow"), misc.row);
        Text("States", misc.rowLabel);
        static constexpr const char* kStates[] = { "rest", "hover", "press", "focus", "on", "off" };
        BeginHorizontal(idManager.AllocateDynamicID("states"), theme.emptyHorzStyleRef, LayoutOverride::Auto().NoGrow());
        for (int i = 0; i < int(std::size(kStates)); ++i)
        {
            BeginVertical(idManager.AllocateDynamicID(std::format("stateColumn{}", i)), misc.state);
            if (i == 1) BeginShownState(ElementState::Hover);
            if (i == 2) BeginShownState(ElementState::Hover | ElementState::Active);
            if (i == 3) BeginShownState(ElementState::Focused);
            if (i == 4) BeginSelected();
            if (i == 5) BeginDisabled();
            Button(idManager.AllocateDynamicID(std::format("state{}", i)), GUIContent{ showcase.heartOutlineIcon, {}, "Favorite" }, [] {}, usual);
            if (i == 5) EndDisabled();
            if (i == 4) EndSelected();
            if (i >= 1 && i <= 3) EndShownState();
            Text(kStates[i], misc.stateLabel);
            EndVertical();
        }
        EndHorizontal();
        EndHorizontal();

        BeginHorizontal(idManager.AllocateDynamicID("sizesRow"), misc.row);
        Text("Sizes", misc.rowLabel);
        static constexpr const char* kSizes[] = { "20px", "24px, the usual", "28px", "32px" };
        for (int i = 0; i < 4; ++i)
            Button(idManager.AllocateDynamicID(std::format("size{}", i)), GUIContent{ showcase.starOutlineIcon, {}, kSizes[i] }, [] {}, misc.iconButton[i]);
        EndHorizontal();

        BeginHorizontal(idManager.AllocateDynamicID("groupRow"), misc.row);
        Text("Group", misc.rowLabel);
        BeginHorizontal(idManager.AllocateDynamicID("group"), misc.iconButtonGroup);
        Toggle(idManager.AllocateDynamicID("bold"),   misc.bold,   GUIContent{ showcase.boldIcon,   {}, "Bold" },   usual);
        Toggle(idManager.AllocateDynamicID("italic"), misc.italic, GUIContent{ showcase.italicIcon, {}, "Italic" }, usual);
        Toggle(idManager.AllocateDynamicID("link"),   misc.link,   GUIContent{ theme.linkIcon,      {}, "Link" },   usual);
        EndHorizontal();
        EndHorizontal();

        BeginHorizontal(idManager.AllocateDynamicID("labelRow"), misc.row);
        Text("With label", misc.rowLabel);
        Toggle(idManager.AllocateDynamicID("like"), misc.liked, GUIContent{ showcase.likeIcon, "Like" }, theme.controls->toggle, LayoutOverride::Auto().NoGrow());
        EndHorizontal();

        BeginHorizontal(idManager.AllocateDynamicID("kindsRow"), misc.row);
        Text("Kinds", misc.rowLabel);
        Button(idManager.AllocateDynamicID("play"), GUIContent{ showcase.playIcon, {}, "Play" }, [] {}, misc.iconButtonPrimary, LayoutOverride::FixedSize(28, 28));
        Button(idManager.AllocateDynamicID("delete"), GUIContent{ theme.deleteIcon, {}, "Delete" }, [] {}, misc.iconButtonDanger, LayoutOverride::FixedSize(28, 28));
        Button(idManager.AllocateDynamicID("more"), GUIContent{ showcase.moreIcon, {}, "More" }, [] {}, misc.iconButton[2]);
        EndHorizontal();
        EndVertical();
    }

    // Links (one inside the app, dotted), bold, italic, inline code and an inline image,
    // icons in the line, other scripts, and a text shadow and a glow.
    void Application::MiscRichTextCard()
    {
        BeginVertical(idManager.AllocateDynamicID("richTextCard"), misc.card, LayoutOverride::FixedSize(360, 300));
        Text("Rich text · links, inline code, inline images, scripts", misc.caption);

        // A line of rich text, `lines` high; its links say which was clicked.
        auto richText = [this](std::string_view key, std::span<ContentRun> runs, int lines)
        {
            const ID id = idManager.AllocateDynamicID(key);
            const ElementState state = guiStateManager.GetCurrentState(id);
            const TextID textID = textSystem->RegisterRichText(id, runs, misc.richText, state);
            tachyon::Element element{ .id = id, .styleRef = misc.richText };
            element.state  = state;
            element.textID = textID;
            element.eventHandler = [this, textID](ElementEvent&& event) -> bool
            {
                if (event.type != EventType::MouseMove && event.type != EventType::MouseDown)
                    return false;
                // The rect is in layout space: LocalCursor keeps the subtraction in that space
                // under any ancestor's contentTransform.
                const int2 pos = event.rect != nullptr ? event.LocalCursor(elementStorage) - event.rect->innerRect.position
                                                       : event.state.screenPos;
                const intptr_t hit = textSystem->HitTestContent(textID, pos.x, pos.y);
                textSystem->SetHoveredContent(textID, hit);
                if (hit != 0 && event.type == EventType::MouseDown && event.state.LeftPressed())
                {
                    TACHYON_LOG_INFO(kLogShowcase, "Link clicked: {}", hit == 1 ? "documentation" : "source");
                    return true;
                }
                return false;
            };
            RegisterElement(std::move(element), LayoutOverride::FixedSize(332, 19 * lines));
        };

        const color4b text1 = ToBytes(theme.GetColor("--text-1"));
        const color4b well  = ToBytes(theme.GetColor("--bg-well"));
        static const TextStyle boldStyle   { .fontSize = 12.0f, .weight = FontWeight::SemiBold };
        static const TextStyle italicStyle { .fontSize = 12.0f, .style = FontStyle::Italic };
        static const TextStyle codeStyle   { .fontSize = 11.0f, .family = FontFamily::Monospace };

        ContentRun links[] =
        {
            { .type = ContentRun::Text, .text = "Learn about " },
            { .type = ContentRun::Text, .text = "our documentation", .contentId = 1, .isLink = true, .linkStyle = theme.linkStyle },
            { .type = ContentRun::Text, .text = " or read " },
            { .type = ContentRun::Text, .text = "the source", .contentId = 2, .isLink = true, .linkStyle = theme.internalLinkStyle },
            { .type = ContentRun::Text, .text = " for more." },
        };
        richText("links", links, 2);

        ContentRun styled[] =
        {
            { .type = ContentRun::Text, .text = "Bold", .textStyleOverride = &boldStyle, .color = text1 },
            { .type = ContentRun::Text, .text = ", " },
            { .type = ContentRun::Text, .text = "italic", .textStyleOverride = &italicStyle },
            { .type = ContentRun::Text, .text = ", " },
            { .type = ContentRun::Text, .text = " inline code ", .textStyleOverride = &codeStyle, .background = well, .color = text1 },
            { .type = ContentRun::Text, .text = ", and an image " },
            { .type = ContentRun::Icon, .iconID = showcase.inlinePictureIcon, .iconWidth = 16.0f, .iconHeight = 16.0f },
            { .type = ContentRun::Text, .text = " in the line." },
        };
        richText("styled", styled, 1);

        ContentRun rating[] =
        {
            { .type = ContentRun::Text, .text = "Rate " },
            { .type = ContentRun::Icon, .iconID = showcase.starIcon,    .iconWidth = 12.0f, .iconHeight = 12.0f },
            { .type = ContentRun::Icon, .iconID = showcase.starIcon,    .iconWidth = 12.0f, .iconHeight = 12.0f },
            { .type = ContentRun::Icon, .iconID = showcase.starIcon,    .iconWidth = 12.0f, .iconHeight = 12.0f },
            { .type = ContentRun::Icon, .iconID = showcase.starIcon,    .iconWidth = 12.0f, .iconHeight = 12.0f },
            { .type = ContentRun::Icon, .iconID = showcase.starDimIcon, .iconWidth = 12.0f, .iconHeight = 12.0f },
            { .type = ContentRun::Text, .text = " · Favorite " },
            { .type = ContentRun::Icon, .iconID = showcase.heartIcon,   .iconWidth = 12.0f, .iconHeight = 12.0f },
        };
        richText("rating", rating, 1);

        Text(reinterpret_cast<const char*>(u8"한국어 · 中文 · 日本語 · العربية · हिन्दी · ไทย"), misc.richText);

        BeginHorizontal(idManager.AllocateDynamicID("shadows"), misc.row, LayoutOverride::Auto().NoGrow());
        Text("Text shadow", textShadowSampleStyleRef);
        Text("Glow", textGlowSampleStyleRef);
        EndHorizontal();
        EndVertical();
    }

    // 76px thumbnails: a picked one, one with alpha over the checkerboard, one still loading,
    // and one whose file has no picture.
    void Application::MiscThumbnailsCard()
    {
        BeginVertical(idManager.AllocateDynamicID("thumbnailsCard"), misc.card, LayoutOverride::FixedSize(470, 220));
        Text("Image thumbnails · selected, alpha, loading, missing", misc.caption);
        BeginHorizontal(idManager.AllocateDynamicID("thumbs"), misc.row, LayoutOverride::Auto().NoGrow());

        struct Thumb { const char* name; ImageIndex image; bool alpha; };
        const Thumb thumbs[] = { { "gradient.png", misc.grey, false }, { "hue.png", misc.hue, false }, { "glow.png", misc.glow, true } };
        for (int i = 0; i < int(std::size(thumbs)); ++i)
        {
            const Thumb thumb = thumbs[i];
            const bool selected = misc.selectedThumb == i;
            BeginVertical(idManager.AllocateDynamicID(std::format("thumbTile{}", i)), misc.thumbTile);
            const ID id = idManager.AllocateDynamicID(std::format("thumb{}", i));
            tachyon::Element tile{ .id = id, .styleRef = selected ? misc.thumbSelected : misc.thumb };
            tile.state = guiStateManager.GetCurrentState(id);
            tile.eventHandler = [this, i](ElementEvent&& event) -> bool
            {
                if (event.type != EventType::MouseDown || !event.state.LeftPressed())
                    return false;
                misc.selectedThumb = i;
                return true;
            };
            const ElementRef ref = RegisterElement(std::move(tile), LayoutOverride{});
            if (ref.IsValid())
            {
                VERIFY(elementStorage.AddCustomDraw(ref, [this, thumb](tachyon::CustomDrawContext& ctx)
                {
                    const IntRect inner = ctx.ElementInnerRect();
                    const IntRect box{ 0, 0, inner.w, inner.h };
                    const float radius = styleManager.Px(5.0f);
                    if (thumb.alpha)
                    {
                        const int32_t cell = std::max(1, styleManager.Px(8));
                        for (int32_t y = 0; y < box.h; y += cell)
                            for (int32_t x = 0; x < box.w; x += cell)
                                ctx.RenderQuad(box, IntRect{ x, y, cell, cell },
                                               ((x / cell + y / cell) % 2 == 0) ? theme.checkerAColor : theme.checkerBColor, radius);
                    }
                    ctx.RenderQuad(box, theme.imageFillColorStyle, radius, thumb.image);
                }));
            }
            Text(thumb.name, selected ? misc.thumbNameSelected : misc.thumbName);
            EndVertical();
        }

        // Still loading: a skeleton for the picture and for the name.
        BeginVertical(idManager.AllocateDynamicID("thumbLoading"), misc.thumbTile);
        Skeleton(idManager.AllocateDynamicID("thumbSkeleton"), misc.thumbLoading);
        Skeleton(idManager.AllocateDynamicID("nameSkeleton"), LayoutOverride::FixedSize(52, 8));
        EndVertical();

        // A file with no picture to show: the image icon on the well.
        BeginVertical(idManager.AllocateDynamicID("thumbMissing"), misc.thumbTile);
        {
            // The icon goes through the text system, which sizes it as the style says.
            const ID id = idManager.AllocateDynamicID("missing");
            tachyon::Element tile{ .id = id, .styleRef = misc.thumbMissing };
            tile.state  = guiStateManager.GetCurrentState(id);
            tile.textID = textSystem->RegisterText(id, showcase.imageIcon, {}, tile.styleRef, tile.state);
            RegisterElement(std::move(tile), LayoutOverride{});
        }
        Text("old.tga", misc.thumbName);
        EndVertical();

        EndHorizontal();
        EndVertical();
    }

    // The clipboard's image: copied from a well that shows it, pasted into one, or an image file
    // dragged in from another application dropped on it (the accent while it's over the well).
    void Application::MiscClipboardWellsCard()
    {
        BeginVertical(idManager.AllocateDynamicID("wellsCard"), misc.card, LayoutOverride::FixedSize(680, 220));
        Text("Clipboard image wells · copy from, paste into", misc.caption);
        BeginHorizontal(idManager.AllocateDynamicID("wells"), misc.row);

        // Draws `image` over the well's inside, keeping its shape and centred.
        auto drawImage = [this](ElementRef ref, ImageIndex image, uint2 size, bool cover)
        {
            VERIFY(elementStorage.AddCustomDraw(ref, [this, image, size, cover](tachyon::CustomDrawContext& ctx)
            {
                const IntRect inner = ctx.ElementInnerRect();
                const float sx = float(inner.w) / float(std::max(1u, size.x));
                const float sy = float(inner.h) / float(std::max(1u, size.y));
                const float scale = cover ? std::max(sx, sy) : std::min(sx, sy);
                const int32_t w = int32_t(std::lround(float(size.x) * scale)), h = int32_t(std::lround(float(size.y) * scale));
                const IntRect box{ (inner.w - w) / 2, (inner.h - h) / 2, w, h };
                ctx.RenderQuad(box, IntRect{ 0, 0, inner.w, inner.h }, theme.imageFillColorStyle, styleManager.Px(5.0f), image);
            }));
        };

        // Copy image: the picture, Copy at its bottom right. The picture drags onto Paste image.
        BeginVertical(idManager.AllocateDynamicID("copyColumn"), misc.wellColumn);
        Text("Copy image", misc.label);
        {
            const ID id = idManager.AllocateDynamicID("copyWell");
            const uint2 size{ MiscPanel::kPictureWidth, MiscPanel::kPictureHeight };
            tachyon::Element well{ .id = id, .styleRef = misc.well };
            well.state = guiStateManager.GetCurrentState(id);
            well.eventHandler = MakeDragSourceHandler<DraggedImage>(guiStateManager, DragPayload::Image,
                [this, size] { return DraggedImage{ misc.picture, size, misc.picturePixels }; },
                ImageDragGhost(misc.picture, size));
            const ElementRef ref = RegisterElement(std::move(well), LayoutOverride{});
            if (ref.IsValid())
                drawImage(ref, misc.picture, size, true);
            VERIFY(elementStorage.BeginChildren());
            const int32_t inset = styleManager.Px(6);
            Button(idManager.AllocateDynamicID("copyImage"), GUIContent{ showcase.copyIcon, "Copy", "Puts the picture on the clipboard" },
                   [this] { CopyDemoImage(); }, theme.controls->button,
                   LayoutOverride{}.Absolute(PositionConstraintAxis::kAuto, inset, inset, PositionConstraintAxis::kAuto));
            EndArea();
            SetTooltip(id, "Drag the picture onto Paste image");
        }
        EndVertical();

        // Paste image: click to paste, or drop the Copy image well's picture or an image file;
        // the accent while one's dragged over.
        BeginVertical(idManager.AllocateDynamicID("pasteColumn"), misc.wellColumn);
        Text("Paste image", misc.label);
        {
            const ID id = idManager.AllocateDynamicID("pasteWell");
            const bool dragOver = DropTargetID() == id;
            const bool filled   = pastedImage != nullptr && !dragOver;
            tachyon::Element well{ .id = id, .styleRef = dragOver ? misc.wellDrop : filled ? misc.well : misc.wellEmpty };
            well.state = guiStateManager.GetCurrentState(id);
            if (!filled)
                well.textID = textSystem->RegisterText(id, showcase.imageIcon, {}, well.styleRef, well.state);
            EventHandler takesPictures = MakeDropTargetHandler<DraggedImage>(DragPayload::Image,
                [this](const DraggedImage& image, ID) { ShowPastedImage(image.rgba.data(), image.size); },
                [](const DraggedImage& image) { return image.rgba.size() == size_t(image.size.x) * image.size.y * 4; });
            well.eventHandler = [this, takesPictures](ElementEvent&& event) -> bool
            {
                if (takesPictures(ElementEvent{ event }))
                    return true;
                if (event.type == EventType::FileDragOver)
                    return true;
                if (event.type == EventType::FileDrop)
                {
                    if (event.state.droppedFiles.empty())
                        return false;   // text isn't an image
                    PasteImageFile(event.state.droppedFiles.front());
                    return true;
                }
                if (event.type == EventType::MouseDown && event.state.LeftPressed())
                {
                    PasteImage();
                    return true;
                }
                return false;
            };
            const ElementRef ref = RegisterElement(std::move(well), LayoutOverride{});
            if (ref.IsValid() && filled)
                drawImage(ref, pastedImageIndex, pastedImageSize, false);
            SetTooltip("Pastes the image on the clipboard. The picture dragged from Copy image, or an image file from another application, shows too");
        }
        EndVertical();

        EndHorizontal();
        EndVertical();
    }

    // Further demos: fonts per run and per field, scripts drawn by fallback fonts, CJK regional
    // forms, ways to show an image, and where a box puts its icon.
    void Application::MiscExtraCards()
    {
        // ---- Fonts: a run in a font registered from code, one in the generic monospace; a
        // field in a font of its own.
        BeginVertical(idManager.AllocateDynamicID("fontsCard"), misc.card, LayoutOverride::Auto().NoGrow());
        Text("Fonts · per run, and a field in its own", misc.caption);
        {
            static const FontID boldFontId = [this]
            {
                const std::string boldFace = ResourceFile("fonts/Inter/Inter-Bold.ttf");
                const std::string_view boldFaces[] = { boldFace };
                return textSystem->RegisterFontFamily("Inter Bold", boldFaces);
            }();
            static TextStyle boldStyle { .fontSize = 16.0f, .fontId = FontID::Invalid };
            static TextStyle monoStyle { .fontSize = 14.0f, .family = FontFamily::Monospace };
            boldStyle.fontId = boldFontId;

            static ContentRun mixedFontRuns[] =
            {
                { .type = ContentRun::Text, .text = "Normal " },
                { .type = ContentRun::Text, .text = "Bold ",     .textStyleOverride = &boldStyle },
                { .type = ContentRun::Text, .text = "Monospace", .textStyleOverride = &monoStyle },
            };
            const ID id = idManager.AllocateDynamicID("mixedFonts");
            const ElementState state = guiStateManager.GetCurrentState(id);
            tachyon::Element mixed{ .id = id, .styleRef = misc.richText };
            mixed.state  = state;
            mixed.textID = textSystem->RegisterRichText(id, mixedFontRuns, misc.richText, state);
            RegisterElement(std::move(mixed), LayoutOverride::FixedSize(320, 30));

            static const FontID serifFont = [this]
            {
                const std::string face = ResourceFile("fonts/Noto-otf/NotoSerif-Regular.otf");
                const std::string_view faces[] = { face };
                return textSystem->RegisterFontFamily("Noto Serif", faces);
            }();
            static std::string serifText = "This text box uses Noto Serif";
            Field(serifText, styleManager.WithTextOverride(theme.controls->textbox, { .font = serifFont }),
                  LayoutOverride::Auto().MinWidth(320).NoGrow());
        }
        EndVertical();

        // ---- Scripts the default font lacks, drawn by fallback fonts.
        BeginVertical(idManager.AllocateDynamicID("scriptsCard"), misc.card, LayoutOverride::Auto().NoGrow());
        Text("Scripts · drawn by fallback fonts", misc.caption);
        {
            static const char8_t* const samples[] =
            {
                u8"Symbols:  ★ ✓ ✗ → ⚠ ♪ ∑ √ ∞",
                u8"Emoji:    😀 🎉 👍 🚀",
                u8"Korean:   안녕하세요, 세계",
                u8"Chinese:  你好，世界",
                u8"Japanese: こんにちは、カタカナ、漢字",
                u8"Arabic:   مرحبا بالعالم",
                u8"Hindi:    नमस्ते दुनिया",
                u8"Thai:     สวัสดีชาวโลก",
                u8"Mixed:    Inter, 中文, 한국어 and العربية on one line",
            };
            BeginVertical(idManager.AllocateDynamicID("scriptLines"), theme.emptyVertStyleRef, LayoutOverride::Auto().NoGrow());
            for (const char8_t* sample : samples)
                Text(reinterpret_cast<const char*>(sample), misc.richText);
            EndVertical();
        }
        EndVertical();

        // ---- Regional forms: each column in its own language, so the characters they share
        // (the first row) take that region's letterforms from one CJK font.
        BeginVertical(idManager.AllocateDynamicID("regionalCard"), misc.card, LayoutOverride::Auto().NoGrow());
        Text("Regional forms · one CJK font, five languages", misc.caption);
        {
            static const char* const languages[] = { "ja", "zh-Hans", "zh-Hant", "zh-HK", "ko" };
            static const char* const headers[]   = { "", "Japanese", "Simplified", "Traditional", "Hong Kong", "Korean" };
            static const char8_t* const rows[][6] =
            {
                { u8"Forms",    u8"次 直 骨 述", u8"次 直 骨 述", u8"次 直 骨 述", u8"次 直 骨 述", u8"次 直 骨 述" },
                { u8"Settings", u8"設定",        u8"设置",        u8"設定",        u8"設定",        u8"설정" },
                { u8"Delete",   u8"削除",        u8"删除",        u8"刪除",        u8"刪除",        u8"삭제" },
                { u8"Edit",     u8"編集",        u8"编辑",        u8"編輯",        u8"編輯",        u8"편집" },
            };
            const int colWidth = styleManager.Px(88);
            BeginHorizontal(LayoutOverride::Auto().NoGrow());
            for (const char* header : headers)
                Text(header, LayoutOverride::Auto().MinWidth(Pixels{ colWidth }));
            EndHorizontal();
            for (const auto& row : rows)
            {
                BeginHorizontal(LayoutOverride::Auto().NoGrow());
                Text(reinterpret_cast<const char*>(row[0]), LayoutOverride::Auto().MinWidth(Pixels{ colWidth }));
                for (int c = 0; c < 5; ++c)
                {
                    const StyleRef style = styleManager.WithTextOverride(theme.staticTextStyleRef, { .language = LanguageTag(languages[c]) });
                    Text(reinterpret_cast<const char*>(row[c + 1]), style, LayoutOverride::Auto().MinWidth(Pixels{ colWidth }));
                }
                EndHorizontal();
            }
        }
        EndVertical();

        // ---- One image shown in different ways (ImageDisplay), and an image field.
        BeginVertical(idManager.AllocateDynamicID("displayCard"), misc.card, LayoutOverride::Auto().NoGrow());
        Text("Image display · channels, swaps, encoding, tint; an image field", misc.caption);
        BeginHorizontal(idManager.AllocateDynamicID("displays"), misc.row, LayoutOverride::Auto().NoGrow());
        {
            Image(demoImageIndex, LayoutOverride::FixedSize(64, 64));
            SetTooltip("As it is");
            Image(demoImageIndex, ImageDisplay::Channel(ImageChannel::Red), LayoutOverride::FixedSize(64, 64));
            SetTooltip("Its red channel, as grey");
            ImageDisplay swapped;
            swapped.red  = ImageChannel::Blue;
            swapped.blue = ImageChannel::Red;
            Image(demoImageIndex, swapped, LayoutOverride::FixedSize(64, 64));
            SetTooltip("Red and blue swapped");
            ImageDisplay encoded;
            encoded.srgbEncode = true;
            Image(demoImageIndex, encoded, LayoutOverride::FixedSize(64, 64));
            SetTooltip("sRGB-encoded, as for an image of linear values");
            ImageDisplay tinted;
            tinted.tint = color4f{ 1.0f, 0.6f, 0.2f, 1.0f };
            Image(demoImageIndex, tinted, LayoutOverride::FixedSize(64, 64));
            SetTooltip("Tinted orange");

            const ID fieldID = idManager.AllocateDynamicID("imageField");
            tachyon::Element field{ .id = fieldID, .styleRef = theme.imageStyleRef };
            field.backgroundImage = demoImageIndex;
            field.state           = guiStateManager.GetCurrentState(fieldID);
            field.eventHandler    = [](ElementEvent&& event) -> bool
            {
                if (event.type == EventType::MouseDown && event.state.LeftPressed())
                {
                    TACHYON_LOG_INFO(kLogShowcase, "Image field: open picker");
                    return true;
                }
                return false;
            };
            RegisterElement(std::move(field), LayoutOverride::FixedSize(64, 64));
            SetTooltip("Click to choose an image");
        }
        EndHorizontal();
        EndVertical();

        // ---- Where a box puts its icon (-tachyon-icon-side), and its text's vertical
        // alignment on each side (-tachyon-vertical-text-align).
        BeginVertical(idManager.AllocateDynamicID("iconSideCard"), misc.card, LayoutOverride::Auto().Width(560).NoGrow());
        Text("Icon placement · side, and vertical alignment", misc.caption);
        BeginHorizontalWrap(idManager.AllocateDynamicID("iconSides"), theme.emptyHorzWrapStyleRef);
        {
            AddBox(idManager.AllocateDynamicID(), theme.iconLeftStyleRef,   showcase.circleIcon, "Left");
            AddBox(idManager.AllocateDynamicID(), theme.iconRightStyleRef,  showcase.starIcon,   "Right");
            AddBox(idManager.AllocateDynamicID(), theme.iconTopStyleRef,    showcase.circleIcon, "Top");
            AddBox(idManager.AllocateDynamicID(), theme.iconBottomStyleRef, showcase.starIcon,   "Bottom");

            static const IconID* const kSideIcon[4] = { &showcase.circleIcon, &showcase.starIcon, &showcase.circleIcon, &showcase.starIcon };
            static const char* const kLabel[4][3] =
            {
                { "L / top", "L / mid", "L / bot" },
                { "R / top", "R / mid", "R / bot" },
                { "T / top", "T / mid", "T / bot" },
                { "B / top", "B / mid", "B / bot" },
            };
            for (int s = 0; s < 4; ++s)
                for (int v = 0; v < 3; ++v)
                    AddBox(idManager.AllocateDynamicID(), iconVAlignDemoStyleRefs[s][v], *kSideIcon[s], kLabel[s][v]);
        }
        EndHorizontalWrap();
        EndVertical();
    }

}
