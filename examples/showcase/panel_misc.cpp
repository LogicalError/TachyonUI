#include "application.h"
#include <gui_core/diagnostics/gui_logging.h>

//
// This file is for testing and prototyping various miscellaneous GUI rendering features that don't fit into other categories, such as custom draw, on-demand color styles, polygons, dashed borders, etc.
//

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <iterator>
#include <vector>

static constexpr tachyon::Diagnostics::LogCategory kLogShowcase = tachyon::Diagnostics::MakeLogCategory("showcase");

namespace tachyon
{
    void Application::CopyDemoImage()
    {
        const ClipboardImage image{ 64, 64, demoImagePixels };
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
        tachyon::Image* created = graphicsContext->CreateImage(const_cast<uint8_t*>(image.rgba.data()), uint2{ image.width, image.height },
                                                               ImageFormat::R8G8B8A8, Sampler::LinearClampToEdge, true);
        if (created == nullptr)
        {
            ShowToast(ToastDesc{ .severity = Severity::Error, .title = "Image not pasted",
                                 .body = std::format("A {} x {} image can't be made here.", image.width, image.height) });
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
        pastedImageSize  = uint2{ image.width, image.height };
    }

    void Application::RenderMiscPanel()
    {
        BeginHorizontalWrap();

        // Get current time from the system clock for animating some elements
        static const auto s_appStart = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        float elapsedMilliseconds = std::chrono::duration<float, std::milli>(now - s_appStart).count();

        int2 size = { styleManager.Px(490), styleManager.Px(200) };
        {
            tachyon::ID id = idManager.AllocateDynamicID();
            tachyon::Element shapeBox{};
            shapeBox.id = id;
            shapeBox.state = guiStateManager.GetCurrentState(shapeBox.id);
            shapeBox.styleRef = boxStyleRefs[showcase.testBoxStyleCount];

            auto addRes = elementStorage.Add(std::move(shapeBox), tachyon::LayoutOverride::FixedSize(Pixels{ size.x }, Pixels{ size.y }));
            VERIFY(addRes.status);
            VERIFY(windowRegistry.Add(addRes.ref));

            VERIFY(elementStorage.AddCustomDraw(addRes.ref, [elapsedMilliseconds, this](tachyon::CustomDrawContext& ctx)
            {
                // Coords are inner-rect-local LAYOUT pixels.
                const StyleManager& sm = styleManager;
                const tachyon::LineStyle lineStyle      = { .colorStyleIndex = theme.lineWhiteStyle, .lineWidth = sm.Px(3.0f) };
                const tachyon::LineStyle innerLineStyle = { .colorStyleIndex = triangleFillStyle, .lineWidth = sm.Px(10.0f) };
                const tachyon::LineStyle outerLineStyle = { .colorStyleIndex = theme.lineDarkStyle, .lineWidth = innerLineStyle.lineWidth + sm.Px(3.0f) };

                ctx.RenderLine({ sm.Px(24), sm.Px(120) }, { sm.Px(24), sm.Px(24) }, { sm.Px(180), sm.Px(24) }, { sm.Px(180), sm.Px(120) }, lineStyle);
                ctx.RenderDot({ sm.Px(24.0f), sm.Px(120.0f) }, sm.Px(20.0f), { 1.0f, 0.2f, 0.2f, 1.0f });

                // Rotate triangle over time. elapsedMilliseconds is in ms.
                float t = elapsedMilliseconds * 0.001f; // seconds
                float angle = t; // radians (1 radian per second)
                float s = std::sin(angle);
                float c = std::cos(angle);

                // Element-local triangle points.
                tachyon::float2 pa { sm.Px(260.f), sm.Px( 40.f) };
                tachyon::float2 pb { sm.Px(300.f), sm.Px(130.f) };
                tachyon::float2 pc { sm.Px(220.f), sm.Px(130.f) };

                // Compute centroid and rotate around it
                tachyon::float2 center = (pa + pb + pc) * (1.0f / 3.0f);

                auto rotatePoint = [&](const tachyon::float2& p) -> tachyon::float2
                {
                    tachyon::float2 v{ p.x - center.x, p.y - center.y };
                    tachyon::float2 rp{ v.x * c - v.y * s, v.x * s + v.y * c };
                    tachyon::float2 finalP{ rp.x + center.x, rp.y + center.y };
                    return tachyon::float2{ (finalP.x + 0.5f), (finalP.y + 0.5f) };
                };

                ctx.RenderTriangle(rotatePoint(pa), rotatePoint(pb), rotatePoint(pc), triangleFillStyle);

                // Second triangle + colorwheel, centered at (400, 90) element-local.
                tachyon::int2 c2{ sm.Px(400), sm.Px(90) };
                ctx.RenderTriangle(
                    tachyon::float2{ sm.Px(-40.f), sm.Px( 30.f) } + c2,
                    tachyon::float2{          0.f, sm.Px(-50.f) } + c2,
                    tachyon::float2{ sm.Px( 40.f), sm.Px( 30.f) } + c2,
                    triangleFillStyle2);
                ctx.RenderCircle(c2, sm.Px(60.f), outerLineStyle);
                ctx.RenderCircle(c2, sm.Px(60.f), innerLineStyle);
            }));
        }

        // ---- Demo: on-demand color style, polygon, dashed border, per-run font ----
        {
            // --- Demo 1: on-demand color style (animated hue) ---
            {
                float hue = std::fmod(elapsedMilliseconds * 0.06f, 360.0f); // cycle hue
                float h   = hue / 60.0f;
                float s   = 1.0f, v = 1.0f;
                int   hi  = (int)h % 6;
                float f   = h - (float)(int)h;
                float p   = v * (1 - s), q = v * (1 - s * f), t2 = v * (1 - s * (1 - f));
                float r, g, b;
                if (hi == 0) { r=v; g=t2; b=p; }
                else if (hi == 1) { r=q; g=v; b=p; }
                else if (hi == 2) { r=p; g=v; b=t2; }
                else if (hi == 3) { r=p; g=q; b=v; }
                else if (hi == 4) { r=t2; g=p; b=v; }
                else              { r=v;  g=p; b=q; }

                tachyon::ColorStyle cs;
                cs.type = tachyon::ColorType::SolidColor;
                cs.gradient.stopCount = 1;
                cs.gradient.stops[0].SetColor(tachyon::color4b { (uint8_t)(r*255), (uint8_t)(g*255), (uint8_t)(b*255), 255 });

                if (animatedHueStyle == tachyon::VisualColorStyleIndex::Invalid)
                    animatedHueStyle = styleManager.CreateColorStyle(cs);
                else
                    styleManager.UpdateColorStyle(animatedHueStyle, cs);
            }

            // --- Demo 2: polygon and dashed border ---

            size = { styleManager.Px(490), styleManager.Px(220) };
            tachyon::ID demoId = idManager.AllocateDynamicID();
            tachyon::Element demoBox{};
            demoBox.id = demoId;
            demoBox.state = guiStateManager.GetCurrentState(demoId);
            demoBox.styleRef = boxStyleRefs[showcase.testBoxStyleCount];

            auto demoRes = elementStorage.Add(std::move(demoBox), tachyon::LayoutOverride::FixedSize(Pixels{ size.x }, Pixels{ size.y }));
            VERIFY(demoRes.status);
            VERIFY(windowRegistry.Add(demoRes.ref));

            VERIFY(elementStorage.AddCustomDraw(demoRes.ref, [this](tachyon::CustomDrawContext& ctx)
            {
                // Coords are inner-rect-local LAYOUT pixels.
                const StyleManager& sm = styleManager;

                // Demo 1: line using animated on-demand color style
                tachyon::LineStyle animStyle { .colorStyleIndex = animatedHueStyle, .lineWidth = sm.Px(4.0f) };
                ctx.RenderLine({ sm.Px(10), sm.Px(20) }, { sm.Px(150), sm.Px(20) }, animStyle);

                // Demo 2a: filled hexagon
                const float hexRadius = sm.Px(55.0f);
                tachyon::float2 hexCenter = { sm.Px(230.f), sm.Px(110.f) };
                tachyon::float2 hexPts[6];
                for (int i = 0; i < 6; i++) {
                    float a = i * (3.14159f / 3.0f);
                    hexPts[i] = hexCenter + tachyon::float2{ std::cos(a) * hexRadius, std::sin(a) * hexRadius };
                }
                ctx.RenderFilledPolygon(hexPts, triangleFillStyle);

                // Demo 2b: outlined pentagon
                const float pentRadius = sm.Px(50.0f);
                tachyon::float2 pentCenterF = { sm.Px(80.f), sm.Px(120.f) };
                tachyon::int2 pentPts[5];
                for (int i = 0; i < 5; i++) {
                    float a = i * (2.0f * 3.14159f / 5.0f) - 3.14159f / 2.0f;
                    pentPts[i] = tachyon::int2 { (int)(pentCenterF.x + std::cos(a) * pentRadius), (int)(pentCenterF.y + std::sin(a) * pentRadius) };
                }
                tachyon::LineStyle pentStyle { .colorStyleIndex = theme.lineWhiteStyle, .lineWidth = sm.Px(2.5f) };
                ctx.RenderPolyline(pentPts, /*closed=*/true, pentStyle);

                // Demo 3: dashed box
                ctx.RenderBox(
                    tachyon::IntRect { .position = { sm.Px(340), sm.Px(60) }, .size = { sm.Px(130), sm.Px(80) } },
                    theme.dashedStyleRef.visualBoxStyleIndex, tachyon::ElementState::None);
            }));
        }

        // Standalone icon examples
        {
            tachyon::int2 iconSize = { styleManager.Px(48), styleManager.Px(48) };

            // Circle icon
            tachyon::Element circleIconElem {};
            circleIconElem.id = idManager.AllocateDynamicID();
            circleIconElem.iconID = showcase.circleIcon;
            circleIconElem.state = guiStateManager.GetCurrentState(circleIconElem.id);
            circleIconElem.styleRef = theme.firstIconStyleRef;

            auto circleRes = elementStorage.Add(std::move(circleIconElem), tachyon::LayoutOverride::FixedSize(Pixels{ iconSize.x }, Pixels{ iconSize.y }));
            VERIFY(circleRes.status);
            VERIFY(windowRegistry.Add(circleRes.ref));

            // Heart icon
            tachyon::Element heartIconElem {};
            heartIconElem.id = idManager.AllocateDynamicID();
            heartIconElem.iconID = showcase.heartIcon;
            heartIconElem.state = guiStateManager.GetCurrentState(heartIconElem.id);
            heartIconElem.styleRef = theme.regularControls.button;

            auto heartRes = elementStorage.Add(std::move(heartIconElem), tachyon::LayoutOverride::FixedSize(Pixels{ iconSize.x }, Pixels{ iconSize.y }));
            VERIFY(heartRes.status);
            VERIFY(windowRegistry.Add(heartRes.ref));

            // Star icon
            tachyon::Element starIconElem {};
            starIconElem.id = idManager.AllocateDynamicID();
            starIconElem.iconID = showcase.starIcon;
            starIconElem.state = guiStateManager.GetCurrentState(starIconElem.id);
            starIconElem.styleRef = theme.regularControls.button;

            auto starRes = elementStorage.Add(std::move(starIconElem), tachyon::LayoutOverride::FixedSize(Pixels{ iconSize.x }, Pixels{ iconSize.y }));
            VERIFY(starRes.status);
            VERIFY(windowRegistry.Add(starRes.ref));
        }

        // Icon + Text button example
        {
            tachyon::int2 buttonSize = { styleManager.Px(200), styleManager.Px(48) };
            tachyon::ID iconButtonId = idManager.AllocateDynamicID();
            tachyon::ElementState state = guiStateManager.GetCurrentState(iconButtonId);

            // Create button with icon and text using rich text
            static tachyon::ContentRun buttonContent[] =
            {
                { .type = tachyon::ContentRun::Icon, .iconID = showcase.heartIcon, .iconWidth = 24.0f, .iconHeight = 24.0f },
                { .type = tachyon::ContentRun::Text, .text = " Like This!" }
            };

            tachyon::Element iconButtonElem{};
            iconButtonElem.id = iconButtonId;
            iconButtonElem.state = guiStateManager.GetCurrentState(iconButtonElem.id);
            iconButtonElem.styleRef = theme.regularControls.button;
            iconButtonElem.textID = textSystem->RegisterRichText(iconButtonId, buttonContent, theme.regularControls.button, state);
            iconButtonElem.eventHandler = MakeButtonHandler([]()
            {
                TACHYON_LOG_INFO(kLogShowcase, "Icon button clicked!");
            });

            auto addRes = elementStorage.Add(std::move(iconButtonElem), tachyon::LayoutOverride::FixedSize(Pixels{ buttonSize.x }, Pixels{ buttonSize.y }));
            VERIFY(addRes.status);
            VERIFY(windowRegistry.Add(addRes.ref));
            tabChainManager.RegisterTabTarget(iconButtonId);
        }

        // Rich text with multiple icons example
        {
            tachyon::int2 richTextSize = { styleManager.Px(300), styleManager.Px(60) };
            tachyon::ID richTextId = idManager.AllocateDynamicID();
            tachyon::ElementState state = guiStateManager.GetCurrentState(richTextId);

            static tachyon::ContentRun richContent[] =
            {
                {.type = tachyon::ContentRun::Text, .text = "Rate: " },
                {.type = tachyon::ContentRun::Icon, .iconID = showcase.starIcon, .iconWidth = 16.0f, .iconHeight = 16.0f },
                {.type = tachyon::ContentRun::Icon, .iconID = showcase.starIcon, .iconWidth = 16.0f, .iconHeight = 16.0f },
                {.type = tachyon::ContentRun::Icon, .iconID = showcase.starIcon, .iconWidth = 16.0f, .iconHeight = 16.0f },
                {.type = tachyon::ContentRun::Icon, .iconID = showcase.starIcon, .iconWidth = 16.0f, .iconHeight = 16.0f },
                {.type = tachyon::ContentRun::Icon, .iconID = showcase.starIcon, .iconWidth = 16.0f, .iconHeight = 16.0f },
                {.type = tachyon::ContentRun::Text, .text = " Favorite: " },
                {.type = tachyon::ContentRun::Icon, .iconID = showcase.heartIcon, .iconWidth = 16.0f, .iconHeight = 16.0f }
            };

            tachyon::Element richTextElem{};
            richTextElem.id = richTextId;
            richTextElem.state = guiStateManager.GetCurrentState(richTextElem.id);
            richTextElem.styleRef = theme.textStyleRef;
            richTextElem.textID = textSystem->RegisterRichText(richTextId, richContent, theme.textStyleRef, state);

            auto addRes = elementStorage.Add(std::move(richTextElem), tachyon::LayoutOverride::FixedSize(Pixels{ richTextSize.x }, Pixels{ richTextSize.y }));
            VERIFY(addRes.status);
            VERIFY(windowRegistry.Add(addRes.ref));
        }

        // Rich text with clickable links
        {
            tachyon::int2 linkTextSize = { styleManager.Px(400), styleManager.Px(60) };
            tachyon::ID linkTextId = idManager.AllocateDynamicID();
            tachyon::ElementState state = guiStateManager.GetCurrentState(linkTextId);

            // The links take the theme's .link style; a theme reload changes it.
            tachyon::ContentRun linkContent[] =
            {
                { .type = tachyon::ContentRun::Text, .text = "Learn about " },
                { .type = tachyon::ContentRun::Text, .text = "our documentation",
                  .contentId = 1, .isLink = true, .linkStyle = theme.linkStyle },
                { .type = tachyon::ContentRun::Text, .text = " or check " },
                { .type = tachyon::ContentRun::Text, .text = "the source",
                  .contentId = 2, .isLink = true, .linkStyle = theme.linkStyle },
                { .type = tachyon::ContentRun::Text, .text = " for more info." },
            };

            tachyon::TextID linkTextID = textSystem->RegisterRichText(linkTextId, linkContent, theme.textStyleRef, state);

            tachyon::Element linkElem{};
            linkElem.id = linkTextId;
            linkElem.state = guiStateManager.GetCurrentState(linkElem.id);
            linkElem.styleRef = theme.textStyleRef;
            linkElem.textID = linkTextID;
            linkElem.eventHandler = [this, linkTextID](ElementEvent&& event)->bool
            {
                if (event.type != tachyon::EventType::MouseMove && event.type != tachyon::EventType::MouseDown)
                    return false;

                // event.rect->innerRect is in layout space - use LocalCursor so the
                // subtract stays in the same space under any ancestor contentTransform.
                int2 pos = (event.rect != nullptr)
                    ? event.LocalCursor(elementStorage) - event.rect->innerRect.position
                    : event.state.screenPos;
                intptr_t hitId =
                    textSystem->HitTestContent(linkTextID, pos.x, pos.y);
                textSystem->SetHoveredContent(linkTextID, hitId);
                if (hitId != 0 && event.type == tachyon::EventType::MouseDown && event.state.LeftPressed())
                {
                    TACHYON_LOG_INFO(kLogShowcase, "Link clicked: {}", hitId == 1 ? "documentation" : "source");
                    return true;
                }
                return false;
            };

            auto addRes = elementStorage.Add(std::move(linkElem), tachyon::LayoutOverride::FixedSize(Pixels{ linkTextSize.x }, Pixels{ linkTextSize.y }));
            VERIFY(addRes.status);
            VERIFY(windowRegistry.Add(addRes.ref));
        }

        // Image element - drop an image straight into the flow. Two display
        // sizes, then a clickable one standing in for an image-selection field.
        {
            Image(demoImageIndex, tachyon::LayoutOverride::FixedSize(96, 96));
            Image(demoImageIndex, tachyon::LayoutOverride::FixedSize(48, 48));

            // The same image shown differently (ImageDisplay): its red as grey, red and blue
            // swapped, as if it held linear values, and tinted.
            Image(demoImageIndex, ImageDisplay::Channel(ImageChannel::Red), tachyon::LayoutOverride::FixedSize(48, 48));
            SetTooltip("Its red channel, as grey");
            ImageDisplay swapped;
            swapped.red  = ImageChannel::Blue;
            swapped.blue = ImageChannel::Red;
            Image(demoImageIndex, swapped, tachyon::LayoutOverride::FixedSize(48, 48));
            SetTooltip("Red and blue swapped");
            ImageDisplay encoded;
            encoded.srgbEncode = true;
            Image(demoImageIndex, encoded, tachyon::LayoutOverride::FixedSize(48, 48));
            SetTooltip("sRGB-encoded, as for an image of linear values");
            ImageDisplay tinted;
            tinted.tint = color4f{ 1.0f, 0.6f, 0.2f, 1.0f };
            Image(demoImageIndex, tinted, tachyon::LayoutOverride::FixedSize(48, 48));
            SetTooltip("Tinted orange");

            const tachyon::ID fieldID = idManager.AllocateDynamicID("imageField");
            tachyon::Element field{};
            field.id              = fieldID;
            field.styleRef        = theme.imageStyleRef;
            field.backgroundImage = demoImageIndex;
            field.state           = guiStateManager.GetCurrentState(fieldID);
            field.eventHandler    = [](tachyon::ElementEvent&& ev) -> bool
            {
                if (ev.type == tachyon::EventType::MouseDown && ev.state.LeftPressed())
                {
                    TACHYON_LOG_INFO(kLogShowcase, "Image field: open picker");
                    return true;
                }
                return false;
            };
            auto fRes = elementStorage.Add(std::move(field), tachyon::LayoutOverride::FixedSize(64, 64));
            VERIFY(fRes.status);
            VERIFY(windowRegistry.Add(fRes.ref));
            SetTooltip("Click to choose an image");

            // The clipboard: the sample image copied to it, and the image on it (a screenshot,
            // say) pasted here.
            Button(idManager.AllocateDynamicID("copyImage"), "Copy image", [this] { CopyDemoImage(); });
            SetTooltip("Puts the sample image on the clipboard");
            Button(idManager.AllocateDynamicID("pasteImage"), "Paste image", [this] { PasteImage(); });
            SetTooltip("Shows the image on the clipboard, such as a screenshot");
            if (pastedImage != nullptr)
                Image(pastedImageIndex, pastedImageSize, tachyon::LayoutOverride::FixedSize(160, 96));
        }

        // --- Progress and loading -----------------------------------------------------
        {
            const float clock = guiStateManager.Interaction().GetFrameTime();
            BeginVertical(tachyon::LayoutOverride::Auto().NoGrow().Width(360));
            Text("Progress and loading", theme.labelStyleRef);

            // An import: 6 s running, then done for 3 s, then again. Cancel stops it, and Retry
            // starts it over.
            float elapsed = clock - importStart;
            if (elapsed >= 9.0f && !importCancelled)
            {
                importStart = clock;
                elapsed     = 0.0f;
            }
            const float done = std::min(elapsed / 6.0f, 1.0f);
            const std::string importDetail = std::format("{:.1f} MB of 19.3 MB, about {} s left", 19.3f * done,
                                                         int(std::ceil(6.0f - elapsed)));
            if (importCancelled)
                TaskProgress(ProgressTask{ .name = "Import cancelled", .value = done, .state = TaskState::Failed,
                                           .detail = "character.fbx was not imported",
                                           .onRetry = [this, clock] { importStart = clock; importCancelled = false; } });
            else if (elapsed < 6.0f)
                TaskProgress(ProgressTask{ .name = "Importing character.fbx", .value = done, .detail = importDetail,
                                           .onCancel = [this] { importCancelled = true; } });
            else
                TaskProgress(ProgressTask{ .name = "Import complete", .state = TaskState::Done, .note = "6.0 s" });

            TaskProgress(ProgressTask{ .name = "Compiling shaders..." });

            // A bake that failed; Retry runs it for 3 s, and it fails again.
            if (clock < bakeRetryUntil)
                TaskProgress(ProgressTask{ .name = "Baking lightmaps", .note = "retrying" });
            else
                TaskProgress(ProgressTask{ .name = "Bake failed at 73%", .value = 0.73f, .state = TaskState::Failed,
                                           .detail = "Out of GPU memory while baking lightmap 4 of 6",
                                           .onRetry = [this, clock] { bakeRetryUntil = clock + 3.0f; } });

            // The steps of a build, one every 3 s.
            Text("Build player", theme.labelStyleRef);
            const float build = std::fmod(clock, 15.0f) / 3.0f;
            auto stepState = [build](int step) {
                return build >= float(step + 1) ? TaskState::Done : build >= float(step) ? TaskState::Running : TaskState::Pending;
            };
            const ProgressTask steps[] = {
                { .name = "Compile scripts", .value = build,         .state = stepState(0), .note = stepState(0) == TaskState::Done ? "3.0 s" : "" },
                { .name = "Bake lighting",   .value = build - 1.0f,  .state = stepState(1), .note = stepState(1) == TaskState::Done ? "3.0 s" : "" },
                { .name = "Package assets",  .value = build - 2.0f,  .state = stepState(2), .note = stepState(2) == TaskState::Done ? "3.0 s" : "" },
                { .name = "Upload",          .value = build - 3.0f,  .state = stepState(3), .note = stepState(3) == TaskState::Done ? "3.0 s" : "" },
            };
            ProgressSteps(steps);

            // A status bar: the indexing under way, and the tasks in the background behind
            // the pill.
            const float indexed = std::fmod(clock, 20.0f) / 20.0f;
            const std::string count = std::format("{} / 3,800", int(indexed * 3800.0f));
            BeginStatusBar(tachyon::LayoutOverride::Auto().NoGrow());
            StatusProgress(ProgressTask{ .name = "Indexing assets", .value = indexed, .note = count });
            StatusText("", tachyon::LayoutOverride::Auto().FlexGrow(1));
            std::vector<ProgressTask> background;
            const ProgressTask all[] = {
                { .name = "Bake lighting",       .value = std::fmod(clock, 30.0f) / 30.0f },
                { .name = "Generate thumbnails", .value = std::fmod(clock + 10.0f, 30.0f) / 30.0f },
                { .name = "Upload build",        .value = 0.15f, .state = TaskState::Paused },
            };
            for (int i = 0; i < 3; ++i)
                if (!backgroundCancelled[i])
                {
                    background.push_back(all[i]);
                    background.back().onCancel = [this, i] { backgroundCancelled[i] = true; };
                }
            BackgroundTasks(idManager.AllocateDynamicID("background"), background);
            EndStatusBar();
            if (background.empty())
                Button(idManager.AllocateDynamicID("restartTasks"), "Restart background tasks",
                       [this] { std::fill(std::begin(backgroundCancelled), std::end(backgroundCancelled), false); },
                       tachyon::LayoutOverride::Auto().NoGrow());

            // Spinners in their three sizes, a ring filling as the import does, and a button
            // that's busy for a while once clicked.
            BeginHorizontal(tachyon::LayoutOverride::Auto().NoGrow());
            Spinner(SpinnerSize::Small);
            Spinner(SpinnerSize::Medium);
            Spinner(SpinnerSize::Large);
            CircularProgress(done);
            BusyButton(idManager.AllocateDynamicID("save"), { theme.menuCheckIcon, "Save" }, clock < saveBusyUntil, "Saving...",
                       [this, clock] { saveBusyUntil = clock + 2.5f; });
            SetTooltip("Busy for 2.5 s once clicked");
            EndHorizontal();

            // A list still loading: placeholder rows, indented as the tree they stand for.
            static const int rows[][2] = { { 0, 140 }, { 16, 110 }, { 16, 160 }, { 32, 90 }, { 16, 130 } };
            for (const auto& row : rows)
            {
                BeginHorizontal(tachyon::LayoutOverride::Auto().NoGrow().Height(26));
                if (row[0] > 0)   // the indent, empty
                    RegisterElement(tachyon::Element{ .id = idManager.AllocateDynamicID("indent"), .styleRef = theme.emptyHorzStyleRef },
                                    tachyon::LayoutOverride::Auto().Width(row[0]));
                Skeleton(tachyon::LayoutOverride::Auto().Size(14, 14));
                Skeleton(tachyon::LayoutOverride::Auto().Width(row[1]));
                EndHorizontal();
            }
            EndVertical();
        }

        // --- Demo: per-run font override via textStyleOverride ---
        {
            // A family registered from code (one face) and a generic family.
            static const tachyon::FontID boldFontId = [this]
            {
                const std::string boldFace = ResourceFile("fonts/Inter/Inter-Bold.ttf");
                const std::string_view boldFaces[] = { boldFace };
                return textSystem->RegisterFontFamily("Inter Bold", boldFaces);
            }();

            static tachyon::TextStyle boldStyle  { .fontSize = 16.0f, .fontId = tachyon::FontID::Invalid };
            static tachyon::TextStyle monoStyle  { .fontSize = 14.0f, .family = tachyon::FontFamily::Monospace };
            boldStyle.fontId   = boldFontId;

            tachyon::int2 mixedFontSize = { styleManager.Px(420), styleManager.Px(30) };
            tachyon::ID mixedFontId = idManager.AllocateDynamicID();
            tachyon::ElementState mixedState = guiStateManager.GetCurrentState(mixedFontId);

            static tachyon::ContentRun mixedFontRuns[] =
            {
                { .type = tachyon::ContentRun::Text, .text = "Normal " },
                { .type = tachyon::ContentRun::Text, .text = "Bold ",     .textStyleOverride = &boldStyle },
                { .type = tachyon::ContentRun::Text, .text = "Monospace", .textStyleOverride = &monoStyle },
            };

            tachyon::Element mixedElem{};
            mixedElem.id       = mixedFontId;
            mixedElem.state    = mixedState;
            mixedElem.styleRef = theme.textStyleRef;
            mixedElem.textID   = textSystem->RegisterRichText(mixedFontId, mixedFontRuns, theme.textStyleRef, mixedState);

            auto addRes = elementStorage.Add(std::move(mixedElem), tachyon::LayoutOverride::FixedSize(Pixels{ mixedFontSize.x }, Pixels{ mixedFontSize.y }));
            VERIFY(addRes.status);
            VERIFY(windowRegistry.Add(addRes.ref));
        }

        // --- Script coverage: text the default font lacks, drawn by fallback fonts ---
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
            BeginVertical(tachyon::LayoutOverride::Auto().NoGrow());
            for (const char8_t* sample : samples)
                Text(reinterpret_cast<const char*>(sample));
            EndVertical();
        }

        // --- Text shadow and glow (CSS text-shadow) --------------------------------
        BeginHorizontal(tachyon::LayoutOverride::Auto().NoGrow());
        Text("Text shadow", textShadowSampleStyleRef);
        Text("Glow", textGlowSampleStyleRef);
        EndHorizontal();

        // --- Per-element text overrides (StyleManager::WithTextOverride) -----------
        {
            // Translation table: each column's text is in its own language, so shared
            // characters (first row) take that region's letterforms from one CJK font.
            static const char* const languages[] = { "ja", "zh-Hans", "zh-Hant", "zh-HK", "ko" };
            static const char* const headers[]   = { "", "Japanese", "Simplified", "Traditional", "Hong Kong", "Korean" };
            static const char8_t* const rows[][6] =
            {
                { u8"Forms",    u8"次 直 骨 述", u8"次 直 骨 述", u8"次 直 骨 述", u8"次 直 骨 述", u8"次 直 骨 述" },
                { u8"Settings", u8"設定",        u8"设置",        u8"設定",        u8"設定",        u8"설정" },
                { u8"Delete",   u8"削除",        u8"删除",        u8"刪除",        u8"刪除",        u8"삭제" },
                { u8"Edit",     u8"編集",        u8"编辑",        u8"編輯",        u8"編輯",        u8"편집" },
            };
            const int colWidth = styleManager.Px(96);
            BeginVertical(tachyon::LayoutOverride::Auto().NoGrow());
            {
                BeginHorizontal(tachyon::LayoutOverride::Auto().NoGrow());
                for (const char* header : headers)
                    Text(header, tachyon::LayoutOverride::Auto().MinWidth(Pixels{ colWidth }));
                EndHorizontal();
                for (const auto& row : rows)
                {
                    BeginHorizontal(tachyon::LayoutOverride::Auto().NoGrow());
                    Text(reinterpret_cast<const char*>(row[0]), tachyon::LayoutOverride::Auto().MinWidth(Pixels{ colWidth }));
                    for (int c = 0; c < 5; ++c)
                    {
                        const tachyon::StyleRef style = styleManager.WithTextOverride(theme.staticTextStyleRef, { .language = tachyon::LanguageTag(languages[c]) });
                        Text(reinterpret_cast<const char*>(row[c + 1]), style, tachyon::LayoutOverride::Auto().MinWidth(Pixels{ colWidth }));
                    }
                    EndHorizontal();
                }
            }
            EndVertical();

            // A text box with its own font; all other text keeps the CSS font.
            static const tachyon::FontID serifFont = [this]
            {
                const std::string face = ResourceFile("fonts/Noto-otf/NotoSerif-Regular.otf");
                const std::string_view faces[] = { face };
                return textSystem->RegisterFontFamily("Noto Serif", faces);
            }();
            static std::string serifText = "This text box uses Noto Serif";
            Field(serifText, styleManager.WithTextOverride(theme.controls->textbox, { .font = serifFont }),
                  tachyon::LayoutOverride::Auto().MinWidth(320).NoGrow());
        }

        // --- Icon + Text Examples with iconTextGap and iconSide ---

        {
            AddBox(idManager.AllocateDynamicID(), theme.iconLeftStyleRef, showcase.circleIcon, "Left");

            AddBox(idManager.AllocateDynamicID(), theme.iconRightStyleRef, showcase.starIcon, "Right");

            AddBox(idManager.AllocateDynamicID(), theme.iconTopStyleRef, showcase.circleIcon, "Top");

            AddBox(idManager.AllocateDynamicID(), theme.iconBottomStyleRef, showcase.starIcon, "Bottom");
        }

        // --- Vertical-align demo matrix -----------------------------------------
        // 4 icon sides x 3 vertical alignments (-tachyon-vertical-text-align). The
        // boxes share an 80px min-height so the alignment differences show.
        {
            static const tachyon::IconID kSideIcon[4] = {
                showcase.circleIcon, showcase.starIcon, showcase.circleIcon, showcase.starIcon
            };
            static const char* kLabel[4][3] = {
                { "L / top",    "L / mid",    "L / bot"    },
                { "R / top",    "R / mid",    "R / bot"    },
                { "T / top",    "T / mid",    "T / bot"    },
                { "B / top",    "B / mid",    "B / bot"    },
            };
            for (int s = 0; s < 4; ++s)
            {
                for (int v = 0; v < 3; ++v)
                {
                    AddBox(idManager.AllocateDynamicID(),
                           iconVAlignDemoStyleRefs[s][v],
                           kSideIcon[s],
                           kLabel[s][v]);
                }
            }
        }

        EndHorizontalWrap();
    }

}