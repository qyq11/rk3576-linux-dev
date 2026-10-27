#include "calibration_coordinates.h"
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>

using namespace Sn100Calibration;
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559 &&
              std::numeric_limits<double>::digits == 53 &&
              std::numeric_limits<double>::has_denorm == std::denorm_present,
              "These boundary cases require binary64 double with subnormal values");

namespace
{
struct ApiCase
{
    const char *id;
    int mode;
    int direction;
    const char *inputToken;
    double input;
    int inputSignbit;
    bool expectedValid;
    double expectedValue;
    const char *expectedError;
    int expectedSignbit;
};

// Fixed expectations are copied explicitly from api_boundary_cases.json.
// No transform is used to construct an expected value.
const ApiCase cases[] =
{
    {"direction_minus_one", 0, -1, "12.5", 12.5, 0, false, 0, "UnsupportedDirection", -1},
    {"direction_two", 0, 2, "12.5", 12.5, 0, false, 0, "UnsupportedDirection", -1},
    {"reciprocal_negative_zero", 2, 0, "negative_zero", -0.0, 1, false, 0, "Domain", -1},
    {"reciprocal_negative_four", 2, 0, "-4", -4.0, 1, true, -0.25, "None", 1},
    {"reciprocal_denorm_min_overflow", 2, 0, "denorm_min",
        std::numeric_limits<double>::denorm_min(), 0, false, 0, "Unrepresentable", -1},
    {"identity_negative_zero_sign", 0, 0, "negative_zero", -0.0, 1, true, -0.0, "None", 1}
};
constexpr unsigned caseCount = sizeof(cases) / sizeof(cases[0]);
static_assert(caseCount == 6, "Keep the supplemental six cases separate from the original 23");

bool sameBits(const double &left, const double &right)
{
    return std::memcmp(&left, &right, sizeof(double)) == 0;
}
}

int main()
{
    std::cout << "id\tmode\tdirection_integer\tinput_token\texpected_valid\texpected_value"
              << "\texpected_error\texpected_output_signbit\tactual_input\tinput_signbit"
              << "\tactual_valid\tactual_value\tactual_error\treadable\toutput_signbit"
              << "\tinput_unchanged\tinvalid_read_guard\tpassed\n" << std::setprecision(17);
    unsigned failures = 0;
    for (const auto &test : cases)
    {
        const double input = test.input;
        const double before = input;
        // Deliberately bypass the original TSV's forward/inverse token restriction.
        const auto result = transformCalibrationCoordinate(input,
            static_cast<CoordinateMode>(test.mode), static_cast<CoordinateDirection>(test.direction));
        const double sentinel = 8123.75;
        double output = sentinel;
        const bool readable = result.readValue(output);
        const int inputSignbit = std::signbit(input) ? 1 : 0;
        const int outputSignbit = readable ? (std::signbit(output) ? 1 : 0) : -1;
        const bool inputUnchanged = sameBits(input, before);
        const bool invalidReadGuard = result.valid() || (!readable && sameBits(output, sentinel));
        const bool passed = result.valid() == test.expectedValid && readable == test.expectedValid &&
            std::strcmp(coordinateErrorName(result.error()), test.expectedError) == 0 &&
            inputSignbit == test.inputSignbit && inputUnchanged && invalidReadGuard &&
            (!test.expectedValid || (std::isfinite(output) && output == test.expectedValue &&
                                    outputSignbit == test.expectedSignbit));

        std::cout << test.id << '\t' << test.mode << '\t' << test.direction << '\t'
                  << test.inputToken << '\t' << test.expectedValid << '\t';
        if (test.expectedValid)
        {
            std::cout << test.expectedValue;
        }
        else
        {
            std::cout << '-';
        }
        std::cout << '\t' << test.expectedError << '\t' << test.expectedSignbit << '\t'
                  << input << '\t' << inputSignbit << '\t' << result.valid() << '\t';
        if (readable)
        {
            std::cout << output;
        }
        else
        {
            std::cout << "<unavailable>";
        }
        std::cout << '\t' << coordinateErrorName(result.error()) << '\t' << readable << '\t'
                  << outputSignbit << '\t' << inputUnchanged << '\t' << invalidReadGuard << '\t'
                  << passed << '\n';
        failures += passed ? 0 : 1;
    }
    std::cerr << "API_BOUNDARIES cases=" << caseCount << " passed=" << (caseCount - failures)
              << " failed=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
