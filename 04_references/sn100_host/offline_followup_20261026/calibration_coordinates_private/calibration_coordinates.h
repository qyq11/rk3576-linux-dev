#ifndef SN100_PRIVATE_CALIBRATION_COORDINATES_H
#define SN100_PRIVATE_CALIBRATION_COORDINATES_H

namespace Sn100Calibration
{
enum class CoordinateMode
{
    Identity = 0,
    Log10 = 1,
    Reciprocal = 2,
    NaturalLog = 3
};

enum class CoordinateDirection
{
    Forward,
    Inverse
};

enum class CoordinateError
{
    None,
    UnsupportedMode,
    UnsupportedDirection,
    NonFiniteInput,
    Domain,
    Unrepresentable
};

class CoordinateValue;
CoordinateValue transformCalibrationCoordinate(double input, CoordinateMode mode,
                                              CoordinateDirection direction) noexcept;

// A rejected result exposes its error, but never exposes a numeric payload.
// readValue returns false and leaves the caller's destination unchanged.
class CoordinateValue
{
public:
    bool valid() const noexcept;
    CoordinateError error() const noexcept;
    bool readValue(double &destination) const noexcept;

private:
    CoordinateValue(CoordinateError error, double value) noexcept;
    CoordinateError error_;
    double value_;
    friend CoordinateValue transformCalibrationCoordinate(double input, CoordinateMode mode,
                                                         CoordinateDirection direction) noexcept;
};

const char *coordinateErrorName(CoordinateError error) noexcept;
}

#endif
