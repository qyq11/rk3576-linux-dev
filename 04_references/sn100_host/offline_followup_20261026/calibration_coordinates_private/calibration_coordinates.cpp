#include "calibration_coordinates.h"
#include <cmath>
#include <limits>

namespace Sn100Calibration
{
CoordinateValue::CoordinateValue(CoordinateError error, double value) noexcept
    : error_(error), value_(error == CoordinateError::None
        ? value : std::numeric_limits<double>::quiet_NaN())
{
}

bool CoordinateValue::valid() const noexcept
{
    return error_ == CoordinateError::None;
}

CoordinateError CoordinateValue::error() const noexcept
{
    return error_;
}

bool CoordinateValue::readValue(double &destination) const noexcept
{
    if (!valid())
    {
        return false;
    }
    destination = value_;
    return true;
}

CoordinateValue transformCalibrationCoordinate(double input, CoordinateMode mode,
                                              CoordinateDirection direction) noexcept
{
    // 原依据：algorithmcore.h:188-227 / AL4A_DLL.h:341-383。
    // 这是独立数学预览：数值按调用方明确的单位输入，不换算倍率，不写回原值。
    switch (mode)
    {
    case CoordinateMode::Identity:
    case CoordinateMode::Log10:
    case CoordinateMode::Reciprocal:
    case CoordinateMode::NaturalLog:
        break;
    default:
        return CoordinateValue(CoordinateError::UnsupportedMode, 0);
    }
    if (direction != CoordinateDirection::Forward && direction != CoordinateDirection::Inverse)
    {
        return CoordinateValue(CoordinateError::UnsupportedDirection, 0);
    }
    if (!std::isfinite(input))
    {
        return CoordinateValue(CoordinateError::NonFiniteInput, 0);
    }

    const bool inverse = direction == CoordinateDirection::Inverse;
    double output = input;
    switch (mode)
    {
    case CoordinateMode::Identity:
        break;
    case CoordinateMode::Log10:
    case CoordinateMode::NaturalLog:
        if (!inverse && input <= 0)
        {
            return CoordinateValue(CoordinateError::Domain, 0);
        }
        if (mode == CoordinateMode::Log10)
        {
            output = inverse ? std::pow(10.0, input) : std::log10(input);
        }
        else
        {
            output = inverse ? std::exp(input) : std::log(input);
        }
        break;
    case CoordinateMode::Reciprocal:
        if (input == 0)
        {
            return CoordinateValue(CoordinateError::Domain, 0);
        }
        output = 1.0 / input;
        break;
    }

    // 有限但下溢到0的指数结果也拒绝。恒等0、log(1)=0仍是合法值。
    // 这是明确的新离线错误合同，不宣称等同于厂商DLL的极值/错误行为。
    const bool mathematicallyNonzero = mode == CoordinateMode::Reciprocal ||
        (inverse && (mode == CoordinateMode::Log10 || mode == CoordinateMode::NaturalLog));
    if (!std::isfinite(output) || (mathematicallyNonzero && output == 0))
    {
        return CoordinateValue(CoordinateError::Unrepresentable, 0);
    }
    return CoordinateValue(CoordinateError::None, output);
}

const char *coordinateErrorName(CoordinateError error) noexcept
{
    switch (error)
    {
    case CoordinateError::None: return "None";
    case CoordinateError::UnsupportedMode: return "UnsupportedMode";
    case CoordinateError::UnsupportedDirection: return "UnsupportedDirection";
    case CoordinateError::NonFiniteInput: return "NonFiniteInput";
    case CoordinateError::Domain: return "Domain";
    case CoordinateError::Unrepresentable: return "Unrepresentable";
    }
    return "UnknownError";
}
}
