#pragma once

#include <gui_core/style/gui_style.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tachyon 
{
    /// @addtogroup styling
    /// @{

    // ============================================================================
    // StyleSheet - result of parsing a CSS-inspired style file
    // ============================================================================

    /// A problem found while parsing a style sheet; the parser skips the bad part and goes on.
    struct ParseError
    {
        std::string message;    ///< what is wrong
        int         line   = 0; ///< 1-based line of the problem in the parsed source
        int         column = 0; ///< column of the problem on that line
    };

    /// A resolved CSS custom property (`:root { --name: value; }`): a color, 1..4 numbers
    /// or a box-shadow.
    struct CustomProperty
    {
        color4b    color {};         ///< the value as a solid color
        ColorStyle colorStyle {};    ///< the full value, which may be a gradient
        bool       hasColor = false; ///< color / colorStyle are set

        /// 1..4 numeric values (`--border-first: 2px 2px 2px 2px;`), for shorthands such
        /// as border-width and border-radius.
        uint16_t   dims[4] {};
        uint8_t    dimCount = 0; ///< number of values in dims, 0 = not numeric
        float      number    = 0.0f; ///< == dims[0] when dimCount >= 1
        bool       hasNumber = false; ///< dimCount >= 1

        /// A box-shadow value (`--shadow: 0 8px 20px rgba(0,0,0,0.35);`, or none).
        BoxShadow  shadows[kMaxBoxShadows] {}; ///< see hasShadow
        bool       hasShadow = false; ///< shadows is set (all-empty for `none`)

        std::string text;              ///< the value as written, such as "#121418" or "4px 0px 0px 4px"
        int         line = 0;          ///< the line that set it
        bool        important = false; ///< set with `!important`: a later plain declaration leaves it
    };

    /// The kind of value an `@property` rule's `syntax` allows.
    enum class CustomPropertyType : uint8_t
    {
        Any,       ///< "*": anything, not checked
        Color,     ///< "<color>": a color or a gradient
        Number,    ///< "<number>": one number
        Length,    ///< "<length>": one length, in pixels
        Lengths,   ///< "<length>+": one to four lengths
        Edges,     ///< "<edges>": one to four lengths, a side each, as border-width takes them
        Corners,   ///< "<corners>": one to four lengths, a corner each, as border-radius takes them
        Shadow,    ///< "<shadow>": a box-shadow, or none
    };

    /// What an `@property` rule says about a custom property: enough for a theme editor to
    /// show it and pick the right field for it.
    ///     @property --radius-md {
    ///         syntax: "<length>";
    ///         category: "Sizes";
    ///         label: "Control corners";
    ///         description: "Corner radius of controls";
    ///         range: 0 16;
    ///     }
    struct CustomPropertyInfo
    {
        std::string        name;          ///< with the leading "--"
        CustomPropertyType type = CustomPropertyType::Any;
        std::string        category;      ///< the group it's shown in, such as "Surfaces"
        std::string        label;         ///< the name it's shown with, such as "Panel"
        std::string        description;   ///< what it's used for, a short sentence
        bool               hasRange = false;   ///< min and max are set
        float              min = 0.0f;    ///< the smallest value a number or length may have
        float              max = 0.0f;    ///< the largest
        int                line = 0;      ///< the line of the rule
    };

    /// One `@font-face` rule. Faces sharing a family name form one family.
    struct FontFace
    {
        std::string family; ///< font-family name
        std::string src; ///< as written; relative paths are relative to the stylesheet
    };

    /// A class's styles for where an element is among its parent's children, from its
    /// `:first-child`, `:last-child` and `:only-child` rules (box properties only).
    struct ChildStyles
    {
        Style first; ///< the first of several children
        Style last;  ///< the last of several children
        Style only;  ///< the only child: the first-, last- and only-child rules apply
    };

    /// A parsed style sheet: named styles, custom properties and font faces.
    /// Register a style with StyleManager::Register to use it.
    struct StyleSheet
    {
        std::unordered_map<std::string, Style>          styles; ///< keyed by class name, without the '.'
        /// The classes with `:first-child` / `:last-child` / `:only-child` rules; register them
        /// with StyleManager::RegisterWithChildStyles.
        std::unordered_map<std::string, ChildStyles>    childStyles;
        std::unordered_map<std::string, CustomProperty> customProperties; ///< keyed by name including the leading "--"
        /// What the `@property` rules say about the custom properties, in source order; a
        /// second rule for a name replaces the first in its place.
        std::vector<CustomPropertyInfo>                 propertyInfos;
        /// The custom properties whose colors the styles use, numbered from 1 in this order:
        /// ColorStyle::variable of a color taken from `var(--name)` is its number.
        std::vector<std::string>                        colorVariables;        std::vector<FontFace>                           fontFaces; ///< in source order
        Style                                           baseStyle; ///< what `*` rules set; styles start from it
        std::vector<ParseError>                         errors; ///< in the order found

        /// True when parsing reported any error.
        bool                  HasErrors()                            const { return !errors.empty(); }
        /// The style of a class name (without the '.'); nullptr when not defined.
        const Style*          GetStyle(std::string_view name)          const;
        /// A custom property by name (with the leading "--"); nullptr when not defined.
        const CustomProperty* GetCustomProperty(std::string_view name) const;
        /// The `@property` rule's facts for a custom property (with the leading "--");
        /// nullptr when it has none.
        const CustomPropertyInfo* GetPropertyInfo(std::string_view name) const;
    };

    /// Parses a theme's CSS (the subset TachyonUI supports, see docs/css.md) into a
    /// StyleSheet. Errors don't stop it: a bad declaration or rule is skipped and
    /// reported in StyleSheet::errors, and the rest is parsed.
    StyleSheet ParseStyleSheet(std::string_view source);

    /// @}
} // namespace tachyon
