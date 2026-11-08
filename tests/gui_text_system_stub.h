#pragma once

#include <gui_core/text/gui_utf8.h>
#include <gui_core/text/gui_text_system.h>
#include <gui_core/style/gui_style_manager.h>
#include <gui_core/gui_id.h>
#include <gui_core/gui_hash.h>

#include <algorithm>
#include <climits>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <cstdio>
#include <cstdlib>

namespace tachyon
{
    /// Stub implementation of ITextSystem for testing without platform dependencies
    class StubTextSystem : public ITextSystem
    {
    public:
        StubTextSystem(
            int32_t defaultCharWidth = 8,
            int32_t defaultLineHeight = 16,
            int32_t defaultIconSize = 16)
            : m_defaultCharWidth(defaultCharWidth)
            , m_defaultLineHeight(defaultLineHeight)
            , m_defaultIconSize(defaultIconSize)
        {
        }

        // -------------------- Initialization --------------------

        bool Initialize(GraphicsContext* /*graphicsContext*/, StyleManager* /*styleManager*/,
                        TextAtlasManager* /*textAtlasManager*/,
                        GUIStateManager* /*guiStateManager*/, IClipboard* /*clipboard*/) override
        {
            return true;
        }

        void Shutdown() override
        {
            m_registry.clear();
            m_elementIdToTextIdMap.clear();
            m_pendingEdits.clear();
            m_editableElementIds.clear();
            m_activeEditor = ID::Invalid();
        }

        void Update() override
        {
            // No-op for stub
        }
        void BeginFrame(int2 /*screenMousePos*/, MouseButtons /*mouseButtons*/, uint32_t /*timeMs*/) override
        {
            // No-op for stub
        }

        // -------------------- Font Registration --------------------

        /// The styles a text's icon is measured by (its side, size and gap); without them an icon
        /// isn't measured.
        void SetStyleManager(const StyleManager* styleManager) { m_styleManager = styleManager; }

        void SetFontDirectory(std::string_view /*directory*/) override {}
        void SetFallbackFontDirectory(std::string_view /*directory*/) override {}
        void SetLanguage(std::string_view /*languageTag*/) override {}

        using ITextSystem::RegisterFontFamily;
        FontID RegisterFontFamily(std::string_view familyName, std::span<const FontFaceSource> /*faces*/) override
        {
            // Stub: no actual font loading.
            return FontIDFromName(familyName);
        }

        // -------------------- Text Registration --------------------

        TextID RegisterText(ID elementId, IconID iconId, std::string_view text, tachyon::StyleRef styleIndex, tachyon::ElementState state) override
        {
            uint64_t key = Hash::Combine(Hash::Combine(Hash::Calculate(elementId), Hash::Calculate(text), Hash::Calculate(styleIndex)),
                                         Hash::Calculate(iconId));
            
            if (auto it = m_elementIdToTextIdMap.find(key); it != m_elementIdToTextIdMap.end())
                return it->second;

            RegisteredText entry;
            entry.elementId = elementId;
            entry.text = text;
            entry.styleIndex = styleIndex;
            entry.state = state;
            entry.iconId = iconId;
            entry.baseMeasurement = WithIcon(iconId, styleIndex, state, MeasureTextInternal(text, styleIndex));

            uint32_t newIndex = static_cast<uint32_t>(m_registry.size() + 1);
            m_registry.push_back(entry);
            
            TextID textId{newIndex};
            m_elementIdToTextIdMap[key] = textId;
            m_elementTextIdMap[Hash::Calculate(elementId)] = textId;
            return textId;
        }

        TextID RegisterEditableText(ID elementId, std::string& text, TextEditBehavior behavior, tachyon::StyleRef styleIndex, tachyon::ElementState state, bool& wasModified,
                                    const ITextValidator* /*validator*/ = nullptr) override
        {
            m_editableElementIds.insert(elementId);
            ApplyTextEditUpdate(elementId, text, behavior, wasModified);
            return RegisterText(elementId, IconID::Invalid, text, styleIndex, state);
        }

        // No live editor: a value field shows its formatted value each frame, and the
        // frame's injected edits apply to that.
        std::string* ValueEditText(ID /*elementId*/, bool& started) override { started = false; return nullptr; }

        // Remembers the color, so tests can see which texts were recolored.
        void SetTextColor(TextID textId, VisualColorStyleIndex color) override { textColors[textId] = color; }
        void SetCaretColor(VisualColorStyleIndex color) override { caretColor = color; }
        void SetUndoHistory(UndoHistory* /*history*/) override {}
        VisualColorStyleIndex caretColor = VisualColorStyleIndex::Invalid;
        std::unordered_map<TextID, VisualColorStyleIndex> textColors;
        // The style a text was registered with.
        StyleRef StyleOf(TextID textId) const
        {
            if (textId == TextID::Invalid || ((uint32_t)textId) > m_registry.size())
                return StyleRef{};
            return m_registry[((uint32_t)textId) - 1].styleIndex;
        }
        // The icon a text was registered with.
        IconID IconOf(TextID textId) const
        {
            if (textId == TextID::Invalid || ((uint32_t)textId) > m_registry.size())
                return IconID::Invalid;
            return m_registry[((uint32_t)textId) - 1].iconId;
        }


        std::string_view GetText(TextID textId) const override
        {
            if (textId == TextID::Invalid || ((uint32_t)textId) > m_registry.size())
                return "";
            return m_registry[((uint32_t)textId) - 1].text;
        }

        TextID GetTextID(ID elementId) const override
        {
            auto it = m_elementTextIdMap.find(Hash::Calculate(elementId));
            return it != m_elementTextIdMap.end() ? it->second : TextID::Invalid;
        }

        // -------------------- Text Measurement --------------------

        TextMeasurement GetTextMeasurement(TextID textId) const override
        {
            if (textId == TextID::Invalid || ((uint32_t)textId) > m_registry.size())
                return {};
            return m_registry[((uint32_t)textId) - 1].baseMeasurement;
        }

        TextMeasurement MeasureText(TextID textId, const TextWrapContext& wrapContext) const override
        {
            if (textId == TextID::Invalid || ((uint32_t)textId) > m_registry.size())
                return {};

            const auto& entry = m_registry[((uint32_t)textId) - 1];

            if (wrapContext.maxWidth == INT32_MAX)
                return entry.baseMeasurement;

            return WithIcon(entry.iconId, entry.styleIndex, entry.state, MeasureTextInternal(entry.text, entry.styleIndex, wrapContext.maxWidth));
        }

        int32_t GetTextHeightWithoutIcon(TextID textId) const override
        {
            if (textId == TextID::Invalid || ((uint32_t)textId) > m_registry.size())
                return 0;
            const auto& entry = m_registry[((uint32_t)textId) - 1];
            return MeasureTextInternal(entry.text, entry.styleIndex).minHeight;
        }

        IconMeasurement GetIconMeasurement(IconID iconId) const override
        {
            return MeasureIcon(iconId);
        }

        IconMeasurement MeasureIcon(IconID iconId) const override
        {
            if (const auto it = m_pixelIconSizes.find(uint32_t(iconId)); it != m_pixelIconSizes.end())
                return IconMeasurement{ .width = int32_t(it->second.x), .height = int32_t(it->second.y) };
            return IconMeasurement{ .width = m_defaultIconSize, .height = m_defaultIconSize };
        }

        bool IsValidIcon(IconID iconId) const override
        {
            return iconId != IconID::Invalid;
        }

        // -------------------- Text Rendering --------------------

        void RenderText(TextID /*textId*/, IntRect /*rect*/, IntRect /*innerRect*/, IntRect /*clipRect*/, float /*pixelScale*/ = 1.f,
                        const float3x2& /*screenToLayout*/ = float3x2::Identity()) override
        {
            // No-op for stub
        }

        // Any non-negative local coordinate inside a known element is on a link with this cursor.
        std::optional<MouseCursor> linkCursor = MouseCursor::Pointer;
        std::optional<MouseCursor> LinkCursorAt(ID elementId, int32_t localX, int32_t localY) const override
        {
            if (localX < 0 || localY < 0) return std::nullopt;
            if (m_elementTextIdMap.find(Hash::Calculate(elementId)) == m_elementTextIdMap.end()) return std::nullopt;
            return linkCursor;
        }
        // Character index for a local x, at m_defaultCharWidth per character.
        intptr_t HitTestContent(TextID textId, int32_t localX, int32_t /*localY*/) const override
        {
            if (textId == TextID::Invalid || ((uint32_t)textId) > m_registry.size())
                return 0;
            if (localX <= 0) return 0;
            const auto& entry = m_registry[((uint32_t)textId) - 1];
            int32_t idx = localX / m_defaultCharWidth;
            int32_t maxIdx = static_cast<int32_t>(entry.text.length());
            if (idx > maxIdx) idx = maxIdx;
            return static_cast<intptr_t>(idx);
        }
        void SetHoveredContent(TextID /*textId*/, intptr_t /*contentId*/) override {}

        // -------------------- Icon Registration --------------------

        IconID RegisterIconFromSVG(const char* /*name*/, const char* svg_data, size_t svg_size) override
        {
            // Unique ID per call.
            static uint32_t nextIconId = 1;
            const IconID id{ nextIconId++ };
            svgOfIcon[static_cast<uint32_t>(id)] = std::string(svg_data, svg_size);
            return id;
        }

        /// The SVG each icon was registered from.
        std::unordered_map<uint32_t, std::string> svgOfIcon;

        IconID RegisterIconFromSVGFile(const char* /*path*/) override
        {
            static uint32_t nextIconId = 0x10000000;
            return IconID{ nextIconId++ };
        }

        int pixelIconCalls = 0;   ///< RegisterIconFromPixels calls, repeats too

        // An ID per name, measured at its pixel size.
        IconID RegisterIconFromPixels(const char* name, uint2 size, std::span<const uint8_t> rgba) override
        {
            ++pixelIconCalls;
            if (!name || size.x == 0 || size.y == 0 || rgba.size() < size_t(size.x) * size.y * 4)
                return IconID::Invalid;
            if (const auto it = m_pixelIconNames.find(name); it != m_pixelIconNames.end())
                return IconID{ it->second };
            const uint32_t id = 0x20000000 + m_pixelIconCount++;
            m_pixelIconNames[name] = id;
            m_pixelIconSizes[id]   = size;
            return IconID{ id };
        }

        std::vector<IconID> unregisteredIcons;   ///< UnregisterIcon calls, in order

        void UnregisterIcon(IconID iconId) override
        {
            unregisteredIcons.push_back(iconId);
            std::erase_if(m_pixelIconNames, [iconId](const auto& named) { return named.second == uint32_t(iconId); });
            m_pixelIconSizes.erase(uint32_t(iconId));
        }

        // -------------------- Icon Rendering --------------------

        void RenderIcon(IconID /*iconId*/, IntRect /*rect*/, IntRect /*clipRect*/, VisualColorStyleIndex /*tint*/,
                        std::span<const VisualShadow> /*shadows*/ = {}) override
        {
            // No-op for stub
        }

        // -------------------- Rich Text Registration --------------------

        TextID RegisterRichText(ID elementId, const std::span<ContentRun>& contentRuns,
                               StyleRef styleIndex, ElementState state) override
        {
            // Build concatenated text for stub
            std::string combinedText;
            for (const auto& run : contentRuns)
            {
                if (run.type == ContentRun::Text)
                    combinedText += run.text;
                else
                    combinedText += "I"; // Placeholder for icon
            }
            
            return RegisterText(elementId, IconID::Invalid, combinedText, styleIndex, state);
        }

        TextID RegisterEditableRichText(ID elementId, const std::span<ContentRun>& contentRuns,
                                        TextEditBehavior /*behavior*/, StyleRef styleIndex,
                                        ElementState state, bool& wasModified) override
        {
            m_editableElementIds.insert(elementId);
            wasModified = false;
            return RegisterRichText(elementId, contentRuns, styleIndex, state);
        }

        // -------------------- Cursor Visibility --------------------

        void EnsureCursorVisible(ID /*textEditorId*/) override {}
        // The focused editor's cursor (editor-local), as a test sets it; none by default.
        std::optional<IntRect> cursorRect;
        std::optional<IntRect> GetCursorRect(ID /*textEditorId*/) override { return cursorRect; }
        std::optional<IntRect> GetCaretScreenRect() const override { return std::nullopt; }

        // -------------------- Editor Input Handling --------------------

        // Routes typed characters (KeyAction::KeyChar, as InjectText sends them) and
        // editing keys into the active editor's pending buffer.
        void OnKeyboard(ID /*windowId*/, KeyCode key, int /*repeat*/, KeyModifier /*mods*/, KeyAction action) override
        {
            if (m_activeEditor == ID::Invalid()) return;
            auto& pending = m_pendingEdits[Hash::Calculate(m_activeEditor)];
            if (action == KeyAction::KeyChar)
            {
                unsigned int codepoint = static_cast<unsigned int>(key);
                if (codepoint < 32 && codepoint != '\t') return;
                AppendUtf8(pending.inserts, codepoint);
            }
            else if (action == KeyAction::KeyDown)
            {
                if (key == KeyCode::Backspace) pending.backspaces++;
                else if (key == KeyCode::Delete) pending.deletes++;
                else if (key == KeyCode::Return || key == KeyCode::KeyPadEnter) pending.commit = true;
            }
        }
        void OnMouseMove(ID /*windowId*/, int2 /*screenPos*/, MouseButtons /*button*/, KeyModifier /*mods*/) override {}
        void OnMouseButton(ID /*windowId*/, int2 /*screenPos*/, MouseButtons /*button*/, KeyModifier /*mods*/, MouseAction /*action*/) override {}
        void OnMouseScroll(ID /*windowId*/, int2 /*screenPos*/, float2 /*scrollOffset*/, KeyModifier /*mods*/) override {}
        void OnTextComposition(ID /*windowId*/, std::string_view /*text*/, int32_t /*cursor*/) override {}

        void SwitchTextEditor(ID /*from*/, ID to) override
        {
            m_activeEditor = to;
        }

        bool DoesTextEditorAcceptTabs(ID /*elementId*/) const override
        {
            return false; // Stub doesn't support tab-as-text
        }

        bool IsTextEditor(ID elementId) const override
        {
            // Any element that called RegisterEditableText is an editor.
            return m_editableElementIds.count(elementId) > 0;
        }

        void RequestSelectAll(ID /*elementId*/) override {}

        TextEditUpdate ConsumeTextEditUpdate(TextID /*textId*/, std::string& /*outText*/) override
        {
            return {};
        }

        bool UpdateText(TextID textId, std::string_view text) override
        {
            if (textId != TextID::Invalid || ((uint32_t)textId) > m_registry.size())
                return false;

            auto& entry = m_registry[((uint32_t)textId) - 1];
            if (entry.text == text)
                return false;

            uint64_t oldKey = entry.GetHashCode();
            auto it = m_elementIdToTextIdMap.find(oldKey);
            if (it != m_elementIdToTextIdMap.end() && it->second == textId)
                m_elementIdToTextIdMap.erase(it);

            entry.text = std::string(text);
            entry.baseMeasurement = WithIcon(entry.iconId, entry.styleIndex, entry.state, MeasureTextInternal(entry.text, entry.styleIndex));

            uint64_t newKey = entry.GetHashCode();
            m_elementIdToTextIdMap[newKey] = textId;
            return true;
        }

        std::string Copy() override
        {
            return {};
        }

        std::string Cut() override
        {
            return {};
        }

        void Paste(std::string_view /*text*/) override
        {
        }

        bool IsHoveringSelectedText(ID /*textEditorId*/, ID /*windowId*/, tachyon::int2 /*screenSpace*/) const override
        {
            return false;
        }

        bool TryGetTextDragCursor(ID /*windowId*/, tachyon::int2 /*screenSpace*/, MouseCursor& /*outCursor*/) const override
        {
            return false;
        }

        bool TryGetTextDragHoverTarget(ID /*windowId*/, tachyon::int2 /*screenSpace*/, ID& /*outTargetId*/) const override
        {
            return false;
        }

        // -------------------- Cache Management --------------------

        void ClearCache() override
        {
            m_registry.clear();
            m_elementIdToTextIdMap.clear();
            m_elementTextIdMap.clear();
        }

        // -------------------- Configuration --------------------

        void SetDefaultCharWidth(int32_t width) { m_defaultCharWidth = width; }
        void SetDefaultLineHeight(int32_t height) { m_defaultLineHeight = height; }
        void SetDefaultIconSize(int32_t size) { m_defaultIconSize = size; }

    private:
        // Drain queued character / backspace / commit events into `currentText`.
        bool ApplyTextEditUpdate(ID elementId, std::string& currentText, TextEditBehavior behavior, bool& wasModified)
        {
            auto it = m_pendingEdits.find(Hash::Calculate(elementId));
            if (it == m_pendingEdits.end()) return false;
            PendingEdit& pe = it->second;

            // Mirror the real text system's contract: with ContinuousUpdate
            // the bound string tracks every change; with None the edits stay
            // buffered in the editor until a commit (Enter) flushes them.
            // wasModified only fires when the bound string is written.
            const bool continuousUpdate = (behavior & TextEditBehavior::ContinuousUpdate) != TextEditBehavior::None;
            if (!pe.commit && !continuousUpdate)
                return false;

            bool textChanged = false;
            // A whole character at a time, not a byte.
            for (uint32_t i = 0; i < pe.backspaces + pe.deletes && !currentText.empty(); ++i)
            {
                currentText.resize(currentText.size() - LastUtf8CodepointSize(currentText));
                textChanged = true;
            }
            if (!pe.inserts.empty())
            {
                currentText += pe.inserts;
                textChanged = true;
            }
            const bool committed = pe.commit;

            // Reset the pending buffer; the next frame may receive new keys.
            pe = PendingEdit{};

            const bool shouldNotify = committed || (continuousUpdate && textChanged);
            if (shouldNotify) wasModified = true;
            return shouldNotify;
        }


        // The text's icon counted in, beside or above or below it, as the real text system does:
        // at the style's icon size (the icon's own without one), the style's gap from the text.
        TextMeasurement WithIcon(IconID iconId, StyleRef styleIndex, ElementState state, TextMeasurement measurement) const
        {
            if (iconId == IconID::Invalid || !m_styleManager)
                return measurement;
            const VisualTextStyle* style = m_styleManager->GetVisualTextStyle(styleIndex.visualTextStyleIndex,
                                                                              state & ~ElementState::Selected);
            if (!style)
                return measurement;
            IconMeasurement icon = MeasureIcon(iconId);
            if (style->iconSize > 0)
                icon.width = icon.height = style->iconSize;
            switch (style->iconSide)
            {
                case IconSide::Left: case IconSide::Right:
                    measurement.minWidth += style->iconTextGap + icon.width;
                    measurement.maxWidth += style->iconTextGap + icon.width;
                    measurement.minHeight = std::max(measurement.minHeight, icon.height);
                    measurement.maxHeight = std::max(measurement.maxHeight, icon.height);
                    break;
                case IconSide::Top: case IconSide::Bottom:
                    measurement.minHeight += style->iconTextGap + icon.height;
                    measurement.maxHeight += style->iconTextGap + icon.height;
                    measurement.minWidth = std::max(measurement.minWidth, icon.width);
                    measurement.maxWidth = std::max(measurement.maxWidth, icon.width);
                    break;
                default: break;
            }
            return measurement;
        }

        TextMeasurement MeasureTextInternal(std::string_view text, const tachyon::StyleRef /*styleIndex*/,
                                            int32_t maxWidth = INT32_MAX) const
        {
            int32_t charWidth = m_defaultCharWidth;
            int32_t charsPerLine = std::max(1, maxWidth / charWidth);

            uint32_t lines = static_cast<uint32_t>((text.length() + charsPerLine - 1) / charsPerLine);

            int32_t widestLine = std::min<int32_t>(static_cast<int32_t>(text.length()), charsPerLine) * charWidth;
            int32_t height = static_cast<int32_t>(lines) * m_defaultLineHeight;

            // Min-content: the longest word, as wrapping text measures.
            size_t longestWord = 0, word = 0;
            for (char c : text)
            {
                word = (c == ' ') ? 0 : word + 1;
                longestWord = std::max(longestWord, word);
            }

            return TextMeasurement{
                .minWidth = std::min<int32_t>(widestLine, static_cast<int32_t>(longestWord) * charWidth),
                .maxWidth = widestLine,
                .minHeight = height,   // laid out at maxWidth, as TextMeasurement says
                .maxHeight = height
            };
        }

        struct RegisteredText
        {
            ID elementId;
            std::string text;
            tachyon::StyleRef styleIndex;
            tachyon::ElementState state;
            IconID iconId = IconID::Invalid;
            TextMeasurement baseMeasurement;

            uint64_t GetHashCode() const
            {
                return GetHashCode(elementId, text, styleIndex, state);
            }

            static uint64_t GetHashCode(ID elementId, std::string_view text, StyleRef styleIndex, tachyon::ElementState state)
            {
                return Hash::Combine(Hash::Calculate(elementId), Hash::Calculate(text),
                                     Hash::Calculate(styleIndex), Hash::Calculate(state));
            }
        };

        std::vector<RegisteredText> m_registry;
        std::unordered_map<uint64_t, TextID> m_elementIdToTextIdMap;
        std::unordered_map<uint64_t, TextID> m_elementTextIdMap;
        std::unordered_map<uint64_t, std::string> m_floatTextMap;
        std::unordered_map<uint64_t, std::string> m_intTextMap;
        std::unordered_map<std::string, uint32_t> m_pixelIconNames;
        std::unordered_map<uint32_t, uint2>       m_pixelIconSizes;
        uint32_t                                  m_pixelIconCount = 0;

        struct PendingEdit
        {
            std::string inserts;
            uint32_t backspaces = 0;
            uint32_t deletes    = 0;
            bool     commit     = false;
        };
        std::unordered_map<uint64_t, PendingEdit> m_pendingEdits;

        struct IDHash { size_t operator()(ID id) const noexcept { return Hash::Calculate(id); } };
        std::unordered_set<ID, IDHash> m_editableElementIds;
        ID m_activeEditor = ID::Invalid();

        int32_t  m_defaultCharWidth;
        int32_t  m_defaultLineHeight;
        int32_t  m_defaultIconSize;
        const StyleManager* m_styleManager = nullptr;
    };

} // namespace tachyon
