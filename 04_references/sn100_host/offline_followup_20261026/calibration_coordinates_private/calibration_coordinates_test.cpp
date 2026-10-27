#include "calibration_coordinates.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Sn100Calibration;
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559,
              "The reviewed overflow/underflow contract requires binary64 double");

namespace
{
std::vector<std::string> columns(const std::string &line)
{
    std::vector<std::string> result;
    std::istringstream stream(line);
    std::string value;
    while (std::getline(stream, value, '\t'))
    {
        result.push_back(value);
    }
    return result;
}

double number(const std::string &text)
{
    std::size_t end = 0;
    const double value = std::stod(text, &end);
    if (end != text.size())
    {
        throw std::runtime_error("Unexpected numeric token");
    }
    return value;
}

bool sameBits(const double &left, const double &right)
{
    return std::memcmp(&left, &right, sizeof(double)) == 0;
}

bool nearExpected(double actual, double expected, double absolute, double relative)
{
    return std::isfinite(actual) && std::isfinite(expected) &&
        std::fabs(actual - expected) <= std::max(absolute, relative * std::fabs(expected));
}
}

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        std::cerr << "usage: calibration_coordinates_test cases.tsv absolute_tolerance relative_tolerance\n";
        return 2;
    }
    try
    {
        const double absolute = number(argv[2]);
        const double relative = number(argv[3]);
        if (!std::isfinite(absolute) || !std::isfinite(relative) || absolute <= 0 || relative <= 0)
        {
            throw std::runtime_error("Invalid tolerance");
        }
        std::ifstream fixture(argv[1]);
        std::string line;
        if (!fixture || !std::getline(fixture, line) ||
            line != "id\tmode\tdirection\tinput\texpected_valid\texpected_value\texpected_error")
        {
            throw std::runtime_error("Cannot read the reviewed case header");
        }
        std::cout << line << "\tactual_valid\tactual_value\tactual_error\treadable\tinput_unchanged"
                  << "\tinvalid_read_guard\tpassed\n" << std::setprecision(17);
        int cases = 0;
        int failures = 0;
        while (std::getline(fixture, line))
        {
            const auto field = columns(line);
            if (field.size() != 7 || (field[4] != "0" && field[4] != "1"))
            {
                throw std::runtime_error("Malformed reviewed case row");
            }
            std::size_t end = 0;
            const int modeNumber = std::stoi(field[1], &end);
            if (end != field[1].size() || (field[2] != "forward" && field[2] != "inverse"))
            {
                throw std::runtime_error("Unknown mode/direction token");
            }
            const double input = field[3] == "quiet_NaN" ? std::numeric_limits<double>::quiet_NaN() :
                field[3] == "positive_infinity" ? std::numeric_limits<double>::infinity() : number(field[3]);
            const double before = input;
            const auto direction = field[2] == "forward" ? CoordinateDirection::Forward : CoordinateDirection::Inverse;
            const auto result = transformCalibrationCoordinate(input, static_cast<CoordinateMode>(modeNumber), direction);
            const double sentinel = 8123.75;
            double output = sentinel;
            const bool readable = result.readValue(output);
            const bool inputUnchanged = sameBits(input, before);
            const bool invalidReadGuard = result.valid() || (!readable && sameBits(output, sentinel));
            const bool expectedValid = field[4] == "1";
            const bool passed = result.valid() == expectedValid && readable == expectedValid &&
                field[6] == coordinateErrorName(result.error()) && inputUnchanged && invalidReadGuard &&
                (!expectedValid || nearExpected(output, number(field[5]), absolute, relative));
            std::cout << line << '\t' << result.valid() << '\t';
            if (readable)
            {
                std::cout << output;
            }
            else
            {
                std::cout << "<unavailable>";
            }
            std::cout << '\t' << coordinateErrorName(result.error()) << '\t' << readable << '\t'
                      << inputUnchanged << '\t' << invalidReadGuard << '\t' << passed << '\n';
            ++cases;
            failures += passed ? 0 : 1;
        }
        if (fixture.bad() || cases != 23)
        {
            throw std::runtime_error("Expected exactly 23 reviewed cases");
        }
        std::cerr << "TOTAL cases=" << cases << " passed=" << (cases - failures) << " failed=" << failures << '\n';
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "INPUT_ERROR " << error.what() << '\n';
        return 2;
    }
}
