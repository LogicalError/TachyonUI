#include <gui_core/text/gui_numeric_text.h>
#include <gui_core/gui_from_chars.h>

#include <cassert>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace tachyon
{
	namespace
	{
	// Normalizes a run of leading zeros in `s` starting at `start`.
	// "007" -> "7"  (followed by another digit)
	// "000" -> "0", "00.5" -> "0.5"  (followed by non-digit or end: keep one zero)
	void strip_leading_zeros(std::string& s, size_t start)
	{
		size_t i = start;
		while (i < s.size() && s[i] == '0') i++;
		const size_t num_zeros = i - start;
		if (num_zeros <= 1)
			return;

		const bool followed_by_digit = (i < s.size()) && (s[i] >= '1' && s[i] <= '9');
		if (followed_by_digit)
			s.erase(start, num_zeros);
		else
			s.erase(start, num_zeros - 1);
	}

	bool filter_float_text(std::string& text)
	{
		std::string filtered;
		filtered.reserve(text.size());

		// [sign] mantissa [e|E [sign] digits]. Partial forms ("1e", "1e-") stay, so
		// exponent notation can be typed or pasted; the value parses once complete.
		bool seen_dot        = false;
		bool seen_digit      = false;
		size_t exponent_at   = std::string::npos;  // index of 'e' in `filtered`

		for (char c : text)
		{
			if (c >= '0' && c <= '9')
			{
				filtered.push_back(c);
				seen_digit = true;
				continue;
			}

			if (c == '+' || c == '-')
			{
				if (filtered.empty() || (exponent_at != std::string::npos && filtered.size() == exponent_at + 1))
					filtered.push_back(c);
				continue;
			}

			if (c == '.')
			{
				if (!seen_dot && exponent_at == std::string::npos)
				{
					filtered.push_back(c);
					seen_dot = true;
				}
				continue;
			}

			if ((c == 'e' || c == 'E') && seen_digit && exponent_at == std::string::npos)
			{
				exponent_at = filtered.size();
				filtered.push_back(c);
				continue;
			}
		}

		{
			const size_t mantissa_start = (!filtered.empty() && (filtered[0] == '+' || filtered[0] == '-')) ? 1 : 0;
			std::string exponent;
			if (exponent_at != std::string::npos)
			{
				exponent = filtered.substr(exponent_at);
				filtered.resize(exponent_at);
			}
			strip_leading_zeros(filtered, mantissa_start);
			filtered += exponent;
		}

		if (filtered == text)
			return false;

		text = std::move(filtered);
		return true;
	}

	bool filter_int_text(std::string& text)
	{
		std::string filtered;
		filtered.reserve(text.size());

		for (char c : text)
		{
			if (c >= '0' && c <= '9')
			{
				filtered.push_back(c);
				continue;
			}

			if ((c == '+' || c == '-') && filtered.empty())
			{
				filtered.push_back(c);
				continue;
			}
		}

		const size_t digits_start = (!filtered.empty() && (filtered[0] == '+' || filtered[0] == '-')) ? 1 : 0;
		strip_leading_zeros(filtered, digits_start);

		if (filtered == text)
			return false;

		text = std::move(filtered);
		return true;
	}

	bool filter_uint_text(std::string& text)
	{
		std::string filtered;
		filtered.reserve(text.size());

		for (char c : text)
		{
			if (c >= '0' && c <= '9')
			{
				filtered.push_back(c);
				continue;
			}
		}

		strip_leading_zeros(filtered, 0);

		if (filtered == text)
			return false;

		text = std::move(filtered);
		return true;
	}

	// std::to_chars / from_chars, unlike printf and strtod, ignore the C locale: "1.5" stays
	// "1.5" in a host app that set a comma-decimal one.
	template<typename T, typename... Format>
	std::string FormatChars(T value, Format... format)
	{
		char buffer[128];
		const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), value, format...);
		return result.ec == std::errc{} ? std::string(buffer, result.ptr) : std::string("0");
	}

	// The whole of `text` as a number. Leading white space and a '+' are taken, as strtod did.
	template<typename T>
	bool ParseChars(std::string_view text, T& out)
	{
		size_t i = 0;
		while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
			i++;
		if (i < text.size() && text[i] == '+')
		{
			i++;
			if (i < text.size() && text[i] == '-')
				return false;
		}
		const char* first = text.data() + i;
		const char* last  = text.data() + text.size();
		if (first == last)
			return false;
		T value{};
		const std::from_chars_result result = FromChars(first, last, value);
		if (result.ec != std::errc{} || result.ptr != last)
			return false;
		out = value;
		return true;
	}

	std::string format_to_text(double value, bool clamp)
	{
		if (std::isinf(value))
		{
			if (value < 0)
				return "-Infinity";
			return "Infinity";					
		}
		if (std::isnan(value))
		{
			return "NaN";
		}
		if (clamp)
		{
			return FormatChars(value, std::chars_format::general, 9);   // %.9g, in any locale
		}
		return FormatChars(value, std::chars_format::general, 6);       // %g
	}

	// The fewest digits that read back as the same float: 1.8f is "1.8", not the
	// "1.79999995" its nine significant digits as a double would show.
	std::string format_to_text(float value, bool clamp)
	{
		if (!clamp || !std::isfinite(value))
			return format_to_text(static_cast<double>(value), clamp);
		char buffer[64];
		const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), value);
		if (result.ec != std::errc{})
			return format_to_text(static_cast<double>(value), clamp);
		return std::string(buffer, result.ptr);
	}

	std::string format_to_text(int64_t value)
	{
		return FormatChars(value);
	}
	std::string format_to_text(int32_t value) { return format_to_text(static_cast<int64_t>(value)); }
	std::string format_to_text(int16_t value) { return format_to_text(static_cast<int64_t>(value)); }
	std::string format_to_text(int8_t  value) { return format_to_text(static_cast<int64_t>(value)); }

	std::string format_to_text(uint64_t value)
	{
		return FormatChars(value);
	}
	std::string format_to_text(uint32_t value) { return format_to_text(static_cast<uint64_t>(value)); }
	std::string format_to_text(uint16_t value) { return format_to_text(static_cast<uint64_t>(value)); }
	std::string format_to_text(uint8_t  value) { return format_to_text(static_cast<uint64_t>(value)); }

	std::string format_to_text(NumericType type, const NumericValue& outValue)
	{
		switch (type)
		{
			case NumericType::TypeFloat:  return format_to_text(outValue.value_float, true);
			case NumericType::TypeDouble: return format_to_text(outValue.value_double, true);

			case NumericType::TypeUInt64: return format_to_text(outValue.value_uint64);
			case NumericType::TypeUInt32: return format_to_text(outValue.value_uint32);
			case NumericType::TypeUInt16: return format_to_text(outValue.value_uint16);
			case NumericType::TypeUInt8:  return format_to_text(outValue.value_uint8);

			case NumericType::TypeInt64: return format_to_text(outValue.value_int64);
			case NumericType::TypeInt32: return format_to_text(outValue.value_int32);
			case NumericType::TypeInt16: return format_to_text(outValue.value_int16);
			case NumericType::TypeInt8:  return format_to_text(outValue.value_int8);

			default:
				return {};
		}
	}

	bool try_parse_text(std::string_view text, double& outValue)
	{
		double value = 0.0;
		if (!ParseChars(text, value) || !std::isfinite(value)) return false;
		outValue = value;
		return true;
	}

	bool try_parse_text(std::string_view text, float& outValue)
	{
		float value = 0.0f;
		if (!ParseChars(text, value) || !std::isfinite(value)) return false;
		outValue = value;
		return true;
	}

	bool try_parse_text(std::string_view text, int64_t& outValue)
	{
		return ParseChars(text, outValue);
	}

	bool try_parse_text(std::string_view text, uint64_t& outValue)
	{
		return ParseChars(text, outValue);
	}

	bool try_parse_text(std::string_view text, int32_t& outValue)
	{
		int64_t value;
		if (!try_parse_text(text, value)) return false;
		if (value < INT32_MIN || value > INT32_MAX) return false;
		outValue = static_cast<int32_t>(value);
		return true;
	}

	bool try_parse_text(std::string_view text, uint32_t& outValue)
	{
		uint64_t value;
		if (!try_parse_text(text, value)) return false;
		if (value > UINT32_MAX) return false;
		outValue = static_cast<uint32_t>(value);
		return true;
	}

	bool try_parse_text(std::string_view text, int16_t& outValue)
	{
		int64_t value;
		if (!try_parse_text(text, value)) return false;
		if (value < INT16_MIN || value > INT16_MAX) return false;
		outValue = static_cast<int16_t>(value);
		return true;
	}

	bool try_parse_text(std::string_view text, uint16_t& outValue)
	{
		uint64_t value;
		if (!try_parse_text(text, value)) return false;
		if (value > UINT16_MAX) return false;
		outValue = static_cast<uint16_t>(value);
		return true;
	}

	bool try_parse_text(std::string_view text, int8_t& outValue)
	{
		int64_t value;
		if (!try_parse_text(text, value)) return false;
		if (value < INT8_MIN || value > INT8_MAX) return false;
		outValue = static_cast<int8_t>(value);
		return true;
	}

	bool try_parse_text(std::string_view text, uint8_t& outValue)
	{
		uint64_t value;
		if (!try_parse_text(text, value)) return false;
		if (value > UINT8_MAX) return false;
		outValue = static_cast<uint8_t>(value);
		return true;
	}

	bool try_parse_text(std::string_view text, NumericType type, NumericValue& outValue)
	{
		switch (type)
		{
			case NumericType::TypeFloat:  return try_parse_text(text, outValue.value_float);
			case NumericType::TypeDouble: return try_parse_text(text, outValue.value_double);

			case NumericType::TypeUInt64: return try_parse_text(text, outValue.value_uint64);
			case NumericType::TypeUInt32: return try_parse_text(text, outValue.value_uint32);
			case NumericType::TypeUInt16: return try_parse_text(text, outValue.value_uint16);
			case NumericType::TypeUInt8:  return try_parse_text(text, outValue.value_uint8);

			case NumericType::TypeInt64: return try_parse_text(text, outValue.value_int64);
			case NumericType::TypeInt32: return try_parse_text(text, outValue.value_int32);
			case NumericType::TypeInt16: return try_parse_text(text, outValue.value_int16);
			case NumericType::TypeInt8:  return try_parse_text(text, outValue.value_int8);

			default:
				outValue = {};
				return false;
		}
	}

	using FilterFunc = bool(std::string& text);
	FilterFunc* get_text_filter_func(NumericType type)
	{
		if ((int(type) & int(NumericType::TypeUnsignedBit)) == int(NumericType::TypeUnsignedBit))
			return &filter_uint_text;
		if ((int(type) & int(NumericType::TypeSignedBit)) == int(NumericType::TypeSignedBit))
			return &filter_int_text;
		if ((int(type) & int(NumericType::TypeFloatBit)) == int(NumericType::TypeFloatBit))
			return &filter_float_text;
		assert(false && "Unknown numeric type");
		return nullptr;
	}
	}

	std::string FormatNumber(NumericType type, const NumericValue& value) { return format_to_text(type, value); }

	bool ParseNumber(std::string_view text, NumericType type, NumericValue& outValue) { return try_parse_text(text, type, outValue); }

	bool FilterNumberText(NumericType type, std::string& text)
	{
		FilterFunc* filter = get_text_filter_func(type);
		return filter != nullptr && filter(text);
	}

	bool IsNumberCharacter(NumericType type, uint32_t codepoint)
	{
		if (codepoint >= '0' && codepoint <= '9')
			return true;
		if ((int(type) & int(NumericType::TypeUnsignedBit)) == int(NumericType::TypeUnsignedBit))
			return false;
		if (codepoint == '+' || codepoint == '-')
			return true;
		return (int(type) & int(NumericType::TypeFloatBit)) == int(NumericType::TypeFloatBit)
			&& (codepoint == '.' || codepoint == 'e' || codepoint == 'E');
	}
}
