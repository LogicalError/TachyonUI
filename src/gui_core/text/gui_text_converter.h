#pragma once

#include <gui_core/text/gui_numeric_text.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>

namespace tachyon
{
    /// @addtogroup text
    /// @{

    /// What an editable field takes as text, asked while it is edited. An instance given to
    /// the text system must outlive the field (a static or a member, not a temporary).
    class ITextValidator
    {
    public:
        virtual ~ITextValidator() = default;

        /// Whether typing `codepoint` can be part of the text.
        virtual bool AcceptsCharacter(uint32_t /*codepoint*/) const { return true; }
        /// Strips what the text can't hold (pasted or dropped text, a typed character);
        /// true if `text` changed.
        virtual bool Filter(std::string& /*text*/) const { return false; }
        /// Whether an IME may compose text into the field.
        virtual bool AcceptsComposition() const { return true; }
    };

    /// Turns a field's value into the text it shows and edits, and edited text back into a
    /// value. Specialize it for a type to edit that type in a field, or pass any object with
    /// the same members (and ITextValidator as its base) to RegisterEditableValue:
    ///
    ///   std::string Format(const T& value, bool editing) const;   // editing: the focus just arrived
    ///   bool        Parse(std::string_view text, T& value) const; // false: not a T, value untouched
    template<typename T>
    struct TextConverter;

    template<typename T>
    concept NumericFieldType = GetNumericType(T{}) != NumericType::None;

    template<NumericFieldType T>
    struct TextConverter<T> : ITextValidator
    {
        /// For floats and doubles: the digits shown after the point (2: "1.00"); -1 shows as
        /// few as the value needs. Typed text is parsed as it is, not rounded to them.
        int decimals = -1;

        std::string Format(const T& value, bool editing) const
        {
            // NaN and infinities aren't text to start editing from: start from 0.
            if constexpr (std::is_floating_point_v<T>)
            {
                if (editing && !std::isfinite(value))
                    return "0";
                if (decimals >= 0 && std::isfinite(value))
                {
                    char buffer[64];
                    const int written = std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, double(value));
                    if (written > 0 && written < int(sizeof(buffer)))
                        return std::string(buffer, size_t(written));
                }
            }
            NumericValue number{};
            const NumericType type = CopyNumericTypeTo(value, number);
            return FormatNumber(type, number);
        }

        bool Parse(std::string_view text, T& value) const
        {
            NumericValue number{};
            const NumericType type = GetNumericType(value);
            if (!ParseNumber(text, type, number))
                return false;
            return CopyNumericTypeTo(type, number, value);
        }

        bool AcceptsCharacter(uint32_t codepoint) const override { return IsNumberCharacter(GetNumericType(T{}), codepoint); }
        bool Filter(std::string& text) const override { return FilterNumberText(GetNumericType(T{}), text); }
        bool AcceptsComposition() const override { return false; }   // digits and signs only
    };

    /// The converter a field uses for T when it is given none.
    template<typename T>
    const TextConverter<T>& DefaultTextConverter()
    {
        static const TextConverter<T> converter;
        return converter;
    }

    /// The converter that shows a float or double with `decimals` digits after the point (0
    /// to 9; less shows as few as the value needs). One per count, so it outlives the fields.
    template<typename T>
        requires std::is_floating_point_v<T>
    const TextConverter<T>& FixedDecimalsConverter(int decimals)
    {
        static const auto converters = [] {
            std::array<TextConverter<T>, 10> all{};
            for (int i = 0; i < int(all.size()); ++i)
                all[size_t(i)].decimals = i;
            return all;
        }();
        if (decimals < 0)
            return DefaultTextConverter<T>();
        return converters[size_t(std::min(decimals, int(converters.size()) - 1))];
    }
    /// @}
}
