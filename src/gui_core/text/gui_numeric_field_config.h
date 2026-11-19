#pragma once

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <limits>
#include <stdint.h>

namespace tachyon
{
    /// @addtogroup text
    /// @{

    namespace detail
    {
        /// `startValue` moved by `delta`, rounded toward zero and kept in [minVal, maxVal] (min
        /// wins when above max). Values past 2^53, where a double can't hold every integer,
        /// move by whole steps in integer math so a small drag still moves them.
        template<typename T>
        T DragInteger(double delta, T startValue, T minVal, T maxVal)
        {
            if (std::isnan(delta))
                return std::max(minVal, std::min(startValue, maxVal));
            constexpr double kExact = 9007199254740992.0;   // 2^53
            double result;
            if (std::fabs(double(startValue)) <= kExact)
                result = double(startValue) + delta;
            else
            {
                // Only 64-bit types get here. In unsigned math, which wraps as two's complement
                // does: the room left on the side moved to, and the move within it.
                const double   step = std::trunc(delta);
                const uint64_t from = uint64_t(startValue);
                const uint64_t room = step >= 0 ? uint64_t(std::numeric_limits<T>::max()) - from
                                                : from - uint64_t(std::numeric_limits<T>::lowest());
                if (std::fabs(step) >= double(room))
                    return step >= 0 ? std::max(minVal, maxVal) : minVal;
                const uint64_t by = uint64_t(std::fabs(step));
                const T moved = T(step >= 0 ? from + by : from - by);
                return std::max(minVal, std::min(moved, maxVal));
            }
            // double(max) of a 64-bit type rounds up past it: compare before converting.
            if (result <= double(minVal)) return minVal;
            if (result >= double(maxVal)) return std::max(minVal, maxVal);
            return T(result);
        }
    }

    /// Drag speed and range of a numeric Field, specialized per value type. Typed and
    /// dragged values are clamped to [minVal, maxVal].
    ///
    ///     Field(opacity, NumericFieldConfig<float>{ .minVal = 0.0f, .maxVal = 1.0f });
    template<typename T>
    struct NumericFieldConfig {};
    
    /// NumericFieldConfig for double values.
    template<>
    struct NumericFieldConfig<double>
    {
        double  stepPerPixel = 0.01; ///< value change per pixel dragged
        double  minVal       = -DBL_MAX; ///< lowest allowed value
        double  maxVal       = DBL_MAX; ///< highest allowed value
        int     decimals     = -1;      ///< digits shown after the point (2: "1.00"); -1: as few as the value needs

        /// The value after dragging `offset` pixels from startValue; speedScale is the modifier
        /// factor (Ctrl 0.1, Shift 10). The result is clamped to the range.
        double Calculate(int32_t offset, double speedScale, double startValue) const
        {
            double result = startValue + offset * speedScale * stepPerPixel;
            if (std::isnan(result)) return 0.0;
            return std::max(minVal, std::min(result, maxVal));
        }
	};

    /// NumericFieldConfig for float values.
    template<>
    struct NumericFieldConfig<float>
    {
        double  stepPerPixel = 0.01; ///< value change per pixel dragged
        float   minVal       = -FLT_MAX; ///< lowest allowed value
        float   maxVal       = FLT_MAX; ///< highest allowed value
        int     decimals     = -1;      ///< digits shown after the point (2: "1.00"); -1: as few as the value needs

        /// The value after dragging `offset` pixels from startValue; speedScale is the modifier
        /// factor (Ctrl 0.1, Shift 10). The result is clamped to the range.
        float Calculate(int32_t offset, double speedScale, float startValue) const
        {
            double result = double(startValue) + offset * speedScale * stepPerPixel;
            if (std::isnan(result)) return 0.0f;
            return float(std::max(double(minVal), std::min(result, double(maxVal))));
        }
    };

    /// NumericFieldConfig for uint64_t values.
    template<>
    struct NumericFieldConfig<uint64_t>
    {
        double  stepPerPixel = 1.00; ///< value change per pixel dragged
        uint64_t minVal = 0; ///< lowest allowed value
        uint64_t maxVal = UINT64_MAX; ///< highest allowed value
        /// The value after dragging `offset` pixels from startValue; speedScale is the modifier
        /// factor (Ctrl 0.1, Shift 10). The result is clamped to the range.
        uint64_t Calculate(int32_t offset, double speedScale, uint64_t startValue) const
        {
            return detail::DragInteger(offset * speedScale * stepPerPixel, startValue, minVal, maxVal);
        }
    };

    /// NumericFieldConfig for uint32_t values.
    template<>
    struct NumericFieldConfig<uint32_t>
    {
        double  stepPerPixel = 1.00; ///< value change per pixel dragged
        uint32_t minVal = 0; ///< lowest allowed value
        uint32_t maxVal = UINT32_MAX; ///< highest allowed value
        /// The value after dragging `offset` pixels from startValue; speedScale is the modifier
        /// factor (Ctrl 0.1, Shift 10). The result is clamped to the range.
        uint32_t Calculate(int32_t offset, double speedScale, uint32_t startValue) const
        {
            return detail::DragInteger(offset * speedScale * stepPerPixel, startValue, minVal, maxVal);
        }
    };

    /// NumericFieldConfig for uint16_t values.
    template<>
    struct NumericFieldConfig<uint16_t>
    {
        double  stepPerPixel = 1.00; ///< value change per pixel dragged
        uint16_t minVal = 0; ///< lowest allowed value
        uint16_t maxVal = UINT16_MAX; ///< highest allowed value
        /// The value after dragging `offset` pixels from startValue; speedScale is the modifier
        /// factor (Ctrl 0.1, Shift 10). The result is clamped to the range.
        uint16_t Calculate(int32_t offset, double speedScale, uint16_t startValue) const
        {
            return detail::DragInteger(offset * speedScale * stepPerPixel, startValue, minVal, maxVal);
        }
    };

    /// NumericFieldConfig for uint8_t values.
    template<>
    struct NumericFieldConfig<uint8_t>
    {
        double  stepPerPixel = 1.00; ///< value change per pixel dragged
        uint8_t minVal = 0; ///< lowest allowed value
        uint8_t maxVal = UINT8_MAX; ///< highest allowed value
        /// The value after dragging `offset` pixels from startValue; speedScale is the modifier
        /// factor (Ctrl 0.1, Shift 10). The result is clamped to the range.
        uint8_t Calculate(int32_t offset, double speedScale, uint8_t startValue) const
        {
            return detail::DragInteger(offset * speedScale * stepPerPixel, startValue, minVal, maxVal);
        }
    };

    /// NumericFieldConfig for int64_t values.
    template<>
    struct NumericFieldConfig<int64_t>
    {
        double  stepPerPixel = 1.00; ///< value change per pixel dragged
        int64_t minVal = INT64_MIN; ///< lowest allowed value
        int64_t maxVal = INT64_MAX; ///< highest allowed value
        /// The value after dragging `offset` pixels from startValue; speedScale is the modifier
        /// factor (Ctrl 0.1, Shift 10). The result is clamped to the range.
        int64_t Calculate(int32_t offset, double speedScale, int64_t startValue) const
        {
            return detail::DragInteger(offset * speedScale * stepPerPixel, startValue, minVal, maxVal);
        }
    };

    /// NumericFieldConfig for int32_t values.
    template<>
    struct NumericFieldConfig<int32_t>
    {
        double  stepPerPixel = 1.00; ///< value change per pixel dragged
        int32_t minVal = INT32_MIN; ///< lowest allowed value
        int32_t maxVal = INT32_MAX; ///< highest allowed value
        /// The value after dragging `offset` pixels from startValue; speedScale is the modifier
        /// factor (Ctrl 0.1, Shift 10). The result is clamped to the range.
        int32_t Calculate(int32_t offset, double speedScale, int32_t startValue) const
        {
            return detail::DragInteger(offset * speedScale * stepPerPixel, startValue, minVal, maxVal);
        }
    };

    /// NumericFieldConfig for int16_t values.
    template<>
    struct NumericFieldConfig<int16_t>
    {
        double  stepPerPixel = 1.00; ///< value change per pixel dragged
        int16_t minVal = INT16_MIN; ///< lowest allowed value
        int16_t maxVal = INT16_MAX; ///< highest allowed value
        /// The value after dragging `offset` pixels from startValue; speedScale is the modifier
        /// factor (Ctrl 0.1, Shift 10). The result is clamped to the range.
        int16_t Calculate(int32_t offset, double speedScale, int16_t startValue) const
        {
            return detail::DragInteger(offset * speedScale * stepPerPixel, startValue, minVal, maxVal);
        }
    };

    /// NumericFieldConfig for int8_t values.
    template<>
    struct NumericFieldConfig<int8_t>
    {
        double  stepPerPixel = 1.00; ///< value change per pixel dragged
        int8_t minVal = INT8_MIN; ///< lowest allowed value
        int8_t maxVal = INT8_MAX; ///< highest allowed value
        /// The value after dragging `offset` pixels from startValue; speedScale is the modifier
        /// factor (Ctrl 0.1, Shift 10). The result is clamped to the range.
        int8_t Calculate(int32_t offset, double speedScale, int8_t startValue) const
        {
            return detail::DragInteger(offset * speedScale * stepPerPixel, startValue, minVal, maxVal);
        }
    };
    /// @}
}
