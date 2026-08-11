/**
 SpecUtils: a library to parse, save, and manipulate gamma spectrum data files.
 Copyright (C) 2016 William Johnson

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <cmath>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <cassert>
#include <array>
#include <chrono>
#include <regex>

#include "3rdparty/date/include/date/date.h"

#include "SpecUtils/CAMIO.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/SpecFile.h"


// Default byte arrays
namespace {
const std::array<byte_type, 0x060> fileHeader = {
        0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xA4, 0x00, 0x00, 0x00, 0x00,
        0x30, 0x00, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

// Note: acqpCommon/sampCommon used to live here as file-scope variables; they are now
// per-`CAMIO` members (see CAMIO.h) so state cannot leak between, or race across, writes.

const std::array<byte_type, 0x401> nuclCommon = {
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x75, 0x43, 0x69, 0x20,
         0x20, 0x20, 0x20, 0x20, 0x75, 0x43, 0x69, 0x20, 0x20, 0x20, 0x20, 0x20, 0x75, 0x43, 0x69, 0x20,
         0x20, 0x20, 0x20, 0x20, 0x75, 0x43, 0x69, 0x20, 0x20, 0x20, 0x20, 0x20, 0x75, 0x43, 0x69, 0x20,
         0x20, 0x20, 0x20, 0x20, 0x75, 0x43, 0x69, 0x20, 0x20, 0x20, 0x20, 0x20, 0x75, 0x43, 0x69, 0x20,
         0x20, 0x20, 0x20, 0x20, 0x75, 0x43, 0x69, 0x20, 0x20, 0x20, 0x20, 0x20, 0x63, 0x6D, 0x33, 0x20,
         0x20, 0x20, 0x20, 0x20, 0x63, 0x6D, 0x33, 0x20, 0x20, 0x20, 0x20, 0x20, 0x63, 0x6D, 0x33, 0x20,
         0x20, 0x20, 0x20, 0x20, 0x63, 0x6D, 0x33, 0x20, 0x20, 0x20, 0x20, 0x20, 0x63, 0x6D, 0x33, 0x20,
         0x20, 0x20, 0x20, 0x20, 0x63, 0x6D, 0x33, 0x20, 0x20, 0x20, 0x20, 0x20, 0x63, 0x6D, 0x33, 0x20,
         0x20, 0x20, 0x20, 0x20, 0x63, 0x6D, 0x33, 0x20, 0x20, 0x20, 0x20, 0x20, 0x80, 0x40, 0x00, 0x00,
         0x80, 0x40, 0x00, 0x00, 0x80, 0x40, 0x00, 0x00, 0x80, 0x40, 0x00, 0x00, 0x80, 0x40, 0x00, 0x00,
         0x80, 0x40, 0x00, 0x00, 0x80, 0x40, 0x00, 0x00, 0x80, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x75, 0x43, 0x69, 0x20, 0x20, 0x20, 0x20, 0x20, 0x63, 0x6D, 0x33, 0x20,
         0x20, 0x20, 0x20, 0x20, 0x80, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x22, 0x22, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x22, 0x22, 0x20, 0x20, 0x20, 0x20,
         0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00
    };

const std::array<byte_type, 0x018> nlineCommon = {
        0x6B, 0x65, 0x56, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x80, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

const std::array<byte_type, 0x7D0> procCommon = { 0 }; // Initialize with zeros


// Helper function to convert half-life units
/** Returns how many seconds one of the given CAM half-life unit is.

 Genie pads this field out with spaces (real files give "Y ", "D ", "H ", "S "), and the case is
 not consistent, so the comparison is done on an upper-cased copy truncated at the first space.
 Reader and writer share this so their unit tables cannot drift apart - they did, and the writer's
 untrimmed comparison rejected every nuclide the reader produces.

 Throws if the unit isnt one of y, d, h, m or s.
 */
static double half_life_unit_to_seconds(const std::string& unit_str) {
    std::string unit = unit_str;
    std::transform(unit.begin(), unit.end(), unit.begin(), ::toupper);
    unit = unit.substr(0, unit.find_first_of(' '));

    if (unit == "Y") return 31557600.0;  //Julian year = 365.25 days
    if (unit == "D") return 86400.0;
    if (unit == "H") return 3600.0;
    if (unit == "M") return 60.0;
    if (unit == "S") return 1.0;

    throw std::runtime_error("Half Life Unit " + unit + " not recognized");
}

static void ConvertHalfLife(CAMInputOutput::Nuclide& nuc) {
    // The file stores seconds; `Nuclide::HalfLife` is in `HalfLifeUnit`.
    const double seconds_per_unit = half_life_unit_to_seconds(nuc.HalfLifeUnit);
    nuc.HalfLife /= seconds_per_unit;
    nuc.HalfLifeUncertainty /= seconds_per_unit;
}

static std::vector<std::string> DecomposeIsotopeName(const std::string& name)
{
    std::regex pattern(R"(^([A-Za-z]+)-?(\d+)([A-Za-z]*)$)");
    std::smatch match;
    bool rmatch = std::regex_match(name, match, pattern);
    if (!rmatch)
    {
        throw std::runtime_error("Could not determine nuclude atomic number or element symbol for: " + name);
    }

    std::vector<std::string> result(match.begin() + 1, match.end());
    return result;
}


template< typename T > std::array< byte_type, sizeof(T) >  to_bytes(const T& object)
{
    std::array< byte_type, sizeof(T) > bytes;

    const byte_type* begin = reinterpret_cast<const byte_type*>(std::addressof(object));
    const byte_type* end = begin + sizeof(T);
    std::copy(begin, end, std::begin(bytes));

    return bytes;
}

enum class cam_type
{
    cam_float,     //any float
    cam_double,    //any double
    cam_byte,      //a byte
    cam_word,      //int16
    cam_longword,  //int
    cam_quadword,  //int64
    cam_datetime,   //date time
    cam_duration,   //time duration
    cam_string,
};

// Helper function to validate that we can safely read 'size' bytes starting at 'pos' from 'data'
static void validate_bounds( const std::vector<uint8_t>& data, const size_t pos, const size_t size, const char* context )
{
  if( pos > data.size() )
    throw std::out_of_range( std::string(context) + ": position " + std::to_string(pos) + " exceeds data size " + std::to_string(data.size()) );

  if( size > (data.size() - pos) )
    throw std::out_of_range( std::string(context) + ": attempting to read " + std::to_string(size) + " bytes at position " + std::to_string(pos) + " but only " + std::to_string(data.size() - pos) + " bytes available" );
}

// Convert data to CAM data formats

template <class T>
// IEEE-754 variables to CAM float (PDP-11)
static std::array< byte_type, sizeof(int32_t) > convert_to_CAM_float(const T& input)
{

    //pdp-11 is a wordswaped float/4
    float temp_f = static_cast<float>(input * 4);
    const auto temp = to_bytes(temp_f);
    const size_t word_size = 2;
    std::array< byte_type, sizeof(int32_t) > output = { 0x00 };
    //perform a word swap
    for (size_t i = 0; i < word_size; i++)
    {
        output[i] = temp[i + word_size];
        output[i + word_size] = temp[i];
    }
    return output;
}

template <class T>
// IEEE variables to CAM double (PDP-11)
static std::array< byte_type, sizeof(int64_t) > convert_to_CAM_double(const T& input)
{

    //pdp-11 is a word swaped Double/4
    double temp_d = static_cast<double>(input * 4.0);
    const auto temp = to_bytes(temp_d);
    const size_t word_size = 2;
    std::array< byte_type, sizeof(int64_t) > output = { 0x00 };
    //perform a word swap
    for (size_t i = 0; i < word_size; i++)
    {
        output[i + 3 * word_size] = temp[i];				//IEEE fourth is PDP-11 first
        output[i + 2 * word_size] = temp[i + word_size];  //IEEE third is PDP-11 second
        output[i + word_size] = temp[i + 2 * word_size];//IEEE second is PDP-11 third
        output[i] = temp[i + 3 * word_size];            //IEEE first is PDP-11 fouth
    }
    return output;
}

// time_point to CAM DateTime
static std::array< byte_type, sizeof(int64_t) > convert_to_CAM_datetime(const SpecUtils::time_point_t& date_time)
{
    //error checking
    if (SpecUtils::is_special(date_time))
        throw std::range_error("The input date time is not a valid date time");

    std::array< byte_type, sizeof(int64_t) > bytes = { 0x00 };
    // CAM datetimes are 64-bit counts of 100-nanosecond ticks since the modified
    //  Julian epoch (1858-11-17), which is 3506716800 seconds before the Unix epoch.
    const date::year_month_day epoch(date::year(1970), date::month(1u), date::day(1u));
    const date::sys_days epoch_days = epoch;
    assert(epoch_days.time_since_epoch().count() == 0); //true if using unix epoch, lets see on the various systems

    using cam_ticks_t = std::chrono::duration<int64_t, std::ratio<1, 10000000>>;
    const cam_ticks_t since_unix_epoch = std::chrono::duration_cast<cam_ticks_t>(date_time - epoch_days);
    const cam_ticks_t cam_epoch_offset{ 3506716800LL * 10000000LL };
    const int64_t cam_value = (since_unix_epoch + cam_epoch_offset).count();
    assert(cam_value >= 0);
    bytes = to_bytes(static_cast<uint64_t>(cam_value));
    return bytes;
}

// float sec to CAM duration
static std::array< byte_type, sizeof(int64_t) > convert_to_CAM_duration(const float& duration)
{
    std::array< byte_type, sizeof(int64_t) > bytes = { 0x00 };
    //duration in usec is larger than a int64: covert to years
    if ((static_cast<double>(duration) * 10000000.0) > static_cast<double>(INT64_MAX))
    {
        double t_duration = duration / 31557600;
        //duration in years is larger than an int32, divide by a million years
        if ((duration / 31557600.0) > static_cast<double>(INT32_MAX))
        {
            int32_t y_duration = SpecUtils::float_to_integral<int32_t>(t_duration / 1e6);
            const auto y_bytes = to_bytes(y_duration);
            std::copy(begin(y_bytes), end(y_bytes), begin(bytes));
            //set the flags
            bytes[7] = 0x80;
            bytes[4] = 0x01;
            return bytes;

        }
        //duration can be represented in years
        else
        {
            int32_t y_duration = static_cast<int32_t>(t_duration);
            const auto y_bytes = to_bytes(y_duration);
            std::copy(begin(y_bytes), end(y_bytes), begin(bytes));
            //set the flag
            bytes[7] = 0x80;
            return bytes;
        }
    }
    //duration is able to be represented in usec
    else
    {
        //cam time span is in usec and a negatve int64
        int64_t t_duration = static_cast<int64_t>((double)duration * -10000000);
        bytes = to_bytes(t_duration);
        return bytes;
    }

}

// CAM double to double
static double convert_from_CAM_double(const std::vector<uint8_t>& data, size_t pos)
{
    validate_bounds( data, pos, sizeof(double_t), "convert_from_CAM_double" );

    const size_t word_size = 2;
    std::array<uint8_t, sizeof(double_t)> temp = { 0x00 };

    // Perform word swap
    for (size_t i = 0; i < word_size; i++)
    {
        size_t j = i + pos;
        temp[i] = data[j + 3 * word_size];             // PDP-11 fourth is IEEE first
        temp[i + word_size] = data[j + 2 * word_size]; // PDP-11 third is IEEE second
        temp[i + 2 * word_size] = data[j + word_size]; // PDP-11 second is IEEE third
        temp[i + 3 * word_size] = data[j];             // PDP-11 first is IEEE forth
    }

    // Convert bytes back to double
    double temp_d;
    std::memcpy(&temp_d, temp.data(), sizeof(double_t)); // Safely copy the bytes into a double

    // scale to the double value
    return temp_d / 4.0;
}

// CAM float to float
static float convert_from_CAM_float(const std::vector<uint8_t>& data, size_t pos) {
    validate_bounds( data, pos, 4, "convert_from_CAM_float" );

    uint8_t word1[2], word2[2];

    std::memcpy(word1, &data[pos + 0x2], sizeof(word1));
    std::memcpy(word2, &data[pos], sizeof(word2));

    uint8_t bytearr[4];
    // Copy the words from the data array
    // Assuming the input data is in a format that needs to be swapped
    std::memcpy(bytearr, word1, sizeof(word1)); // Copy word1 to the beginning
    std::memcpy(bytearr + 2, word2, sizeof(word2)); // Copy word2 to the end

    float val;
    std::memcpy(&val, bytearr, sizeof(float));

    return val / 4;
}

//CAM DateTime to time_point
static SpecUtils::time_point_t convert_from_CAM_datetime(const std::vector<uint8_t>& data, size_t pos)
{
    validate_bounds( data, pos, sizeof(uint64_t), "convert_from_CAM_datetime" );

    uint64_t time_raw;
    std::memcpy(&time_raw, &data[pos], sizeof(uint64_t));

    if (!time_raw)
        return SpecUtils::time_point_t{};

    const date::sys_days epoch_days = date::year_month_day(date::year(1970), date::month(1u), date::day(1u));
    SpecUtils::time_point_t answer{ epoch_days };

    // CAM datetimes are 64-bit counts of 100-nanosecond ticks since the modified
    //  Julian epoch (1858-11-17), which is 3506716800 seconds before the Unix epoch.
    using cam_ticks_t = std::chrono::duration<int64_t, std::ratio<1, 10000000>>;
    const cam_ticks_t total_ticks{ static_cast<int64_t>(time_raw) };
    const std::chrono::seconds whole_secs = date::floor<std::chrono::seconds>(total_ticks);
    const cam_ticks_t sub_second_ticks = total_ticks - whole_secs;

    answer += (whole_secs - std::chrono::seconds(3506716800LL));
    answer += std::chrono::duration_cast<std::chrono::microseconds>(sub_second_ticks);

    return answer;
}//convert_from_CAM_datetime(...)

// CAM duration to float sec
static float convert_from_CAM_duration(std::vector<uint8_t>& data, size_t pos)
{
    validate_bounds( data, pos, 8, "convert_from_CAM_duration" );

    double span;
    //the duration is in usec
    if (data[pos + 7] != 0x80) {
        int64_t value;
        std::memcpy(&value, &data[pos], sizeof(int64_t));
        //convert to seconds
        span = std::abs(static_cast<double>(value) / 10000000.0);
    }
    //duration is in years
    else {
        int32_t value;
        std::memcpy(&value, &data[pos], sizeof(int32_t));
        //if the flag is set, duration is in millions of years
        span = data[pos + 4] == 0x01 ? value * 1e6 : value;
        //convert to seconds (Julian year = 365.25 days)
        span *= 31557600.0;
    }

    return span;
}

//enter the input to the cam desition vector of bytes at the location, with a given datatype
template<typename T, typename = typename std::enable_if<std::is_arithmetic<T>::value, T>::type>
static void enter_CAM_value(const T& input, std::vector<byte_type>& destination, 
    const size_t& location, const cam_type& type)
{
    switch (type) {
    case cam_type::cam_float:
    {
        const auto bytes = convert_to_CAM_float(input);

        if ((std::begin(destination) + location + (std::end(bytes) - std::begin(bytes))) > std::end(destination))
            throw std::runtime_error("enter_CAM_value(cam_float) invalid write location");

        std::copy(std::begin(bytes), std::end(bytes), destination.begin() + location);
    }
    break;
    case cam_type::cam_double:
    {
        const auto bytes = convert_to_CAM_double(input);

        if ((std::begin(destination) + location + (std::end(bytes) - std::begin(bytes))) > std::end(destination))
            throw std::runtime_error("enter_CAM_value(cam_double) invalid write location");

        std::copy(std::begin(bytes), std::end(bytes), destination.begin() + location);
    }
    break;
    case cam_type::cam_duration:
    {
        const auto bytes = convert_to_CAM_duration(input);

        if ((std::begin(destination) + location + (std::end(bytes) - std::begin(bytes))) > std::end(destination))
            throw std::runtime_error("enter_CAM_value(cam_duration) invalid write location");

        std::copy(std::begin(bytes), std::end(bytes), destination.begin() + location);
    }
    break;
    case cam_type::cam_quadword:
    {
        int64_t t_quadword = static_cast<int64_t>(input);
        const auto bytes = to_bytes(t_quadword);

        if ((std::begin(destination) + location + (std::end(bytes) - std::begin(bytes))) > std::end(destination))
            throw std::runtime_error("enter_CAM_value(cam_quadword) invalid write location");

        std::copy(std::begin(bytes), std::end(bytes), destination.begin() + location);
    }
    break;
    case cam_type::cam_longword:
    {
        // TODO: it appears we actually want to use a uint32_t here, and not a int32_t, but because of the static_cast here, things work out, but if we are to "clamp" values, then we need to switch to using unsigned integers
        int32_t t_longword = static_cast<int32_t>(input);

        const auto bytes = to_bytes(t_longword);

        if ((std::begin(destination) + location + (std::end(bytes) - std::begin(bytes))) > std::end(destination))
            throw std::runtime_error("enter_CAM_value(cam_longword) invalid write location");

        std::copy(std::begin(bytes), std::end(bytes), destination.begin() + location);
    }
    break;
    case cam_type::cam_word:
    {
        int16_t t_word = static_cast<int16_t>(input);
        const auto bytes = to_bytes(t_word);

        if ((std::begin(destination) + location + (std::end(bytes) - std::begin(bytes))) > std::end(destination))
            throw std::runtime_error("enter_CAM_value(cam_word) invalid write location");

        std::copy(std::begin(bytes), std::end(bytes), destination.begin() + location);
    }
    break;
    case cam_type::cam_byte:
    {
        byte_type t_byte = static_cast<byte_type>(input);
        destination.at(location) = t_byte;
        //const byte_type* begin = reinterpret_cast<const byte_type*>(std::addressof(t_byte));
        //const byte_type* end = begin + sizeof(byte_type);
        //std::copy(begin, end, destination.begin() + location);
        break;
    }
    default:
        std::string message = "error - Invalid converstion from: ";
        message.append(typeid(T).name());
        message.append(" to athermetic type");

        throw std::invalid_argument(message);
        break;
    }//end switch
}
// enter the input to the cam desition vector of bytes at the location, with a given datatype
static void enter_CAM_value(const SpecUtils::time_point_t& input, std::vector<byte_type>& destination, 
    const size_t& location, const cam_type& type = cam_type::cam_datetime)
{
    if (type != cam_type::cam_datetime)
    {
        throw std::invalid_argument("error - Invalid conversion from time_point");
    }

    const auto bytes = convert_to_CAM_datetime(input);

    if ((std::begin(destination) + location + (std::end(bytes) - std::begin(bytes))) > std::end(destination))
        throw std::runtime_error("enter_CAM_value(ptime) invalid write location");

    std::copy(begin(bytes), end(bytes), destination.begin() + location);
}
// enter the input to the cam desition vector of bytes at the location, with a given datatype
static void enter_CAM_value(const std::string& input, std::vector<byte_type>& destination, 
    const size_t& location, const cam_type& type = cam_type::cam_string)
{
    if (type != cam_type::cam_string)
    {
        throw std::invalid_argument("error - Invalid converstion from: char*[]");
    }

    if ((std::begin(destination) + location + (std::end(input) - std::begin(input))) > std::end(destination))
        throw std::runtime_error("enter_CAM_value(string) invalid write location");

    std::copy(input.begin(), input.end(), destination.begin() + location);
}

}


namespace CAMInputOutput {

// LineComparer sort by energy
bool LineComparer::operator()(const std::vector<uint8_t>& x, const std::vector<uint8_t>& y) const
    {
        return convert_from_CAM_float(x, static_cast<size_t>(CAMIO::LineParameterLocation::Energy)) <
            convert_from_CAM_float(y, static_cast<size_t>(CAMIO::LineParameterLocation::Energy));
    }

// NuclideComparer sort by atomic number A then alphabetically, then metastable states 
bool NuclideComparer::operator()(const std::vector<uint8_t>& x, const std::vector<uint8_t>& y) const
    {
        ////get the nuclide name
        //std::string x_name, y_name;
        //char nameBuf[9] = { 0 };
        //std::memcpy(nameBuf, &x[CAMIO::NuclideParameterLocation::Name], 8);
        //x_name = nameBuf;
        //std::memcpy(nameBuf, &y[CAMIO::NuclideParameterLocation::Name], 8);
        //y_name = nameBuf;


        //// TODO consider moving this to a struct for a Nuclide
        // Nuclides are sorted by A then Z the iosmeric state. 
        //// match the atomic number
        //std::regex pattern(R"((?<![a-zA-Z])(\d+)(?![a-zA-Z]))");
        //std::smatch a_x, a_y;
        //bool x_match = std::regex_search(x_name, a_x, pattern);
        //bool y_match = std::regex_search(y_name, a_y, pattern);
        //int ia_x = std::stoi(a_x.str());
        //int ia_y = std::stoi(a_y.str());
        //if (!y_match|| !x_match) 
        //{
        //    throw std::runtime_error("Could not determine nuclude atomic number: " + x_name+ " or " + y_name);
        //}
        //// Sort if the atomic numbers are the same sort the chemical symbol alphebetically  
        //if (ia_x == ia_y)
        //{
        //    std::smatch x_sy, y_sy;
        //    std::regex pattern(R"((?<=\d)([A-Za-z]+))");
        //    bool x_match = std::regex_search(x_name, x_sy, pattern);
        //    bool y_match = std::regex_search(y_name, y_sy, pattern);
        //    // TODO figure out how metastable states fit into this
        //    return x_sy.str() < y_sy.str();
        //}
        //return ia_x < ia_y;
        return false;
    }

// Struct implementations
Peak::Peak(float energy, float centrd, float centrdUnc, float fwhm, float lowTail,
    float area, float areaUnc, float continuum, float critialLevel,
    float cntRate, float cntRateUnc, int leftChan, int rightChan)
    : Energy(energy), Centroid(centrd), CentroidUncertainty(centrdUnc),
      FullWidthAtHalfMaximum(fwhm), LowTail(lowTail), Area(area),
      AreaUncertainty(areaUnc), Continuum(continuum), CriticalLevel(critialLevel),
      CountRate(cntRate), CountRateUncertainty(cntRateUnc), 
    LeftChannel(leftChan), RightChannel(rightChan) {}

Nuclide::Nuclide(const std::string& name, float halfLife, float halfLifeUnc,
    const std::string& halfLifeUnit, int nucNo, double activity = 0., 
    double activityUnc = 0., double mda = 0.)
    : Name(name), HalfLife(halfLife), HalfLifeUncertainty(halfLifeUnc),
    HalfLifeUnit(halfLifeUnit), Index(nucNo), Activity(activity), 
    ActivityUnc(activityUnc), MDA(mda)
{
    auto deName = DecomposeIsotopeName(name);
    AtomicNumber = std::stoi(deName[1]);
    ElementSymbol = deName[0];
    Metastable = deName[2];
}


CAMInputOutput::Line::Line(float energy, float energyUnc, float abundance, float abundanceUnc, 
    int nucNo, bool key, bool noWgtMean, 
    double lineAct, double lineActUnc, float lineEff, float lineEffUnc, double lineMDA)
    : Energy(energy), EnergyUncertainty(energyUnc), Abundance(abundance),
      AbundanceUncertainty(abundanceUnc), IsKeyLine(key), NuclideIndex(nucNo), 
    NoWeightMean(noWgtMean), LineActivity(lineAct), LineActivityUnceratinty(lineActUnc),
    LineEfficiency(lineEff),LineEfficiencyUncertainty(lineEffUnc), LineMDA(lineMDA)
{}

DetInfo::DetInfo(std::string type, std::string name, std::string serial_no, 
    std::string mca_type)
    : Type(type), Name(name), SerialNo(serial_no), MCAType(mca_type){}

// Out-of-line definitions for the `static constexpr` members that get ODR-used (bound to a
//  reference by std::min/std::max, or iterated over).  Only needed because SpecUtils defaults to
//  C++11; from C++17 these are implicitly inline and the definitions are redundant-but-harmless.
constexpr size_t CAMIO::sm_genie_peak_block_size;
constexpr size_t CAMIO::sm_genie_peak_time_offsets[2];
constexpr uint16_t CAMIO::sm_disp_rec_size;
constexpr size_t CAMIO::sm_genie_disp_block_size;
constexpr size_t CAMIO::sm_geom_max_fit_coeffs;
constexpr size_t CAMIO::sm_geom_fit_display_coeff_offsets[6];
constexpr float CAMIO::sm_geom_default_fit_ref_energy;

// CAMIO constructor
CAMIO::CAMIO()
  : acqpCommon( static_cast<size_t>(BlockSize::ACQP) - 0x30, 0 ),  // Initialize with zeros
    sampCommon( static_cast<size_t>(BlockSize::SAMP) - 0x30, 0 )
{
    // Initialize any necessary members
  //fwhmType = FwhmType::NotReadin;
  efficiencyModel = EfficiencyModel::NotReadin;
}

// read a file given the file name 

void CAMIO::ReadFile(const std::vector<byte_type>& fileData) {
    
    // set the readData pointer to point to the file data
    readData = std::make_shared<std::vector<byte_type>>(fileData);
    // Read the header
    blockAddresses = ReadHeader();

    if (blockAddresses.empty()) {
        throw std::runtime_error("The header format could not be read");
    }
}

// read the overall file header
std::multimap<CAMIO::CAMBlock, uint32_t> CAMIO::ReadHeader() {
    if (readData->empty()) {
        return std::multimap<CAMBlock, uint32_t>();
    }

    std::multimap<CAMBlock, uint32_t> blockInfo;

    // Loop through the header section file
    for (size_t i = 0; i < 28; i++) {
        size_t headOff = 0x70 + i * 0x30;

        if (headOff + 0x20 > readData->size()) {
            return std::multimap<CAMBlock, uint32_t>();
        }

        // Validate bounds before reading section ID
        validate_bounds( *readData, headOff, sizeof(uint32_t), "ReadHeader: reading section ID" );

        // Section ID
        uint32_t secId;
        std::memcpy(&secId, &(*readData)[headOff], sizeof(uint32_t));

        // Don't read a blank header (0x00000000)
        if (secId == 0x00000000) {
            continue;
        }

        // Validate bounds before reading block address
        validate_bounds( *readData, headOff + 0x0a, sizeof(uint32_t), "ReadHeader: reading block address" );

        // Get the addresses of the info
        uint32_t loc;
        std::memcpy(&loc, &(*readData)[headOff + 0x0a], sizeof(uint32_t));

        blockInfo.insert({static_cast<CAMBlock>(secId),  static_cast<uint32_t>(loc)});
    }

    return blockInfo;
}

// read a full cam file block
void CAMIO::ReadBlock(CAMBlock block) {
    if (blockAddresses.empty()) {
        throw std::runtime_error("The header format could not be read");
    }
    if (readData->empty()) {
        throw std::runtime_error("The file contains no data");
    }

    auto range = blockAddresses.equal_range(block);
    for (auto& it = range.first; it != range.second; ++it) {
        size_t pos = it->second;

        // Validate bounds before reading block ID
        validate_bounds( *readData, pos, sizeof(uint32_t), "ReadBlock: reading block ID" );

        // Verify block ID
        uint32_t blockId;
        std::memcpy(&blockId, &(*readData)[pos], sizeof(uint32_t));
        if (blockId != static_cast<uint32_t>(block)) {
            continue;
        }

        // Validate bounds before reading record count
        validate_bounds( *readData, pos + 0x1e, sizeof(uint16_t), "ReadBlock: reading record count" );

        // Read number of records
        uint16_t records;
        std::memcpy(&records, &(*readData)[pos + 0x1e], sizeof(uint16_t));

        // Process block based on type
        switch (block) {
            case CAMBlock::GEOM:
                ReadGeometryBlock(pos, records);
                break;
            case CAMBlock::NLINES:
                ReadLinesBlock(pos, records);
                break;
            case CAMBlock::NUCL:
                ReadNuclidesBlock(pos, records);
                break;
            case CAMBlock::PEAK:
                ReadPeaksBlock(pos, records);
                break;

          case CAMBlock::ACQP:
          case CAMBlock::SAMP:
          case CAMBlock::PROC:
          case CAMBlock::DISP:
          case CAMBlock::SPEC:
          case CAMBlock::K_EDGE_CONFIG:
            // Add other block types as needed
            break;
        }
    }
}

// Helper function to read a uint16_t from the data buffer
static uint16_t ReadUInt16(const std::vector<byte_type>& data, size_t offset) {
    if( (offset + sizeof(uint16_t)) > data.size() )
      throw std::out_of_range( "ReadUInt16: offset " + std::to_string(offset) + " out of range (data size: " + std::to_string(data.size()) + ")" );

    uint16_t value;
    std::memcpy(&value, &data[offset], sizeof(uint16_t));
    return value;
}

// Helper function to read a uint32_t from the data buffer
static uint32_t ReadUInt32(const std::vector<byte_type>& data, size_t offset) {
    if( offset + sizeof(uint32_t) > data.size() )
      throw std::out_of_range( "ReadUInt32: offset " + std::to_string(offset) + " out of range (data size: " + std::to_string(data.size()) + ")" );

    uint32_t value;
    std::memcpy(&value, &data[offset], sizeof(uint32_t));
    return value;
}

// read the geometry block
void CAMIO::ReadGeometryBlock(size_t pos, uint16_t records) {
    if( (pos + 0x28 + 2) > readData->size() )
        throw std::runtime_error( "Data smaller than geometry block" );

    // Get record offset and entry offset
    // When common flag is 0x700 or 0x300, the record offset should be treated as 0
    uint16_t commonFlag = ReadUInt16(*readData, pos + 0x04);
    uint16_t recOffset = (commonFlag == 0x700 || commonFlag == 0x300) ? 0 : ReadUInt16(*readData, pos + 0x22);
    uint16_t entOffset = ReadUInt16(*readData, pos + 0x28);
    uint16_t recSize = ReadUInt16(*readData, pos + 0x20);
    uint16_t entSize = ReadUInt16(*readData, pos + 0x2a);
    uint16_t headSize = ReadUInt16(*readData, pos + 0x10);

    const size_t pointSize = std::max({
        static_cast<size_t>(EfficiencyPointParameterLocation::Energy) + 4,
        static_cast<size_t>(EfficiencyPointParameterLocation::Efficiency) + 4,
        static_cast<size_t>(EfficiencyPointParameterLocation::EfficiencyUncertainty) + 4
    });
    static constexpr size_t maxEfficiencyPoints = 131072;

    if( records > 0 && recSize == 0 )
      throw std::runtime_error( "ReadGeometryBlock: record size cannot be zero" );
    if( records > 0 && entSize < pointSize )
      throw std::runtime_error( "ReadGeometryBlock: efficiency entry size is too small" );

    // Read the whole `sm_geom_model_name_size`-byte field, not just its first eight bytes: the
    //  longest name, "EMPIRICAL", is nine characters, and an eight-byte read could never match it.
    if( (pos + recOffset + 222 + sm_geom_model_name_size) <= readData->size() )
    {
      std::string type_str( sm_geom_model_name_size, '\0' );
      std::memcpy(&(type_str[0]), &(*readData)[pos + recOffset + 222], sm_geom_model_name_size);
      if( type_str.find( "SPLINE" ) != std::string::npos )
        efficiencyModel = EfficiencyModel::SPLINE;
      else if( type_str.find( "EMPIRICAL" ) != std::string::npos )
        efficiencyModel = EfficiencyModel::EMPIRICAL;
      else if( type_str.find( "AVERAGE" ) != std::string::npos )
        efficiencyModel = EfficiencyModel::AVERAGE;
      else if( type_str.find( "DUAL" ) != std::string::npos )
        efficiencyModel = EfficiencyModel::DUAL;
      else if( type_str.find( "LINEAR" ) != std::string::npos )
        efficiencyModel = EfficiencyModel::LINEAR;
      else if( type_str.find( "INTERPOL" ) != std::string::npos )
        efficiencyModel = EfficiencyModel::INTERPOL;
      else
        efficiencyModel = EfficiencyModel::Unknown;
    }else
    {
      efficiencyModel = EfficiencyModel::Unknown;
    }//if( (pos + recOffset + 222 + 8) <= readData->size() )

    // Loop through the records
    for (size_t i = 0; i < records; i++) {
        // Check for potential overflow in i * recSize calculation
        if( (recSize > 0) && (i > (std::numeric_limits<size_t>::max() / recSize)) )
          throw std::out_of_range( "ReadGeometryBlock: record index * recSize would overflow" );

        const size_t recordOffset = i * static_cast<size_t>(recSize);
        const size_t fixedOffset = static_cast<size_t>(headSize)
                                   + static_cast<size_t>(recOffset);
        if( fixedOffset > readData->size() || pos > readData->size() - fixedOffset
            || recordOffset > readData->size() - pos - fixedOffset )
          break;

        const size_t recordStart = pos + fixedOffset + recordOffset;
        const size_t recordBytes = std::min(static_cast<size_t>(recSize),
                                            readData->size() - recordStart);
        const size_t recordEnd = recordStart + recordBytes;
        if( entOffset > recordBytes )
          throw std::runtime_error( "ReadGeometryBlock: entry offset exceeds record size" );
        size_t loc = recordStart + static_cast<size_t>(entOffset);

        // Validate that loc is within bounds before entering loop
        if( loc >= readData->size() )
          break;

        // Loop through the entries
        // Each entry starts with a byte that matches the record number (1-based)
        while (loc < recordEnd && (*readData)[loc] == static_cast<uint8_t>(i + 1)) {
            if( pointSize > recordEnd - loc )
              throw std::runtime_error( "ReadGeometryBlock: truncated efficiency point" );
            validate_bounds( *readData, loc, pointSize, "ReadGeometryBlock: reading efficiency point" );

            if( efficiencyPoints.size() >= maxEfficiencyPoints )
              throw std::runtime_error( "ReadGeometryBlock: too many efficiency points" );

            EfficiencyPoint point{};
            point.Index = static_cast<int>(i);
            point.Energy = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(EfficiencyPointParameterLocation::Energy));
            point.Efficiency = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(EfficiencyPointParameterLocation::Efficiency));
            point.EfficiencyUncertainty = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(EfficiencyPointParameterLocation::EfficiencyUncertainty));

            efficiencyPoints.push_back(point);
            // `entSize >= pointSize` is checked above, and `loc < recordEnd <= readData->size()`,
            //  so this always advances and cannot wrap.
            loc += static_cast<size_t>(entSize);
        }
    }
}

// read the lines block
void CAMIO::ReadLinesBlock(size_t pos, uint16_t records) {
    if( (pos + 0x22 + 2) > readData->size() )
        throw std::runtime_error( "Data smaller than lines block" );

    // Get record offset and size
    uint16_t commonFlag = ReadUInt16(*readData, pos + 0x04);
    uint16_t recOffset = (commonFlag == 0x700 || commonFlag == 0x300) ? 0 : ReadUInt16(*readData, pos + 0x22);
    uint16_t recSize = ReadUInt16(*readData, pos + 0x20);
    uint16_t headSize = ReadUInt16(*readData, pos + 0x10);

    for (size_t i = 0; i < records; i++) {
        // Check for potential overflow in i * recSize calculation
        if( (recSize > 0) && (i > (std::numeric_limits<size_t>::max() / recSize)) )
          throw std::out_of_range( "ReadLinesBlock: record index * recSize would overflow" );

        // Use explicit size_t casts to avoid uint16_t overflow in offset calculation
        const size_t loc = pos + static_cast<size_t>(headSize) + static_cast<size_t>(recOffset) +
                           (i * static_cast<size_t>(recSize));

        // Validate bounds before copying
        validate_bounds( *readData, loc, recSize, "ReadLinesBlock: reading line record" );

        // Create a copy of the line record
        std::vector<uint8_t> line(recSize);
        std::copy(readData->begin() + loc, readData->begin() + loc + recSize, line.begin());

        // Insert in sorted order based on energy
        auto it = std::lower_bound(lines.begin(), lines.end(), line, LineComparer());
        lines.insert(it, line);
    }
}

// read the nuclides block
void CAMIO::ReadNuclidesBlock(size_t pos, uint16_t records) {
    if( (pos + 0x22 + 2) > readData->size() )
        throw std::runtime_error( "Data smaller than nuclides block" );

    // Get record offset
    // When common flag is 0x700 or 0x300, the record offset should be treated as 0
    uint16_t commonFlag = ReadUInt16(*readData, pos + 0x04);
    uint16_t recOffset = (commonFlag == 0x700 || commonFlag == 0x300) ? 0 : ReadUInt16(*readData, pos + 0x22);
    uint16_t recSize = ReadUInt16(*readData, pos + 0x20);
    uint16_t headSize = ReadUInt16(*readData, pos + 0x10);
    uint32_t lineListOffset = 0;

    for (size_t i = 0; i < records; i++) {
        // Check for potential overflow in i * recSize calculation
        if( (recSize > 0) && (i > (std::numeric_limits<size_t>::max() / recSize)) )
          throw std::out_of_range( "ReadNuclidesBlock: record index * recSize would overflow" );

        // Use explicit size_t casts to avoid uint16_t overflow in offset calculation
        const size_t loc = pos + static_cast<size_t>(headSize) + static_cast<size_t>(recOffset) +
                           static_cast<size_t>(lineListOffset) + (i * static_cast<size_t>(recSize));

        // Validate we can read the size field
        validate_bounds( *readData, loc, 2, "ReadNuclidesBlock: reading numLines size field" );

        if( (loc + 2) > readData->size() )
            throw std::runtime_error( "Data smaller than nuclide record" );

        // Calculate the size of this nuclide record including its lines
        // Validate that the size field is reasonable before subtracting
        const uint16_t sizeField = ReadUInt16(*readData, loc);
        const uint16_t minSize = static_cast<uint16_t>(recSize) + nuclide_line_size;
        if( sizeField < minSize )
          throw std::out_of_range( "ReadNuclidesBlock: invalid nuclide size field (too small)" );

        uint32_t numLines = ((sizeField - minSize) / nuclide_line_size) + 1;

        // Check for overflow in totalSize calculation
        if( numLines > ((std::numeric_limits<uint32_t>::max() - recSize) / 3) )
          throw std::out_of_range( "ReadNuclidesBlock: numLines would cause totalSize overflow" );

        uint32_t totalSize = static_cast<uint32_t>(recSize) + (numLines * 3);

        // Validate we can read the entire nuclide record
        validate_bounds( *readData, loc, totalSize, "ReadNuclidesBlock: reading nuclide record" );

        // Create a copy of the nuclide record with its lines - FIXED: was copying from nucs, should be readData
        std::vector<uint8_t> nuc(totalSize);
        std::copy(readData->begin() + loc, readData->begin() + loc + totalSize, nuc.begin());

        nucs.push_back(nuc);

        // Check for overflow in lineListOffset accumulation
        if( lineListOffset > (std::numeric_limits<uint32_t>::max() - totalSize) )
          throw std::out_of_range( "ReadNuclidesBlock: lineListOffset accumulation overflow" );

        lineListOffset += totalSize;

        // Also validate that the accumulated offset doesn't exceed data bounds
        const size_t nextLoc = pos + static_cast<size_t>(headSize) + static_cast<size_t>(recOffset) + static_cast<size_t>(lineListOffset);
        if( nextLoc > readData->size() )
          throw std::out_of_range( "ReadNuclidesBlock: accumulated lineListOffset exceeds data size" );
    }

}

// read the peaks block
void CAMIO::ReadPeaksBlock(size_t pos, uint16_t records) {
    if( (pos + 0x22 + 2) > readData->size() )
        throw std::runtime_error( "Data smaller than peaks block" );

    // Get record offset and size
    // When common flag is 0x700 or 0x300, the record offset should be treated as 0
    uint16_t commonFlag = ReadUInt16(*readData, pos + 0x04);
    uint16_t recOffset = (commonFlag == 0x700 || commonFlag == 0x300) ? 0 : ReadUInt16(*readData, pos + 0x22);
    uint16_t recSize = ReadUInt16(*readData, pos + 0x20);
    uint16_t headSize = ReadUInt16(*readData, pos + 0x10);

    std::vector<Peak> tempPeaks;

    for (size_t i = 0; i < records; i++) {
        // Check for potential overflow in i * recSize calculation
        if( (recSize > 0) && (i > (std::numeric_limits<size_t>::max() / recSize)) )
          throw std::out_of_range( "ReadPeaksBlock: record index * recSize would overflow" );

        // Use explicit size_t casts to avoid uint16_t overflow in offset calculation
        const size_t loc = pos + static_cast<size_t>(headSize) + static_cast<size_t>(recOffset) + 0x01 +
                           (i * static_cast<size_t>(recSize));

        // Validate we can read the entire peak record
        // Find the maximum offset we'll access (Width is the last field accessed)
        const size_t maxOffset = std::max({
            static_cast<size_t>(PeakParameterLocation::Energy) + 4,
            static_cast<size_t>(PeakParameterLocation::Centroid) + 4,
            static_cast<size_t>(PeakParameterLocation::CentroidUncertainty) + 4,
            static_cast<size_t>(PeakParameterLocation::Continuum) + 4,
            static_cast<size_t>(PeakParameterLocation::CriticalLevel) + 4,
            static_cast<size_t>(PeakParameterLocation::Area) + 4,
            static_cast<size_t>(PeakParameterLocation::AreaUncertainty) + 4,
            static_cast<size_t>(PeakParameterLocation::CountRate) + 4,
            static_cast<size_t>(PeakParameterLocation::CountRateUncertainty) + 4,
            static_cast<size_t>(PeakParameterLocation::FullWidthAtHalfMaximum) + 4,
            static_cast<size_t>(PeakParameterLocation::LowTail) + 4,
            static_cast<size_t>(PeakParameterLocation::LeftChannel) + 4,
            static_cast<size_t>(PeakParameterLocation::Width) + 2
        });
        validate_bounds( *readData, loc, maxOffset, "ReadPeaksBlock: reading peak record" );

        Peak peak{};
        peak.Energy = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::Energy));
        peak.Centroid = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::Centroid));
        peak.CentroidUncertainty = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::CentroidUncertainty));
        peak.Continuum = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::Continuum));
        peak.CriticalLevel = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::CriticalLevel));
        peak.Area = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::Area));
        peak.AreaUncertainty = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::AreaUncertainty));
        peak.CountRate = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::CountRate));
        peak.CountRateUncertainty = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::CountRateUncertainty));
        peak.FullWidthAtHalfMaximum = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::FullWidthAtHalfMaximum));
        peak.LowTail = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::LowTail));
        peak.LeftChannel = ReadUInt32(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::LeftChannel));
        //  `Width` is 16 bits - reading it as a longword would pull in `Continuum` (0x0C) as its
        //  high bytes.  `GetPeaks()` has always read it as 16 bits; this used to disagree.
        peak.RightChannel = peak.LeftChannel + ReadUInt16(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::Width)) - 1;

        tempPeaks.push_back(peak);
    }

    // Store the peaks in a member variable or process them as needed
    peaks = std::move(tempPeaks);
}

// get all the nuclide lines
std::vector<Line>& CAMIO::GetLines() {
    // Idempotent, like the other Get...() accessors - this used to append on every call, which
    //  also silently corrupted `GetNuclides()` (it indexes into `fileLines` by line number).
    if( !fileLines.empty() )
      return fileLines;

    auto range = blockAddresses.equal_range(CAMBlock::NLINES);
    if (range.first == range.second) {
        throw std::runtime_error("There is no nuclide line data in the loaded file");
    }

    for (auto& it = range.first; it != range.second; ++it) {
        size_t pos = it->second;

        if( (pos + 0x22 + 2) > readData->size() )
          throw std::runtime_error( "Data smaller than lines pos" );

        // Get record offset and size
        uint16_t commonFlag = ReadUInt16(*readData, pos + 0x04);
        uint16_t recOffset = (commonFlag == 0x700 || commonFlag == 0x300) ? 0 : ReadUInt16(*readData, pos + 0x22);
        uint16_t recSize = ReadUInt16(*readData, pos + 0x20);
        uint16_t numRec = ReadUInt16(*readData, pos + 0x1E);
        uint16_t headSize = ReadUInt16(*readData, pos + 0x10);

        for (size_t i = 0; i < numRec; i++) {
            // Check for potential overflow in i * recSize calculation
            if( (recSize > 0) && (i > (std::numeric_limits<size_t>::max() / recSize)) )
              throw std::out_of_range( "GetLines: record index * recSize would overflow" );

            // Use explicit size_t casts to avoid uint16_t overflow in offset calculation
            const size_t loc = pos + static_cast<size_t>(headSize) + static_cast<size_t>(recOffset) +
                               (i * static_cast<size_t>(recSize));

            // Validate bounds before accessing line data
            const size_t maxOffset = std::max({
                static_cast<size_t>(LineParameterLocation::Energy) + 4,
                static_cast<size_t>(LineParameterLocation::EnergyUncertainty) + 4,
                static_cast<size_t>(LineParameterLocation::Abundance) + 4,
                static_cast<size_t>(LineParameterLocation::AbundanceUncertainty) + 4,
                static_cast<size_t>(LineParameterLocation::IsKeyLine) + 1,
                static_cast<size_t>(LineParameterLocation::NuclideIndex) + 1,
                static_cast<size_t>(LineParameterLocation::NoWeightMean) + 1
            });
            validate_bounds( *readData, loc, maxOffset, "GetLines: reading line data" );

            Line line{};
            line.Energy = convert_from_CAM_float(*readData, loc + static_cast<size_t>(LineParameterLocation::Energy));
            line.EnergyUncertainty = convert_from_CAM_float(*readData, loc + static_cast<size_t>(LineParameterLocation::EnergyUncertainty));
            line.Abundance = convert_from_CAM_float(*readData, loc + static_cast<size_t>(LineParameterLocation::Abundance));
            line.AbundanceUncertainty = convert_from_CAM_float(*readData, loc + static_cast<size_t>(LineParameterLocation::AbundanceUncertainty));
            line.IsKeyLine = (*readData)[loc + static_cast<size_t>(LineParameterLocation::IsKeyLine)] == 0x04;
            line.NuclideIndex = (*readData)[loc + static_cast<size_t>(LineParameterLocation::NuclideIndex)];
            line.NoWeightMean = (*readData)[loc + static_cast<size_t>(LineParameterLocation::NoWeightMean)] == 0x02;

            fileLines.push_back(line);
        }
    }

    // Sort lines by energy
    std::sort(fileLines.begin(), fileLines.end(),
              [](const Line& a, const Line& b) { return a.Energy < b.Energy; });

    return fileLines;
}

// get all the nuclides
std::vector<Nuclide>& CAMIO::GetNuclides() {
    auto range = blockAddresses.equal_range(CAMBlock::NUCL);
    if (range.first == range.second) {
        throw std::runtime_error("There is no nuclide data in the loaded file");
    }

    fileNuclides.clear();

    // Get lines if they don't exist
    if (fileLines.empty()) {
        GetLines();
    }

    if (fileLines.empty()) {
        throw std::runtime_error("There are no lines in the file");
    }

    for (auto& it = range.first; it != range.second; ++it) {
        size_t pos = it->second;

        if( (pos + 0x22 + 2) > readData->size() )
          throw std::runtime_error( "Data smaller than nuclides pos" );

        // When common flag is 0x700 or 0x300, the record offset should be treated as 0
        const uint16_t commonFlag = ReadUInt16(*readData, pos + 0x04);
        uint16_t recOffset = (commonFlag == 0x700 || commonFlag == 0x300) ? 0 : ReadUInt16(*readData, pos + 0x22);
        uint16_t recSize = ReadUInt16(*readData, pos + 0x20);
        uint16_t numRec = ReadUInt16(*readData, pos + 0x1E);
        uint16_t headSize = ReadUInt16(*readData, pos + 0x10);
        uint32_t lineListOffset = 0x0;
        uint16_t lineListLoc = recSize;

        for (size_t i = 0; i < numRec; i++) {
            // Check for potential overflow in i * recSize calculation
            if( (recSize > 0) && (i > (std::numeric_limits<size_t>::max() / recSize)) )
              throw std::out_of_range( "GetNuclides: record index * recSize would overflow" );

            // Use explicit size_t casts to avoid uint16_t overflow in offset calculation
            const size_t loc = pos + static_cast<size_t>(headSize) + static_cast<size_t>(recOffset) +
                               static_cast<size_t>(lineListOffset) + (i * static_cast<size_t>(recSize));

            // Validate we can read the nuclide size field (first 2 bytes)
            validate_bounds( *readData, loc, 2, "GetNuclides: reading nuclide size field" );

            // Calculate numLines with validation to prevent underflow
            const uint16_t sizeField = ReadUInt16(*readData, loc);
            const uint16_t minSize = static_cast<uint16_t>(recSize);
            if( sizeField < minSize )
              throw std::out_of_range( "GetNuclides: invalid nuclide size field (too small)" );

            uint32_t numLines = ((sizeField - minSize) / 0x03);

            // Validate we can read all nuclide data including activities and line list
            const size_t maxOffset = std::max({
                size_t(0x1b + 8),  // HalfLife location + size
                size_t(0x89 + 8),  // HalfLifeUncertainty location + size
                static_cast<size_t>(NuclideParameterLocation::Name) + size_t(8),
                static_cast<size_t>(NuclideParameterLocation::HalfLifeUnit) + size_t(3),
                static_cast<size_t>(NuclideParameterLocation::MeanActivity) + size_t(8),
                static_cast<size_t>(NuclideParameterLocation::MeanActivityUnceratinty) + size_t(8),
                static_cast<size_t>(NuclideParameterLocation::NuclideMDA) + size_t(8),
                size_t(lineListLoc) + size_t(0x01 + 2)  // line index location + size
            });
            validate_bounds( *readData, loc, maxOffset, "GetNuclides: reading nuclide data" );

            Nuclide nuc;
            nuc.HalfLife = convert_from_CAM_duration(*readData, loc + 0x1b);
            nuc.HalfLifeUncertainty = convert_from_CAM_duration(*readData, loc + static_cast<uint32_t>(NuclideParameterLocation::HalfLifeUncertainty) );

            // Read name (8 characters)
            char nameBuf[9] = {0};
            std::memcpy(nameBuf, &(*readData)[loc + NuclideParameterLocation::Name], 8);
            nameBuf[8] = '\0';
            nuc.Name = std::string(nameBuf);

            // Read half-life unit (2 characters)
            char unitBuf[4] = {0};
            std::memcpy(unitBuf, &(*readData)[loc + NuclideParameterLocation::HalfLifeUnit], 3);
            unitBuf[3] = '\0';
            nuc.HalfLifeUnit = std::string(unitBuf);

            // Convert half-life to appropriate units
            ConvertHalfLife(nuc);

            // Get first line index - validate it's within bounds
            size_t lineIndex = static_cast<size_t>(ReadUInt16(*readData, loc + lineListLoc + 0x01));
            if( (lineIndex == 0) || (lineIndex > fileLines.size()) )
              throw std::out_of_range( "GetNuclides: lineIndex " + std::to_string(lineIndex) + " is out of range (fileLines size: " + std::to_string(fileLines.size()) + ")" );

            nuc.Index = fileLines[lineIndex - 1].NuclideIndex;

            // Check for overflow in lineListOffset accumulation
            if( numLines > (std::numeric_limits<uint32_t>::max() / nuclide_line_size) )
              throw std::out_of_range( "GetNuclides: numLines * nuclide_line_size would overflow" );

            const uint32_t linesSize = numLines * nuclide_line_size;
            if( lineListOffset > (std::numeric_limits<uint32_t>::max() - linesSize) )
              throw std::out_of_range( "GetNuclides: lineListOffset accumulation overflow" );

            lineListOffset += linesSize;

            // Validate that the accumulated offset doesn't exceed data bounds
            const size_t nextLoc = pos + static_cast<size_t>(headSize) + static_cast<size_t>(recOffset) + static_cast<size_t>(lineListOffset);
            if( nextLoc > readData->size() )
              throw std::out_of_range( "GetNuclides: accumulated lineListOffset exceeds data size" );

            nuc.Activity = convert_from_CAM_double(*readData, loc + NuclideParameterLocation::MeanActivity);
            nuc.ActivityUnc = convert_from_CAM_double(*readData, loc + NuclideParameterLocation::MeanActivityUnceratinty);
            nuc.MDA = convert_from_CAM_double(*readData, loc + NuclideParameterLocation::NuclideMDA);

            fileNuclides.push_back(nuc);
        }
    }

    return fileNuclides;
}

// get the peaks
std::vector<Peak>& CAMIO::GetPeaks() {
    auto range = blockAddresses.equal_range(CAMBlock::PEAK);
    if (range.first == range.second) {
        throw std::runtime_error("There is no peak data in the loaded file");
    }

    // Clear first, like `GetNuclides()`: without this a second call appends a second copy of every
    //  peak - the same bug this class already had in `GetLines()`.
    filePeaks.clear();

    for (auto& it = range.first; it != range.second; ++it) {
        size_t pos = it->second;

        if( (pos + 0x22 + 2) > readData->size() )
          throw std::runtime_error( "Data smaller than peaks pos" );

        // When common flag is 0x700 or 0x300, the record offset should be treated as 0
        const uint16_t commonFlag = ReadUInt16(*readData, pos + 0x04);
        uint16_t recOffset = (commonFlag == 0x700 || commonFlag == 0x300) ? 0 : ReadUInt16(*readData, pos + 0x22);
        uint16_t recSize = ReadUInt16(*readData, pos + 0x20);
        uint16_t numRec = ReadUInt16(*readData, pos + 0x1E);
        uint16_t headSize = ReadUInt16(*readData, pos + 0x10);

        for (size_t i = 0; i < numRec; i++) {
            // Check for potential overflow in i * recSize calculation
            if( (recSize > 0) && (i > (std::numeric_limits<size_t>::max() / recSize)) )
              throw std::out_of_range( "GetPeaks: record index * recSize would overflow" );

            // Use explicit size_t casts to avoid uint16_t overflow in offset calculation
            const size_t loc = pos + static_cast<size_t>(headSize) + static_cast<size_t>(recOffset) + 0x01 +
                               (i * static_cast<size_t>(recSize));

            // Validate we can read the entire peak record
            const size_t maxOffset = std::max({
                static_cast<size_t>(PeakParameterLocation::Energy) + size_t(4),
                static_cast<size_t>(PeakParameterLocation::Centroid) + size_t(4),
                static_cast<size_t>(PeakParameterLocation::CentroidUncertainty) + size_t(4),
                static_cast<size_t>(PeakParameterLocation::Continuum) + size_t(4),
                static_cast<size_t>(PeakParameterLocation::CriticalLevel) + size_t(4),
                static_cast<size_t>(PeakParameterLocation::Area) + size_t(4),
                static_cast<size_t>(PeakParameterLocation::AreaUncertainty) + size_t(4),
                static_cast<size_t>(PeakParameterLocation::CountRate) + size_t(4),
                static_cast<size_t>(PeakParameterLocation::CountRateUncertainty) + size_t(4),
                static_cast<size_t>(PeakParameterLocation::FullWidthAtHalfMaximum) + size_t(4),
                static_cast<size_t>(PeakParameterLocation::LowTail) + size_t(4),
                static_cast<size_t>(PeakParameterLocation::LeftChannel) + size_t(4),
                static_cast<size_t>(PeakParameterLocation::Width) + size_t(2)
            });
            validate_bounds( *readData, loc, maxOffset, "GetPeaks: reading peak record" );

            Peak peak{};
            peak.Energy = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::Energy));
            peak.Centroid = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::Centroid));
            peak.CentroidUncertainty = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::CentroidUncertainty));
            peak.Continuum = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::Continuum));
            peak.CriticalLevel = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::CriticalLevel));
            peak.Area = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::Area));
            peak.AreaUncertainty = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::AreaUncertainty));
            peak.CountRate = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::CountRate));
            peak.CountRateUncertainty = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::CountRateUncertainty));
            peak.FullWidthAtHalfMaximum = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::FullWidthAtHalfMaximum));
            peak.LowTail = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::LowTail));
            peak.LeftChannel = ReadUInt32(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::LeftChannel));
            peak.RightChannel = peak.LeftChannel + ReadUInt16(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::Width)) - 1;

            filePeaks.push_back(peak);
        }
    }

    return filePeaks;
}

// get the spectrum
std::vector<uint32_t>& CAMIO::GetSpectrum() {
    auto range = blockAddresses.equal_range(CAMBlock::SPEC);
    if (range.first == range.second) {
        throw std::runtime_error("There is no spectral data in the loaded file");
    }

    for (auto& it = range.first; it != range.second; ++it) {
        size_t pos = it->second;

        if( (pos + 0x2A + 2) > readData->size() )
          throw std::runtime_error( "Data smaller than spectrum pos" );

        // Get number of channels
        uint16_t channels = ReadUInt16(*readData, pos + 0x2A);
        uint16_t headerOffset = ReadUInt16(*readData, pos + 0x10);
        uint16_t dataOffset = ReadUInt16(*readData, pos + 0x28);

        // Validate that we can read all the spectrum data
        const size_t dataStart = pos + dataOffset + headerOffset;
        const size_t totalDataSize = static_cast<size_t>(channels) * sizeof(uint32_t);

        validate_bounds( *readData, dataStart, totalDataSize, "GetSpectrum: reading spectrum data" );

        // Resize spectrum vector to accommodate all channels
        fileSpectrum.resize(channels);

        if( (pos + dataOffset + headerOffset + channels*4) > readData->size() )
          throw std::runtime_error( "Data smaller than spectrum data" );

        // Read channel data
        for (size_t i = 0; i < channels; i++) {
            uint32_t value;
            std::memcpy(&value, &(*readData)[pos + dataOffset + headerOffset + i * 4], sizeof(uint32_t));
            fileSpectrum[i] = value;
        }
    }

    return fileSpectrum;
}

std::string CAMIO::GetSampleTitle()
{
    auto range = blockAddresses.equal_range(CAMBlock::SAMP);
    if (range.first == range.second) {
        throw std::runtime_error("There is no sample data in the loaded file");
    }

    for (auto& it = range.first; it != range.second; ++it) {
        size_t pos = it->second;

        uint16_t headSize = ReadUInt16(*readData, pos + 0x10);

        // Validate we can read the sample title (64 bytes)
        validate_bounds( *readData, pos + headSize, 64, "GetSampleTitle: reading sample title" );

        char nameBuf[65] = { 0 };
        std::memcpy(nameBuf, &(*readData)[pos + headSize], sizeof(nameBuf)-1);
        nameBuf[64] = '\0';
        return std::string(nameBuf);
    }

    return std::string(); // Should never reach here
    
}

DetInfo& CAMIO::GetDetectorInfo()
{
    auto range = blockAddresses.equal_range(CAMBlock::ACQP);
    if (range.first == range.second) {
        throw std::runtime_error("There is no aqusition data in the loaded file");
    }

    for (auto& it = range.first; it != range.second; ++it) {
        size_t pos = it->second;

        // When common flag is 0x700 or 0x300, the record offset should be treated as 0
        const uint16_t commonFlag = ReadUInt16(*readData, pos + 0x04);
        uint16_t recOffset = (commonFlag == 0x700 || commonFlag == 0x300) ? 0 : ReadUInt16(*readData, pos + 0x22);
        uint16_t recSize = ReadUInt16(*readData, pos + 0x20);
        //uint16_t numRec = ReadUInt16(*readData, pos + 0x1E);
        uint16_t numRec = 1;
        uint16_t headSize = ReadUInt16(*readData, pos + 0x10);;

        for (size_t i = 0; i < numRec; i++) {
            // Check for potential overflow in i * recSize calculation
            if( (recSize > 0) && (i > (std::numeric_limits<size_t>::max() / recSize)) )
              throw std::out_of_range( "GetDetectorInfo: record index * recSize would overflow" );

            // Use explicit size_t casts to avoid uint16_t overflow in offset calculation
            const size_t loc = pos + static_cast<size_t>(headSize) + static_cast<size_t>(recOffset) +
                               (i * static_cast<size_t>(recSize));

            // Validate we can read all detector info fields
            const size_t maxOffset = std::max({
                size_t(0x2DC + 8),   // Type field
                size_t(0x9C + 24),   // MCAType field
                size_t(0x108 + 16),  // Name field
                size_t(0x1CB + 8)    // SerialNo field
            });
            validate_bounds( *readData, loc, maxOffset, "GetDetectorInfo: reading detector info fields" );

            // these are actually record parameters but we don't deal with multiple spectrum in a single file
            char type_buf[9] = { 0 };
            std::memcpy(type_buf, &(*readData)[loc + 0x2DC], sizeof(type_buf) - 1);
            type_buf[8] = '\0';
            
            char mca_type_buf[25] = { 0 };
            std::memcpy(mca_type_buf, &(*readData)[loc + 0x9C], sizeof(mca_type_buf) - 1);
            mca_type_buf[24] = '\0';
            
            char name_buf[17] = { 0 };
            std::memcpy(name_buf, &(*readData)[loc + 0x108], sizeof(name_buf) - 1);
            name_buf[16] = '\0';
            
            char sn_buf[0x9] = { 0 };
            std::memcpy(sn_buf, &(*readData)[loc + 0x1CB], sizeof(sn_buf) - 1);
            sn_buf[8] = '\0';
            
            // Only update det_info if we found non-empty data
            // (prevents later empty blocks from overwriting valid info)
            const std::string type_str(type_buf);
            const std::string mca_type_str(mca_type_buf);
            const std::string name_str(name_buf);
            const std::string sn_str(sn_buf);
            
            const bool hasData = !type_str.empty() || !mca_type_str.empty() || 
                                 !name_str.empty() || !sn_str.empty();
            
            if( hasData )
            {
                det_info.Type = type_str;
                det_info.MCAType = mca_type_str;
                det_info.Name = name_str;
                det_info.SerialNo = sn_str;
                return det_info;
            }
        }
    }

    return det_info;
}

// get the sampling time
SpecUtils::time_point_t CAMIO::GetSampleTime() {
    auto range = blockAddresses.equal_range(CAMBlock::SAMP);
    if (range.first == range.second) {
        throw std::runtime_error("There is no sample data in the loaded file");
    }

    for (auto& it = range.first; it != range.second; ++it) {
        size_t pos = it->second;
        uint16_t headSize = ReadUInt16(*readData, pos + 0x10);

        // Validate bounds before calling convert_from_CAM_datetime (which also validates, but this is clearer)
        validate_bounds( *readData, pos + headSize + 0xb4, sizeof(uint64_t), "GetSampleTime: reading sample time" );

        return convert_from_CAM_datetime(*readData, pos + headSize + 0xb4);
    }

    return SpecUtils::time_point_t{}; // Should never reach here
}

// get the aqusition start time
SpecUtils::time_point_t CAMIO::GetAquisitionTime() {
    auto range = blockAddresses.equal_range(CAMBlock::ACQP);
    if (range.first == range.second) {
        throw std::runtime_error("There is no temporal data in the loaded file");
    }

    SpecUtils::time_point_t result{};

    for (auto& it = range.first; it != range.second; ++it) {
        size_t pos = it->second;
      
        uint16_t headSize = ReadUInt16(*readData, pos + 0x10);
        uint16_t timeOffset = ReadUInt16(*readData, pos + 0x24);

        // Check for potential overflow in offset calculation (timeOffset is file-controlled)
        const size_t timePos = pos + static_cast<size_t>(headSize) + static_cast<size_t>(timeOffset) + 0x01;

        const SpecUtils::time_point_t blockTime = convert_from_CAM_datetime(*readData, timePos);
        
        // Only update if we found a valid (non-special) time
        if( !SpecUtils::is_special(blockTime) )
          return blockTime;
    }

    return result;
}

// gets the live time in float seconds
float CAMIO::GetLiveTime() {
    auto range = blockAddresses.equal_range(CAMBlock::ACQP);
    if (range.first == range.second) {
        throw std::runtime_error("There is no temporal data in the loaded file");
    }

    for (auto& it = range.first; it != range.second; ++it) {
        size_t pos = it->second;
        uint16_t timeOffset = ReadUInt16(*readData, pos + 0x24);

        // Check for overflow in offset calculation (timeOffset is file-controlled)
        const size_t liveTimePos = pos + 0x30 + static_cast<size_t>(timeOffset) + 0x11;

        const float liveTime = convert_from_CAM_duration(*readData, liveTimePos);
        
        // Only return if we found a valid (non-zero) live time
        if( liveTime > 0.0f )
          return liveTime;
    }

    return 0.0f;
}

// gets the real time in float seconds
float CAMIO::GetRealTime() {
    auto range = blockAddresses.equal_range(CAMBlock::ACQP);
    if (range.first == range.second) {
        throw std::runtime_error("There is no temporal data in the loaded file");
    }

    for (auto& it = range.first; it != range.second; ++it) {
        size_t pos = it->second;
        uint16_t timeOffset = ReadUInt16(*readData, pos + 0x24);

        // Check for overflow in offset calculation (timeOffset is file-controlled)
        const size_t realTimePos = pos + 0x30 + static_cast<size_t>(timeOffset) + 0x09;

        const float realTime = convert_from_CAM_duration(*readData, realTimePos);
        
        // Only return if we found a valid (non-zero) real time
        if( realTime > 0.0f )
          return realTime;
    }

    return 0.0f;
}

// get the shape calibration coefficients
std::vector<float>& CAMIO::GetShapeCalibration() {
  if( !fileShapeCal.empty() )
    return fileShapeCal;

    auto range = blockAddresses.equal_range(CAMBlock::ACQP);
    if (range.first == range.second) {
        throw std::runtime_error("There is no calibration data in the loaded file");
    }


    fileShapeCal.resize(4);

    for (auto& it = range.first; it != range.second; ++it) {
        size_t pos = it->second;
        
        // Check common flag - when 0x700 or 0x300, the record offset should be 0
        const uint16_t commonFlag = ReadUInt16(*readData, pos + 0x04);
        const uint16_t recOffset = (commonFlag == 0x700 || commonFlag == 0x300) 
                                   ? 0 : ReadUInt16(*readData, pos + 0x22);
        
        // Use size_t to avoid uint16_t overflow in offset calculation
        const size_t shapeCalOffset = 0x30 + static_cast<size_t>(recOffset) + 0xDC;

      //CONSTANT or SQRT
      //std::string type_str( shapeCalOffset + 100 + 1, '\0');
      //std::memcpy(&(type_str[0]), &(*readData)[pos], shapeCalOffset + 100);
      //std::cout << "FWHM_type: '" << type_str << "'" << std::endl;

        // Validate bounds before reading calibration coefficients
        const size_t calibStart = pos + shapeCalOffset;
        const size_t calibSize = fileShapeCal.size() * 4;
        validate_bounds( *readData, calibStart, calibSize, "GetShapeCalibration: reading coefficients" );

        // Only update if we find non-zero calibration (first block with data wins)
        bool allZeros = true;
        std::vector<float> tempCal(fileShapeCal.size());
        for (size_t i = 0; i < tempCal.size(); i++) {
            tempCal[i] = convert_from_CAM_float(*readData, pos + shapeCalOffset + i * 4);
            if (tempCal[i] != 0.0f)
                allZeros = false;
        }
        
        // Only use this block's calibration if it has non-zero values
        // (prevents later empty blocks from overwriting valid calibration)
        if (!allZeros) {
            fileShapeCal = std::move(tempCal);
        }
    }

    return fileShapeCal;
}

// get the energy calibration coefficients
std::vector<float>& CAMIO::GetEnergyCalibration() {
  if( !fileEneCal.empty() )
    return fileEneCal;

    auto range = blockAddresses.equal_range(CAMBlock::ACQP);
    if (range.first == range.second) {
        throw std::runtime_error("There is no calibration data in the loaded file");
    }

    fileEneCal.resize(max_energy_cal_coefs);

    for (auto& it = range.first; it != range.second; ++it) {
        size_t pos = it->second;
        
        // Check common flag - when 0x700 or 0x300, the record offset should be 0
        const uint16_t commonFlag = ReadUInt16(*readData, pos + 0x04);
        const uint16_t recOffset = (commonFlag == 0x700 || commonFlag == 0x300) 
                                   ? 0 : ReadUInt16(*readData, pos + 0x22);
        
        // Use size_t to avoid uint16_t overflow in offset calculation
        const size_t eCalOffset = 0x30 + static_cast<size_t>(recOffset) + 0x44;

        // Validate we can read all energy calibration coefficients (4 floats = 16 bytes)
        // eCalOffset is partially file-controlled via ReadUInt16
        const size_t calibStart = pos + eCalOffset;
        const size_t calibSize = fileEneCal.size() * 4;
        validate_bounds( *readData, calibStart, calibSize, "GetEnergyCalibration: reading calibration coefficients" );

        // Only update if we find non-zero calibration (first block with data wins)
        bool allZeros = true;
        std::vector<float> tempCal(fileEneCal.size());
        for (size_t i = 0; i < tempCal.size(); i++) {
            tempCal[i] = convert_from_CAM_float(*readData, pos + eCalOffset + i * 4);
            if (tempCal[i] != 0.0f)
                allZeros = false;
        }
        
        // Only use this block's calibration if it has non-zero values
        // (prevents later empty blocks from overwriting valid calibration)
        if (!allZeros) {
            fileEneCal = std::move(tempCal);
        }
    }

    return fileEneCal;
}

// create a file from added data
std::vector<byte_type>& CAMIO::CreateCAMFile() {

    for (size_t i = 0; i < writeNuclides.size(); i++)
    {
        AddNuclide(writeNuclides[i]);
    }

    //if (lines.empty() || nucs.empty()) {
    //    throw std::runtime_error("Both Lines and Nuclides must not be null");
    //}

    AssignKeyLines();
    
    std::vector<std::vector<uint8_t>> blockList;

    size_t loc = header_size;
    // Add ACQP and PROC blocks
    blockList.push_back(GenerateBlock(CAMBlock::ACQP, loc));
    loc += static_cast<size_t>(BlockSize::ACQP);
    if (sampBlock) {

        blockList.push_back(GenerateBlock(CAMBlock::SAMP, loc));
        loc += static_cast<size_t>(BlockSize::SAMP);
    }

    blockList.push_back(GenerateBlock(CAMBlock::PROC, loc));
    loc += static_cast<size_t>(BlockSize::PROC);


    if (specBlock)
    {
        auto specBlockData = GenerateBlock(CAMBlock::SPEC, loc);
        blockList.push_back(specBlockData);
        loc += specBlockData.size();
    }

    // A fitted curve with no points is what cs137.CNF has (a model name and nothing else), so it
    //  is a legitimate block to write - and dropping it silently, as this used to when only
    //  `AddEfficiencyFit(...)` had been called, loses data the caller asked for without saying so.
    if (!writeEfficiencyPoints.empty() || !writeEfficiencyFitCoeffs.empty())
    {
        auto geomBlockData = GenerateGeometryBlock(loc);
        blockList.push_back(geomBlockData);
        loc += geomBlockData.size();
    }

    if (!writePeaks.empty())
    {
        auto peakBlockData = GeneratePeakBlock(loc);
        blockList.push_back(peakBlockData);
        loc += peakBlockData.size();

        // A PEAK block is only meaningful alongside the DISP block naming its ROIs.
        auto dispBlockData = GenerateDispBlock(loc);
        blockList.push_back(dispBlockData);
        loc += dispBlockData.size();
    }

    // NLINES/NUCL records can span several blocks, and each block in such a chain points at the
    //  one that continues it via the single byte at block offset 0x0E, or 0 when the chain ends.
    //
    //  The value is the block's index in the file's block directory (the 0x30-byte entries
    //  starting at file offset 0x70) of the *next* chain member, PLUS 2.  That `+ 2` was derived
    //  from the only Genie-produced files available with multi-block chains, which agree
    //  unanimously:
    //     ExampleMultiNuclide.nlb   slot 2 (NLINES) -> next slot 3, byte 0x0E = 5
    //     Q_C_UCRM125A_OSL_K_...CNF slot 0 (ACQP)   -> next slot 4, byte 0x0E = 6
    //                               slot 4 (ACQP)   -> next slot 5, byte 0x0E = 7
    //                               slot 5 (ACQP)   -> next slot 6, byte 0x0E = 8
    //  In those same files the byte at 0x0F is a block-type flag (0x28 on NUCL/NLINES, 0x00 on
    //  ACQP/SAMP/PROC/SPEC/GEOM), NOT the high byte of a 16-bit field - so only 0x0E may be
    //  written here, and the index must fit in a byte.
    //
    //  The index has to be taken from `blockList` itself, since how many blocks precede the chain
    //  varies with which of the optional SAMP/SPEC/GEOM blocks are present.  We also cannot know
    //  whether a block ends the chain until after it has been generated (only then do we know how
    //  many records it actually held), so the link is patched in afterwards.
    const auto set_next_block_link = []( std::vector<uint8_t> &block, const size_t next_index ){
        if( next_index == 0 )
        {
            block[0x0E] = 0;  //end of chain
            return;
        }

        const size_t value = next_index + 2;
        if( value > 255 )
          throw std::runtime_error( "CAMIO::CreateCAMFile: too many blocks to chain - next-block link ("
                                    + std::to_string(value) + ") does not fit in a byte." );
        block[0x0E] = static_cast<uint8_t>( value );
    };//set_next_block_link lambda

    size_t startRecord = 0;

    while (startRecord < lines.size()) {
        const size_t this_index = blockList.size();

        std::vector<std::vector<uint8_t>> lineSubset(lines.begin() + startRecord, lines.end());
        auto block = GenerateBlock(CAMBlock::NLINES, loc, lineSubset, 0, startRecord == 0);

        startRecord += ReadUInt16(block, 0x1E);

        set_next_block_link( block, (startRecord < lines.size()) ? (this_index + 1) : 0 );

        blockList.push_back(block);
        loc += static_cast<uint32_t>(BlockSize::NLINES);
    }

    // Create and place the Nucs in the blocks array
    startRecord = 0;

    while (startRecord < nucs.size()) {
        const size_t this_index = blockList.size();

        std::vector<std::vector<uint8_t>> nucSubset(nucs.begin() + startRecord, nucs.end());
        auto block = GenerateBlock(CAMBlock::NUCL, loc, nucSubset, 0, startRecord == 0);

        startRecord += ReadUInt16(block, 0x1E);

        set_next_block_link( block, (startRecord < nucs.size()) ? (this_index + 1) : 0 );

        blockList.push_back(block);
        loc += static_cast<uint32_t>(BlockSize::NUCL);
    }


    // The block directory lives between the file header and the first block (0x70 up to 0x800),
    //  so only 40 entries physically fit - and `ReadHeader()` only ever scans 28 of them.  Past
    //  that the directory would run into the first block and corrupt it, and `GenerateFile()`'s
    //  bounds check cannot catch it (it only compares against the whole file's length).  Fail
    //  loudly rather than write a file that silently loses nuclides.
    if( blockList.size() > sm_max_blocks )
      throw std::runtime_error( "CAMIO::CreateCAMFile: too much data - would need "
                                + std::to_string(blockList.size()) + " blocks, but a CAM file's"
                                " block directory only supports " + std::to_string(sm_max_blocks)
                                + ".  Reduce the number of nuclide library lines." );

    // Generate the file by combining blocks
    GenerateFile(blockList);

    // Put the file size in the file header
    uint32_t fileSize = static_cast<uint32_t>(writebytes.size());
    std::memcpy(&writebytes[0x0A], &fileSize, sizeof(uint32_t));
    
    // Clear the temporary data
    lines.clear();
    nucs.clear();
    specData.clear();
    writeNuclides.clear();
    writeEfficiencyPoints.clear();
    writeEfficiencyModel = EfficiencyModel::Unknown;
    writeEfficiencyFitCoeffs.clear();
    writeEfficiencyFitRefEnergy = 0.0f;
    writeEfficiencyFitChiSquare = 1.0f;
    writeEfficiencyDetectorName.clear();
    writePeaks.clear();
    writeLiveTime = 0.0f;

    // Reset the rest of the write-side state too, so this object can be used to write another
    //  file without inheriting this one's calibrations, sample title, detector type or blocks.
    //  (`writebytes` is deliberately left alone - it is what we are returning.)
    acqpCommon.assign( acqpCommon.size(), 0 );
    sampCommon.assign( sampCommon.size(), 0 );
    specBlock = false;
    sampBlock = false;
    num_channels = 0;
    det_info = DetInfo();
    analysisTime = SpecUtils::time_point_t{};

    return writebytes;
}

void CAMIO::GenerateFile(const std::vector<std::vector<byte_type>>& blocks) {
    // Calculate total file size
    size_t fileLength = 0x800;  // Initial header size
    for (const auto& block : blocks) {
        //auto blockType = static_cast<CAMBlock>(ReadUInt32(block, 0x00));
        fileLength += block.size();  // Use appropriate block size
    }

    // Create the container
    //std::vector<uint8_t> file(fileLength);
    writebytes.assign(fileLength, 0);  //assign (not resize) so no stale directory entries survive

    // fileHeader is a fixed-size array of 0x060 bytes, and writebytes is at least 0x800 bytes,
    // so this copy is always safe
    static_assert( fileHeader.size() <= 0x800, "fileHeader must fit in minimum writebytes size" );
    std::copy(fileHeader.begin(), fileHeader.end(), writebytes.begin());

    // Copy the blocks into the file
    size_t i = 0;
    for (const auto& block : blocks) {
        // Validate source bounds - block must have at least 0x30 bytes for header
        if( block.size() < 0x30 )
          throw std::out_of_range( "GenerateFile: block size (" + std::to_string(block.size()) + ") is smaller than header size (0x30)" );

        // Check for potential overflow in i * 0x30 calculation
        if( (i > 0) && (i > (std::numeric_limits<size_t>::max() / 0x30)) )
          throw std::out_of_range( "GenerateFile: block index * 0x30 would overflow" );

        // Validate destination bounds before copying block header
        const size_t headerDestStart = 0x70 + i * 0x30;
        const size_t headerDestEnd = headerDestStart + 0x30;
        if( headerDestEnd > writebytes.size() )
          throw std::out_of_range( "GenerateFile: block header destination (" + std::to_string(headerDestStart) + " + 0x30) exceeds writebytes size (" + std::to_string(writebytes.size()) + ")" );

        // Copy block header into the file header
        std::copy(block.begin(), block.begin() + 0x30, writebytes.begin() + headerDestStart);

        // The directory entry is otherwise a verbatim copy of the block's header, but Genie clears
        //  bit 0x0400 of the common flag in the directory: a block with 0x0500 (has a common
        //  section) or 0x0700 (a continuation block) is listed as 0x0100 or 0x0300.  This holds
        //  for every block of every Genie-written file checked.
        const uint16_t commonFlag = ReadUInt16(block, 0x04);
        const uint16_t dirFlag = static_cast<uint16_t>( commonFlag & ~uint16_t(0x0400) );
        std::memcpy( &writebytes[headerDestStart + 0x04], &dirFlag, sizeof(dirFlag) );

        // Copy the block to its location in the file.
        // Note: we use block.size() rather than the uint16_t block-size field at offset 0x06,
        //  because for SPEC blocks with many channels, the actual size can exceed 65535 bytes,
        //  overflowing the uint16_t field.
        uint32_t blockLoc = ReadUInt32(block, 0x0a);
        const size_t blockSize = block.size();

        // Validate destination bounds before copying block data
        if( (blockLoc > (std::numeric_limits<size_t>::max() - blockSize)) || ((blockLoc + blockSize) > writebytes.size()) )
          throw std::out_of_range( "GenerateFile: block destination (" + std::to_string(blockLoc) + " + " + std::to_string(blockSize) + ") exceeds writebytes size (" + std::to_string(writebytes.size()) + ")" );

        std::copy(block.begin(), block.end(), writebytes.begin() + blockLoc);

        i++;
    }


}

// add nuclide by values
void CAMIO::AddNuclide(const std::string& name, float halfLife, float halfLifeUnc,
                       const std::string& halfLifeUnit, int nucNo) {
    Nuclide nuc(name, halfLife, halfLifeUnc, halfLifeUnit, nucNo);
    AddNuclide(nuc);
}

// add nuclide by nuclide object
void CAMIO::AddNuclide(const Nuclide& nuc) {
    if (nucs.empty()) {
        nucs.clear();
    }

    // `Nuclide::HalfLife` is in `HalfLifeUnit`, but the record holds a CAM duration, which is
    //  always seconds - so convert here.  The unit *string* still goes out unmodified (it is
    //  written at record offset 0x61), so `ConvertHalfLife()`'s divide undoes this on read.
    //  This conversion used to be computed and then thrown away, writing e.g. Cs-137's half-life
    //  as 30.07 seconds rather than 30.07 years.
    const double seconds_per_unit = half_life_unit_to_seconds(nuc.HalfLifeUnit);

    Nuclide nuc_in_seconds = nuc;
    nuc_in_seconds.HalfLife = static_cast<float>( nuc.HalfLife * seconds_per_unit );
    nuc_in_seconds.HalfLifeUncertainty = static_cast<float>( nuc.HalfLifeUncertainty * seconds_per_unit );

    int nucNo = nuc.Index;

    // find the lines associated with the nuclide
    std::vector<uint16_t> lineNums;
    if (!lines.empty()) {
        for (size_t i = 0; i < lines.size(); i++) {
            if (lines[i][LineParameterLocation::NuclideIndex] == static_cast<uint8_t>(nucNo)) {
                lineNums.push_back(static_cast<uint16_t>(i + 1));
            }
        }
    }

    std::sort(lineNums.begin(), lineNums.end());

    std::vector<uint8_t> nucBytes = GenerateNuclide(nuc_in_seconds, lineNums);
    nucs.push_back(nucBytes);

}

// add line by values
void CAMIO::AddLine(float energy, float enUnc, float yield, float yieldUnc,
                    int nucNo, bool key) {
    Line line(energy, enUnc, yield, yieldUnc, nucNo, key);
    AddLine(line);
}

// add nuclude by values
void CAMIO::AddLine(const Line& line) 
{
    int nucNo = line.NuclideIndex;

    if (nucNo > 255) {
        throw std::runtime_error("Cannot have more than 255 nuclides");
    }

    // Initialize the lines if empty
    if (lines.empty()) {
        lines.clear();
    }

    // Generate the line bytes
    auto lineBytes = GenerateLine(line);

    // Find insertion point using binary search
    auto it = std::lower_bound(lines.begin(), lines.end(), lineBytes,
                              LineComparer());
    lines.insert(it, lineBytes);

    // If the nuclide already exists, add lines to it
    if (!nucs.empty() && static_cast<size_t>(nucNo) < nucs.size()) {
        std::vector<uint8_t> lineIndex = {static_cast<uint8_t>(std::distance(lines.begin(), it))};
        nucs[nucNo] = AddLinesToNuclide(nucs[nucNo], lineIndex);
    }
}

// add a line and nuclide by values
void CAMIO::AddLineAndNuclide(const float energy, const float yield, 
    const std::string& name, const float halfLife, const std::string& halfLifeUnit, 
    const bool noWtMn, const float enUnc, const float yieldUnc, const float halfLifeUnc,
    const bool isKeyLine)

{
    // check if the input has uncertainty, if not compute it from the value
    float energyUnc = (enUnc < size_t(0)) ? ComputeUncertainty(energy) : enUnc;
    float abundanceUnc = (yieldUnc < 0) ? ComputeUncertainty(yield) : yieldUnc;
    float t12Unc = (halfLifeUnc < size_t(0)) ? ComputeUncertainty(halfLife) : halfLifeUnc;


    int nucNo = static_cast<int>( writeNuclides.size() + 1 );
    // TODO try this out without this helper vector
    Nuclide nuc(name, halfLife, t12Unc, halfLifeUnit, nucNo );

    auto it = std::find(writeNuclides.begin(), writeNuclides.end(), nuc);

    if (it == writeNuclides.end()) 
    {
        writeNuclides.push_back(nuc);
    }
    else 
    {
        nucNo = (*it).Index;
    }
    // If the caller did not mark any key line, `AssignKeyLines()` (called from `CreateCAMFile()`)
    //  picks one; if they did, theirs is kept.
    Line line(energy, energyUnc, yield, abundanceUnc, nucNo, isKeyLine, noWtMn);

    AddLine(line); 

}

// Add an energy calibration 
void CAMInputOutput::CAMIO::AddEnergyCalibration(const std::vector<float> coefficients)
{
    enter_CAM_value("POLY", acqpCommon,  0x5E, cam_type::cam_string);
    enter_CAM_value("POLY", acqpCommon, 0xFB, cam_type::cam_string);
    enter_CAM_value("keV", acqpCommon,  0x346, cam_type::cam_string);
    enter_CAM_value(1.0, acqpCommon,  0x312, cam_type::cam_float);
    //check if there is energy calibration infomation
    if (!coefficients.empty()) {
        // ENGCAL is a fixed four-float field starting at 0x32E; writing more than this walks into
        //  the fields that follow it (e.g. the "keV" units string at 0x346, FWHMOFF at 0x3C6, and
        //  the coefficient count at 0x46C), so drop any higher-order terms.  GetEnergyCalibration()
        //  only ever reads four coefficients back, so they couldnt round-trip anyway.
        const size_t num_coefs = std::min(coefficients.size(), size_t(max_energy_cal_coefs));

        for (size_t i = 0; i < num_coefs; i++)
        {
            enter_CAM_value(coefficients[i], acqpCommon, 0x32E + i * 0x4, cam_type::cam_float);
        }
        enter_CAM_value(num_coefs, acqpCommon, 0x46C, cam_type::cam_word);
        enter_CAM_value(03, acqpCommon, 0x32A, cam_type::cam_longword); //ECALFLAGS set to energy and shape calibration
    }
    else
    {
        enter_CAM_value(02, acqpCommon,  0x32A, cam_type::cam_longword); //ECALFLAGS set to just shape calibration
    }
}

// Add the detector type
void CAMIO::AddDetectorType(const std::string& detector_type)
{
    enter_CAM_value("SQRT", acqpCommon,  0x464, cam_type::cam_string);
    if (detector_type.find("NaI") == 0 || detector_type.find("nai") == 0 || detector_type.find("NAI") == 0)
    {
        enter_CAM_value(-7.0, acqpCommon,  0x3C6, cam_type::cam_float); //FWHMOFF
        enter_CAM_value(2.0, acqpCommon, 0x3CA, cam_type::cam_float);  //FWHMSLOPE
    }
    else //use the Ge defualts
    {
        enter_CAM_value(1.0, acqpCommon, 0x3C6, cam_type::cam_float);
        enter_CAM_value(0.035, acqpCommon,  0x3CA, cam_type::cam_float);
    }
}

// Write a real FWHM = fwhmOffset + fwhmSlope*sqrt(energy) shape calibration, overwriting
// whatever AddDetectorType(...) may have set.
void CAMIO::AddShapeCalibration(const float fwhmOffset, const float fwhmSlope)
{
    enter_CAM_value("SQRT", acqpCommon, 0x464, cam_type::cam_string);
    enter_CAM_value(fwhmOffset, acqpCommon, 0x3C6, cam_type::cam_float); //FWHMOFF
    enter_CAM_value(fwhmSlope, acqpCommon, 0x3CA, cam_type::cam_float);  //FWHMSLOPE
}

// The low-tail curve occupies the third and fourth shape-calibration coefficients, immediately
// after FWHMOFF/FWHMSLOPE (GetShapeCalibration() reads all four from 0x30 + recOffset + 0xDC).
void CAMIO::AddLowTailCalibration(const float lowTailOffset, const float lowTailSlope)
{
    enter_CAM_value(lowTailOffset, acqpCommon, 0x3CE, cam_type::cam_float); //LOTAIL0
    enter_CAM_value(lowTailSlope, acqpCommon, 0x3D2, cam_type::cam_float);  //LOTAIL1
}

void CAMIO::AddEfficiencyModel(const EfficiencyModel model)
{
    writeEfficiencyModel = model;
}

void CAMIO::AddEfficiencyPoint(const float energy, const float efficiency, const float efficiencyUncertainty)
{
    EfficiencyPoint pt;
    pt.Index = static_cast<int>(writeEfficiencyPoints.size());
    pt.Energy = energy;
    pt.Efficiency = efficiency;
    pt.EfficiencyUncertainty = efficiencyUncertainty;
    writeEfficiencyPoints.push_back(pt);
}

void CAMIO::AddEfficiencyPoints(const std::vector<EfficiencyPoint>& points)
{
    for (const EfficiencyPoint &pt : points)
        AddEfficiencyPoint(pt.Energy, pt.Efficiency, pt.EfficiencyUncertainty);
}

void CAMIO::AddEfficiencyFit(const std::vector<float>& coefficients, const float referenceEnergy,
                             const float chiSquare, const std::string &detectorName)
{
    // Validate everything before touching any member, so a rejected call leaves whatever fit was
    //  previously set intact rather than half-overwritten.
    if( coefficients.size() > sm_geom_max_fit_coeffs )
      throw std::invalid_argument( "AddEfficiencyFit: at most "
                                   + std::to_string(sm_geom_max_fit_coeffs) + " coefficients can be"
                                   " written, but " + std::to_string(coefficients.size())
                                   + " were given." );

    for( const float c : coefficients )
    {
      if( !std::isfinite(c) )
        throw std::invalid_argument( "AddEfficiencyFit: non-finite coefficient." );
    }

    if( !std::isfinite(chiSquare) || !std::isfinite(referenceEnergy) )
      throw std::invalid_argument( "AddEfficiencyFit: non-finite chi-square or reference energy." );

    if( coefficients.empty() )
    {
      // Documented as clearing the fit, so clear all of it, not just the coefficients.
      writeEfficiencyFitCoeffs.clear();
      writeEfficiencyFitRefEnergy = 0.0f;
      writeEfficiencyFitChiSquare = 1.0f;
      writeEfficiencyDetectorName.clear();
      return;
    }

    writeEfficiencyFitCoeffs = coefficients;
    writeEfficiencyFitRefEnergy = (referenceEnergy > 0.0f) ? referenceEnergy
                                                           : sm_geom_default_fit_ref_energy;
    writeEfficiencyFitChiSquare = (chiSquare > 0.0f) ? chiSquare : 1.0f;
    writeEfficiencyDetectorName = detectorName;
}

// Writes the fitted efficiency curve into a GEOM block that already has its header and model name.
//
// Genie keeps the curve twice: as the plain `ln(eff) = sum_i{ c_i*ln(E)^i }` coefficients, and in
// the `ln(eff) = sum_j{ a_j*ln(E0/E)^j }` basis its Empirical dialog displays.  Substituting
// `x = L - u` (L = ln(E0), u = ln(E0/E)) into the first gives the second:
//    a_j = (-1)^j * sum_{i>=j}{ c_i * C(i,j) * L^(i-j) }
// Both were checked against the Ba-133 test file: its stored ln(E) coefficients are reproduced by
// fitting its own 19 efficiency points, and transforming them about its stored E0 (1260 keV) gives
// exactly the Empirical formula Genie displays for that file.
void CAMIO::WriteEfficiencyFit(std::vector<uint8_t> &block) const
{
    const size_t num_coefs = writeEfficiencyFitCoeffs.size();
    assert( num_coefs && (num_coefs <= sm_geom_max_fit_coeffs) );
    if( !num_coefs || (num_coefs > sm_geom_max_fit_coeffs) )
      return;

    const size_t recStart = static_cast<size_t>(block_header_size)
                             + static_cast<size_t>(sm_geom_rec_offset);

    // The furthest-out thing written; every other offset is below it.
    const size_t needed = recStart + sm_geom_blank_field_offset + sm_geom_blank_field_size;
    if( needed > block.size() )
      throw std::out_of_range( "WriteEfficiencyFit: block too small for the efficiency fit" );

    const float refEnergy = (writeEfficiencyFitRefEnergy > 0.0f) ? writeEfficiencyFitRefEnergy
                                                                 : sm_geom_default_fit_ref_energy;
    enter_CAM_value( refEnergy, block, recStart + sm_geom_fit_ref_energy_offset, cam_type::cam_float );
    enter_CAM_value( static_cast<uint32_t>(num_coefs - 1), block,
                     recStart + sm_geom_fit_order_offset, cam_type::cam_longword );

    for( size_t i = 0; i < num_coefs; ++i )
      enter_CAM_value( writeEfficiencyFitCoeffs[i], block,
                       recStart + sm_geom_fit_ln_e_coeffs_offset + 4*i, cam_type::cam_float );

    // Binomial coefficients C(i,j), by Pascal's rule (the zero-initialization supplies the
    //  out-of-triangle terms).
    double binomial[sm_geom_max_fit_coeffs][sm_geom_max_fit_coeffs] = {};
    binomial[0][0] = 1.0;
    for( size_t i = 1; i < sm_geom_max_fit_coeffs; ++i )
    {
      binomial[i][0] = 1.0;
      for( size_t j = 1; j <= i; ++j )
        binomial[i][j] = binomial[i-1][j-1] + binomial[i-1][j];
    }

    const double lnRef = std::log( static_cast<double>(refEnergy) );
    for( size_t j = 0; j < num_coefs; ++j )
    {
      double a_j = 0.0;
      for( size_t i = j; i < num_coefs; ++i )
        a_j += writeEfficiencyFitCoeffs[i] * binomial[i][j] * std::pow( lnRef, static_cast<double>(i - j) );

      if( j % 2 )
        a_j = -a_j;

      enter_CAM_value( static_cast<float>(a_j), block,
                       recStart + sm_geom_fit_display_coeff_offsets[j], cam_type::cam_float );
    }//for( loop over display-basis coefficients )

    // The marks of a calibration Genie performed itself.  Without these Genie offers only
    //  "Empirical" and "Interpolated" for the curve above; with them it offers "Dual" as well.
    //  See `sm_geom_calibrated_flag_offset` for how that was established.
    const uint8_t order_byte = static_cast<uint8_t>( num_coefs - 1 );
    block[recStart + sm_geom_calibrated_flag_offset] = 0x02;
    block[recStart + sm_geom_fit_order2_offset] = order_byte;
    block[recStart + sm_geom_fit_order3_offset] = order_byte;

    enter_CAM_value( 1.0f, block, recStart + sm_geom_fit_scale_offset, cam_type::cam_float );
    enter_CAM_value( writeEfficiencyFitChiSquare, block,
                     recStart + sm_geom_fit_chi_square_offset, cam_type::cam_float );
    enter_CAM_value( writeEfficiencyFitChiSquare, block,
                     recStart + sm_geom_fit_chi_square2_offset, cam_type::cam_float );

    const auto put_padded = [&block,recStart]( const size_t offset, const std::string &text,
                                               const size_t field_len ){
      for( size_t i = 0; i < field_len; ++i )
        block[recStart + offset + i] = static_cast<uint8_t>( (i < text.size()) ? text[i] : ' ' );
    };

    put_padded( sm_geom_engine_name_offset, "Gamma Efcal v2.2", 16 );
    put_padded( sm_geom_detector_name_offset, writeEfficiencyDetectorName, sm_geom_name_field_size );
    put_padded( sm_geom_cal_file_name_offset, "", sm_geom_name_field_size );
    put_padded( sm_geom_blank_field_offset, "", sm_geom_blank_field_size );
}//CAMIO::WriteEfficiencyFit(...)


// Generates the GEOM (efficiency) block from `writeEfficiencyModel`/`writeEfficiencyPoints`.
//
// Validated against real Genie 2000: it reads the efficiency points written here, and offers its
// "Empirical", "Dual" and "Interpolated" models for the curve `WriteEfficiencyFit(...)` adds.
// (It also offers "Linear" for its own files, which additionally carry six floats at record offset
// 599 whose meaning has not been worked out - see `sm_geom_fit_ln_e_coeffs_offset`.)
std::vector<uint8_t> CAMIO::GenerateGeometryBlock(size_t loc)
{
    if( writeEfficiencyPoints.size() > sm_max_efficiency_points )
      throw std::invalid_argument( "GenerateGeometryBlock: too many efficiency points ("
                                   + std::to_string(writeEfficiencyPoints.size()) + "); the block"
                                   " size field only supports " + std::to_string(sm_max_efficiency_points) + "." );

    // These match a real Genie-written GEOM block (see the Ba-133 test file); the previous
    //  values here were self-consistent with our own reader but looked nothing like a real file.
    const uint16_t recOffset = sm_geom_rec_offset;
    const uint16_t entOffset = sm_geom_ent_offset;
    const uint16_t entSize = sm_geom_ent_size;
    const uint16_t numPoints = static_cast<uint16_t>(writeEfficiencyPoints.size());

    const size_t usedSize = static_cast<size_t>(block_header_size) +
                             static_cast<size_t>(recOffset) +
                             static_cast<size_t>(entOffset) +
                             static_cast<size_t>(entSize) * numPoints;

    // Every block in every Genie-produced file examined starts on, and is a multiple of, 512
    //  bytes; pad to keep that invariant.
    //
    // The block must also actually contain the record its own header declares.  The header copies
    //  a real file's `recSize` of 0x0F02 verbatim (see `GenerateBlockHeader`), so the record runs
    //  to `block_header_size + recOffset + recSize` whatever the entry count - sizing the block
    //  from the entries alone left it ending 1874 bytes short of that for a 20-point curve, and a
    //  reader walking the record would have run into the next block, or off the end of the file
    //  when GEOM was the last block.  All three real files satisfy this bound.
    const size_t recordEnd = static_cast<size_t>(block_header_size)
                              + static_cast<size_t>(recOffset) + sm_geom_rec_size;
    const size_t blockSize = ((std::max(usedSize,recordEnd) + 0x1FF) / 0x200) * 0x200;

    std::vector<uint8_t> block(blockSize, 0);

    // numRec=1: all efficiency points are written as entries of a single logical "record"
    // (matching ReadGeometryBlock()'s per-record entry-tag loop with the record index i=0).
    // The numLines parameter is repurposed here to carry the point count for block-size bookkeeping.
    const std::vector<uint8_t> header = GenerateBlockHeader(CAMBlock::GEOM, loc, 1, numPoints, 0, true);
    //  (GenerateBlockHeader computes the same padded size from numPoints; keep them in step)

    if( header.size() > block.size() )
      throw std::out_of_range( "GenerateGeometryBlock: header larger than block" );
    std::copy(header.begin(), header.end(), block.begin());

    std::string modelStr;
    switch (writeEfficiencyModel)
    {
        case EfficiencyModel::SPLINE:    modelStr = "SPLINE";    break;
        case EfficiencyModel::EMPIRICAL: modelStr = "EMPIRICAL"; break;
        case EfficiencyModel::AVERAGE:   modelStr = "AVERAGE";   break;
        case EfficiencyModel::DUAL:      modelStr = "DUAL";      break;
        case EfficiencyModel::LINEAR:    modelStr = "LINEAR";    break;
        case EfficiencyModel::INTERPOL:  modelStr = "INTERPOL";  break;
        case EfficiencyModel::Unknown:
        case EfficiencyModel::NotReadin:
        default:
          // Points with no model named are what Genie itself calls "INTERPOL"; "SPLINE" (the
          //  previous default here) is not a name Genie 2000 uses.
          modelStr = "INTERPOL";
          break;
    }

    // `ReadGeometryBlock()` reads the model name at pos+recOffset+222, and a real Genie file puts
    //  it exactly there (a real block holds e.g. "INTERPOL" at recOffset 32 + 222).  Genie writes
    //  the full `sm_geom_model_name_size`-byte field, space padded.
    const size_t typeStrPos = static_cast<size_t>(recOffset) + 222;
    if( (typeStrPos + sm_geom_model_name_size) > block.size() )
      throw std::out_of_range( "GenerateGeometryBlock: block too small for model type string" );
    for( size_t i = 0; i < sm_geom_model_name_size; ++i )
      block[typeStrPos + i] = static_cast<uint8_t>( (i < modelStr.size()) ? modelStr[i] : ' ' );

    // The fitted curve.  Without this a GEOM block holds calibration points but no curve, and
    //  Genie can only offer its "Interpolated" model; see `AddEfficiencyFit(...)`.
    if( !writeEfficiencyFitCoeffs.empty() )
      WriteEfficiencyFit( block );

    const size_t entStart = static_cast<size_t>(block_header_size)
                             + static_cast<size_t>(recOffset) + static_cast<size_t>(entOffset);
    for( size_t i = 0; i < numPoints; ++i )
    {
        const size_t entPos = entStart + i * static_cast<size_t>(entSize);
        if( (entPos + entSize) > block.size() )
          throw std::out_of_range( "GenerateGeometryBlock: entry destination out of bounds" );

        block[entPos] = static_cast<uint8_t>(1); // entry tag: record index (0) + 1, per ReadGeometryBlock()

        const EfficiencyPoint &pt = writeEfficiencyPoints[i];
        enter_CAM_value(pt.Energy, block, entPos + static_cast<size_t>(EfficiencyPointParameterLocation::Energy), cam_type::cam_float);
        enter_CAM_value(pt.Efficiency, block, entPos + static_cast<size_t>(EfficiencyPointParameterLocation::Efficiency), cam_type::cam_float);
        enter_CAM_value(pt.EfficiencyUncertainty, block, entPos + static_cast<size_t>(EfficiencyPointParameterLocation::EfficiencyUncertainty), cam_type::cam_float);
    }

    return block;
}

namespace
{
  /** Saturates a peak's channel number into the uint16 the DISP block stores it in.

   `GeneratePeakBlock()` and `GenerateDispBlock()` must agree byte-for-byte on a peak's channel
   range - they describe the same ROI from two different blocks, and Genie reads both - so both
   go through this rather than clamping their own way.
   */
  uint16_t clamp_peak_channel( const int channel )
  {
    return static_cast<uint16_t>( std::max(0, std::min(channel, 0xFFFF)) );
  }
}//namespace


void CAMIO::AddPeak(const Peak& peak)
{
    writePeaks.push_back(peak);
}

void CAMIO::AddPeaks(const std::vector<Peak>& peaks_in)
{
    writePeaks.insert(writePeaks.end(), peaks_in.begin(), peaks_in.end());
}

// Generates the PEAK block from `writePeaks`.
//
// The block header mirrors the PEAK block of a real Genie-produced CNF (headSize 0x30,
// recOffset 243, recSize 248, commonFlag 0x0500, 512-byte aligned size) - `peakBlockMatchesReal-
// GenieLayout` in InterSpec's test_ExportCAM.cpp checks it word for word against cs137.CNF.
//
// The field offsets within a record come from the two Genie files that have *populated* PEAK
// blocks: Ba-133.cnf (23 records, recSize 240) and cs137.CNF (one record, recSize 248).  The
// third file available, Q_C_UCRM125A_OSL_K_..., has a single all-zero record and says nothing
// about the layout.  Real Genie 2000 reads peaks written by this function - but only alongside
// the DISP block `CreateCAMFile()` writes with it; see `sm_disp_rec_size`.
std::vector<uint8_t> CAMIO::GeneratePeakBlock(size_t loc)
{
    const uint16_t recOffset = 0x00F3;   //243, matching RecordSize::PEAK's source file
    const uint16_t recSize = static_cast<uint16_t>( RecordSize::PEAK );

    if( writePeaks.size() > 0xFFFF )
      throw std::invalid_argument( "GeneratePeakBlock: too many peaks ("
                                   + std::to_string(writePeaks.size()) + ")" );

    const uint16_t numPeaks = static_cast<uint16_t>( writePeaks.size() );

    // Each record begins with a 0x01 marker byte, with the first field one byte further in (this
    //  is the same shape as a line record, whose byte 0 is likewise 0x01 - see
    //  `LineParameterLocation::Energy`, which is 0x01 for that reason).  `ReadPeaksBlock` folds
    //  that byte into the record start instead, hence its `+ 0x01`.
    const size_t recBase = static_cast<size_t>(block_header_size) + static_cast<size_t>(recOffset);
    const size_t usedSize = recBase + static_cast<size_t>(recSize) * numPeaks;

    // Real Genie files always allocate 0x3000 for the PEAK block, whether it holds 1 peak or 23,
    //  so match that unless there are enough peaks to need more.
    const size_t blockSize = std::max<size_t>( sm_genie_peak_block_size,
                                               ((usedSize + 0x1FF) / 0x200) * 0x200 );

    std::vector<uint8_t> block(blockSize, 0);

    const std::vector<uint8_t> header = GenerateBlockHeader(CAMBlock::PEAK, loc, numPeaks, 0, 0, true);
    if( header.size() > block.size() )
      throw std::out_of_range( "GeneratePeakBlock: header larger than block" );
    std::copy(header.begin(), header.end(), block.begin());

    // The PEAK block's "common" area names the algorithms that produced the peaks.  Genie writes
    //  these into every file with a populated PEAK block, and appears to use them to decide a peak
    //  search/fit was actually performed - without them it ignores the records entirely.  Offsets
    //  and strings are copied verbatim from Genie-written files (cs137.CNF and Ba-133.cnf, which
    //  agree on all of this apart from the fit version, and the timestamps, which come from
    //  `analysisTime` and are simply not written when it is unset).
    const auto put_string = [&block]( const size_t offset, const char * const str ){
        const size_t len = std::strlen( str );
        if( (offset + len) > block.size() )
          throw std::out_of_range( "GeneratePeakBlock: analysis-engine name out of bounds" );
        std::memcpy( &block[offset], str, len );
    };

    put_string( sm_genie_peak_search_name_offset, "2nd Diff v2.1   " );
    put_string( sm_genie_peak_fit_name_offset,    "NLSQ Fit v2.8   " );

    if( !SpecUtils::is_special(analysisTime) )
    {
        for( const size_t offset : sm_genie_peak_time_offsets )
          enter_CAM_value( analysisTime, block, offset, cam_type::cam_datetime );
    }

    // 0x30 looks to be a bitmask of which analysis steps ran: the Ba-133 file has 0x22 and also
    //  carries a "Std Efcor v2.1" entry, while cs137.CNF has 0x20 and does not.  We do no
    //  efficiency correction of peak areas, so 0x20 is the case we match.
    enter_CAM_value( uint32_t(0x20), block, 0x30, cam_type::cam_longword );

    // The live time the peaks were fit over, as a CAM duration (a negative count of 100 ns ticks -
    //  the same encoding the ACQP block's times use).  Ba-133.cnf holds -300 s here and cs137.CNF
    //  -7400 s, each matching that file's own Area/CountRate ratio.
    enter_CAM_value( writeLiveTime, block, 0x34, cam_type::cam_duration );

    for( size_t i = 0; i < numPeaks; ++i )
    {
        const size_t recPos = recBase + i * static_cast<size_t>(recSize) + 1;
        if( (recPos + recSize) > block.size() )
          throw std::out_of_range( "GeneratePeakBlock: record destination out of bounds" );

        block[recPos - 1] = 0x01;  //record marker; see `recBase` above

        const Peak &p = writePeaks[i];

        const auto put_float = [&block,recPos]( const PeakParameterLocation where, const float value ){
            enter_CAM_value( value, block, recPos + static_cast<size_t>(where), cam_type::cam_float );
        };

        put_float( PeakParameterLocation::Energy, p.Energy );
        put_float( PeakParameterLocation::Centroid, p.Centroid );
        put_float( PeakParameterLocation::CentroidUncertainty, p.CentroidUncertainty );
        put_float( PeakParameterLocation::FullWidthAtHalfMaximum, p.FullWidthAtHalfMaximum );
        put_float( PeakParameterLocation::LowTail, p.LowTail );
        put_float( PeakParameterLocation::Area, p.Area );
        put_float( PeakParameterLocation::AreaUncertainty, p.AreaUncertainty );
        put_float( PeakParameterLocation::Continuum, p.Continuum );
        put_float( PeakParameterLocation::CriticalLevel, p.CriticalLevel );
        put_float( PeakParameterLocation::CountRate, p.CountRate );
        put_float( PeakParameterLocation::CountRateUncertainty, p.CountRateUncertainty );

        // Note: `Width` is a 16-bit field.  Writing it as a longword would run into `Continuum`
        //  at 0x0C and silently zero it - the byte budget only works if this is 16 bits, which is
        //  also what `GetPeaks()` assumes.
        //
        // Both ends go through `clamp_peak_channel(...)`, the same way `GenerateDispBlock()` does
        //  them, so the two blocks can never describe different channel ranges for one peak.  The
        //  left channel used to be written unclamped and the width derived from the unclamped
        //  values, which for a negative or >65535 channel put a range in PEAK that DISP disagreed
        //  with (and, for >65535, stopped a peak from matching even itself in the multiplet scan).
        const uint16_t left = clamp_peak_channel( p.LeftChannel );
        const uint16_t right = clamp_peak_channel( p.RightChannel );
        const int width_int = (right >= left) ? (static_cast<int>(right) - static_cast<int>(left) + 1) : 1;
        const uint16_t width = static_cast<uint16_t>( std::min(width_int, 0xFFFF) );
        enter_CAM_value( static_cast<uint32_t>(left), block, recPos + static_cast<size_t>(PeakParameterLocation::LeftChannel), cam_type::cam_longword );
        enter_CAM_value( width, block, recPos + static_cast<size_t>(PeakParameterLocation::Width), cam_type::cam_word );

        // Fields Genie fills on every peak of every file checked, but that are not simply more of
        //  the peak's fitted quantities; see `PeakParameterLocation2` for what each is.  Several
        //  quantities are stored twice, the centroid and the area among them - our own reader only
        //  ever looked at one copy of each, so a file could round-trip through this class with the
        //  other copy left zero.
        const auto put_float2 = [&block,recPos]( const PeakParameterLocation2 where, const float value ){
            enter_CAM_value( value, block, recPos + static_cast<size_t>(where), cam_type::cam_float );
        };

        put_float2( PeakParameterLocation2::CentroidAgain, p.Centroid );
        put_float2( PeakParameterLocation2::AreaAgain, p.Area );
        put_float2( PeakParameterLocation2::AreaUncertaintyAgain, p.AreaUncertainty );
        put_float2( PeakParameterLocation2::Efficiency, p.Efficiency );
        put_float2( PeakParameterLocation2::EfficiencyAgain, p.Efficiency );
        put_float2( PeakParameterLocation2::EfficiencyUncertainty, p.EfficiencyUncertainty );
        put_float2( PeakParameterLocation2::EfficiencyUncertaintyAgain, p.EfficiencyUncertainty );
        put_float2( PeakParameterLocation2::Significance, p.Significance );
        put_float2( PeakParameterLocation2::BackgroundSigma, p.BackgroundSigma );

        // Never zero in a real file, but what it counts is unknown (see `UnknownLongword`), so
        //  write the smallest value seen rather than invent a meaning for it.
        enter_CAM_value( uint32_t(1), block,
                        recPos + static_cast<size_t>(PeakParameterLocation2::UnknownLongword),
                        cam_type::cam_longword );

        //  Compare the same clamped channel numbers `GenerateDispBlock()` groups on, so a peak is
        //  never flagged as a singlet while DISP counts it in a multi-peak region.
        size_t multiplet_index = 0, multiplet_count = 0;
        for( size_t j = 0; j < numPeaks; ++j )
        {
            const Peak &o = writePeaks[j];
            if( (clamp_peak_channel(o.LeftChannel) != left)
                || (clamp_peak_channel(o.RightChannel) != right) )
              continue;

            if( j < i )
              multiplet_index += 1;
            multiplet_count += 1;
        }//for( count the peaks sharing this ROI )

        const bool in_multiplet = (multiplet_count > 1);
        block[recPos + static_cast<size_t>(PeakParameterLocation2::MultipletFlag)] = in_multiplet ? 0xD8 : 0x10;
        if( in_multiplet )
        {
            block[recPos + static_cast<size_t>(PeakParameterLocation2::MultipletIndex)]
                                        = static_cast<uint8_t>( std::min<size_t>(multiplet_index, 0xFF) );
            block[recPos + static_cast<size_t>(PeakParameterLocation2::MultipletFlag2)] = 0x03;
        }

        const char * const engine = "PANOLIN1S";
        std::memcpy( &block[recPos + static_cast<size_t>(PeakParameterLocation2::FitEngineName)],
                     engine, std::strlen(engine) );
    }//for( loop over peaks )

    return block;
}//GeneratePeakBlock(...)


std::vector<uint8_t> CAMIO::GenerateDispBlock(size_t loc)
{
    // One record per distinct peak channel range, in the order the peaks appear - matching every
    //  Genie file checked, where the DISP regions are exactly the distinct (LeftChannel,
    //  RightChannel) pairs of the PEAK records, in the same order.
    struct Region { uint16_t left, right, num_peaks; };
    std::vector<Region> regions;
    for( const Peak &p : writePeaks )
    {
        const uint16_t left = clamp_peak_channel( p.LeftChannel );
        const uint16_t right = clamp_peak_channel( p.RightChannel );

        const auto pos = std::find_if( begin(regions), end(regions), [left,right]( const Region &r ){
            return (r.left == left) && (r.right == right);
        } );

        if( pos != end(regions) )
            pos->num_peaks += 1;
        else
            regions.push_back( Region{ left, right, 1 } );
    }//for( const Peak &p : writePeaks )

    if( regions.size() > 0xFFFF )
      throw std::invalid_argument( "GenerateDispBlock: too many regions ("
                                   + std::to_string(regions.size()) + ")" );

    const uint16_t numRegions = static_cast<uint16_t>( regions.size() );
    const size_t recStart = static_cast<size_t>(block_header_size) + static_cast<size_t>(sm_disp_rec_offset);
    const size_t usedSize = recStart + static_cast<size_t>(sm_disp_rec_size) * numRegions;
    const size_t blockSize = std::max<size_t>( sm_genie_disp_block_size,
                                               ((usedSize + 0x1FF) / 0x200) * 0x200 );

    std::vector<uint8_t> block(blockSize, 0);

    const std::vector<uint8_t> header = GenerateBlockHeader(CAMBlock::DISP, loc, numRegions, 0, 0, true);
    if( header.size() > block.size() )
      throw std::out_of_range( "GenerateDispBlock: header larger than block" );
    std::copy(header.begin(), header.end(), block.begin());

    for( size_t i = 0; i < regions.size(); ++i )
    {
        const size_t recPos = recStart + i * static_cast<size_t>(sm_disp_rec_size);
        if( (recPos + sm_disp_rec_size) > block.size() )
          throw std::out_of_range( "GenerateDispBlock: record destination out of bounds" );

        const Region &r = regions[i];
        enter_CAM_value( sm_disp_rec_size, block, recPos + 0x00, cam_type::cam_word );
        block[recPos + 0x02] = 0x01;
        block[recPos + 0x03] = static_cast<uint8_t>( std::min<uint16_t>(r.num_peaks, 0xFF) );
        enter_CAM_value( r.left, block, recPos + 0x04, cam_type::cam_word );
        enter_CAM_value( r.right, block, recPos + 0x06, cam_type::cam_word );
    }//for( loop over regions )

    return block;
}//GenerateDispBlock(...)


// Add the count start time
void CAMIO::AddAcquitionTime(const SpecUtils::time_point_t& start_time)
{
    enter_CAM_value(0x01, acqpCommon, acqp_rec_tab_loc, cam_type::cam_byte);
    enter_CAM_value(start_time, acqpCommon, static_cast<size_t>(acqp_rec_tab_loc + uint16_t(0x01)), cam_type::cam_datetime);

    //set the sampling time to the aqusition start time
    enter_CAM_value(start_time, sampCommon, 0xB4, cam_type::cam_datetime);

    analysisTime = start_time;
}
// Add the real time
void CAMIO::AddRealTime(const float real_time)
{
    enter_CAM_value(real_time, acqpCommon, static_cast<size_t>(acqp_rec_tab_loc + uint16_t(0x09)), cam_type::cam_duration);
}

// Add the live time
void CAMIO::AddLiveTime(const float live_time)
{
    writeLiveTime = live_time;
    enter_CAM_value(live_time, acqpCommon, static_cast<size_t>(acqp_rec_tab_loc + uint16_t(0x11)), cam_type::cam_duration);
}
// Add the sample title
void CAMIO::AddSampleTitle(const std::string& title)
{
    sampBlock = true;
    enter_CAM_value(1.0, sampCommon, 0x90, cam_type::cam_float);
    std::string temp = title;
    temp.resize(0x40);
    enter_CAM_value(temp, sampCommon, 0x0);
}

// Add GPS data
void CAMIO::AddGPSData(const double latitude, const double longitude, const float speed, const SpecUtils::time_point_t& position_time)
{
    AddGPSData(latitude, longitude, speed);

    enter_CAM_value(position_time, sampCommon, 0x940, cam_type::cam_datetime);
}

void CAMIO::AddGPSData(const double latitude, const double longitude, const float speed)
{
    enter_CAM_value(latitude, sampCommon, 0x8D0, cam_type::cam_double);
    enter_CAM_value(longitude, sampCommon, 0x928, cam_type::cam_double);
    enter_CAM_value(speed, sampCommon, 0x938, cam_type::cam_double);
}

// Add a spectrum
void CAMIO::AddSpectrum(const std::vector<uint32_t>& channel_counts)
{
    //size_t data_loc = 0x30;
    num_channels = channel_counts.size();
    specData.resize(num_channels * sizeof(uint32_t));
    // put the spectral data in
    for (size_t i = 0; i < num_channels; i++)
    {
        enter_CAM_value(channel_counts[i], specData, sizeof(uint32_t) * i, cam_type::cam_longword);
    }
    // add the channel numbers to the acqp data
    enter_CAM_value(num_channels, acqpCommon, 0x89, cam_type::cam_longword);
    specBlock = true;
    //a samp block is needed if there is a spectrum
    sampBlock = true;
}

void CAMIO::AddSpectrum(const std::vector<float>& channel_counts)
{   
    //size_t data_loc = 0x30;
    num_channels = channel_counts.size();
    specData.resize(num_channels * sizeof(uint32_t));
    // put the spectral data in
    for (size_t i = 0; i < num_channels; i++)
    {
        const uint32_t counts = SpecUtils::float_to_integral<uint32_t>(channel_counts[i]);
        enter_CAM_value(counts, specData, sizeof(uint32_t) * i, cam_type::cam_longword);
    }
    // add the channel numbers to the acqp data
    enter_CAM_value(num_channels, acqpCommon, 0x89, cam_type::cam_longword);
    specBlock = true;
    //a samp block is needed if there is a spectrum
    sampBlock = true;
}

// generate a nuclide record
std::vector<byte_type> CAMIO::GenerateNuclide(const Nuclide nuclide,
                                           const std::vector<uint16_t>& lineNums) {
    if( lineNums.empty() )
      throw std::invalid_argument( "GenerateNuclide: lineNums must not be empty" );

    const size_t numLines = lineNums.size();
    const size_t nucRecSize = static_cast<size_t>( RecordSize::NUCL );

    // The record's size field (which `GetNuclides()` reads back as a little-endian uint16 at
    //  offset 0, and from which it derives the line count) has to hold the whole record.
    const size_t sizeField = (numLines - 1)*3 + nucRecSize + 0x03;
    if( sizeField > 0xFFFF )
      throw std::invalid_argument( "GenerateNuclide: too many lines for one nuclide ("
                                   + std::to_string(numLines) + "); the record size field is only"
                                   " 16 bits." );

    std::vector<uint8_t> nuc( nucRecSize + numLines * 3 );

    // Set the number of line parameter.
    //  Note: this is a real 16-bit field - writing only the low byte and hard-coding 0x02 as the
    //  "spacer" at offset 1 (as this used to) silently produced unreadable records for any
    //  nuclide with more than 65 lines, which nuclides like Eu-152 and Bi-214 easily exceed.
    nuc[0] = static_cast<uint8_t>( sizeField & 0xFF );
    nuc[1] = static_cast<uint8_t>( (sizeField >> 8) & 0xFF );

    // Set the spacer
    nuc[2] = 0x01;
    nuc[0x5f] = 0x01;

    // Set the time spans
    // TODO check if this half-life needs to be converted
    auto halfLifeBytes = convert_to_CAM_duration(nuclide.HalfLife);
    auto halfLifeUncBytes = convert_to_CAM_duration(nuclide.HalfLifeUncertainty);

    // Validate destination bounds before copying (all CAM durations/doubles are 8 bytes)
    if( (NuclideParameterLocation::HalfLife + halfLifeBytes.size()) > nuc.size() )
      throw std::out_of_range( "GenerateNuclide: HalfLife destination out of bounds" );
    if( (NuclideParameterLocation::HalfLifeUncertainty + halfLifeUncBytes.size()) > nuc.size() )
      throw std::out_of_range( "GenerateNuclide: HalfLifeUncertainty destination out of bounds" );

    std::copy(halfLifeBytes.begin(), halfLifeBytes.end(), nuc.begin() + NuclideParameterLocation::HalfLife);
    std::copy(halfLifeUncBytes.begin(), halfLifeUncBytes.end(), nuc.begin() + NuclideParameterLocation::HalfLifeUncertainty);

    // do the activites
    auto activityBytes = convert_to_CAM_double(nuclide.Activity);
    auto actUncBytes = convert_to_CAM_double(nuclide.ActivityUnc);
    auto mdaBytes = convert_to_CAM_double(nuclide.MDA);

    // Validate destination bounds before copying
    if( (NuclideParameterLocation::MeanActivity + activityBytes.size()) > nuc.size() )
      throw std::out_of_range( "GenerateNuclide: MeanActivity destination out of bounds" );
    if( (NuclideParameterLocation::MeanActivityUnceratinty + actUncBytes.size()) > nuc.size() )
      throw std::out_of_range( "GenerateNuclide: MeanActivityUnceratinty destination out of bounds" );
    if( (NuclideParameterLocation::NuclideMDA + mdaBytes.size()) > nuc.size() )
      throw std::out_of_range( "GenerateNuclide: NuclideMDA destination out of bounds" );

    std::copy(activityBytes.begin(), activityBytes.end(), nuc.begin() + NuclideParameterLocation::MeanActivity);
    std::copy(actUncBytes.begin(), actUncBytes.end(), nuc.begin() + NuclideParameterLocation::MeanActivityUnceratinty);
    std::copy(mdaBytes.begin(), mdaBytes.end(), nuc.begin() + NuclideParameterLocation::NuclideMDA);

    // Set the strings
    std::string paddedName = nuclide.Name;
    paddedName.resize(8, ' ');
    std::string paddedUnit = nuclide.HalfLifeUnit;
    // Ensure the unit is always uppercase
    std::transform(paddedUnit.begin(), paddedUnit.end(), paddedUnit.begin(), ::toupper);
    paddedUnit.resize(2, ' ');

    // Validate destination bounds before copying strings
    if( (0x03 + paddedName.size()) > nuc.size() )
      throw std::out_of_range( "GenerateNuclide: Name destination out of bounds" );
    if( (0x61 + paddedUnit.size()) > nuc.size() )
      throw std::out_of_range( "GenerateNuclide: HalfLifeUnit destination out of bounds" );

    std::copy(paddedName.begin(), paddedName.end(), nuc.begin() + 0x03);
    std::copy(paddedUnit.begin(), paddedUnit.end(), nuc.begin() + 0x61);

    // Add the lines
    for (size_t i = 0; i < lineNums.size(); i++) {
        size_t offset = static_cast<size_t>(RecordSize::NUCL) + i * nuclide_line_size;

        // Validate destination bounds before memcpy
        if( (offset + 1 + sizeof(uint16_t)) > nuc.size() )
          throw std::out_of_range( "GenerateNuclide: line number destination out of bounds at index " + std::to_string(i) );

        nuc[offset] = 0x01;
        uint16_t lineNum = lineNums[i];
        std::memcpy(&nuc[offset + 1], &lineNum, sizeof(uint16_t));
    }

    return nuc;
}

// add lines to an existing nuclide
std::vector<byte_type> CAMIO::AddLinesToNuclide(const std::vector<byte_type>& nuc,
                                             const std::vector<byte_type>& lineNums) {
    uint32_t numLines = static_cast<uint32_t>(lineNums.size());

    // Set the number of line parameter
    std::vector<uint8_t> result = nuc;
    result[0] = static_cast<uint8_t>((numLines - 1) * nuclide_line_size + static_cast<uint16_t>(RecordSize::NUCL) + nuclide_line_size);

    // Create the lines list
    std::vector<uint8_t> linesList(numLines * 3);
    std::vector<uint8_t> sortedLineNums = lineNums;
    std::sort(sortedLineNums.begin(), sortedLineNums.end());

    for (size_t i = 0; i < numLines; i++) {
        size_t offset = i * 3;
        linesList[offset] = 0x01;
        linesList[offset + 1] = sortedLineNums[i];
    }

    // Add the lines list to the result
    result.insert(result.end(), linesList.begin(), linesList.end());
    return result;
}

// generate line record
std::vector<byte_type> CAMIO::GenerateLine(const Line t_line) {
    std::vector<uint8_t> line(static_cast<size_t>(static_cast<uint16_t>(RecordSize::NLINES)));

    line[0] = 0x01;

    auto energyBytes = convert_to_CAM_float(t_line.Energy);
    auto enUncBytes = convert_to_CAM_float(t_line.EnergyUncertainty);
    auto yieldBytes = convert_to_CAM_float(t_line.Abundance);
    auto yieldUncBytes = convert_to_CAM_float(t_line.AbundanceUncertainty);

    auto activityBytes = convert_to_CAM_double(t_line.LineActivity);
    auto actUncBytes = convert_to_CAM_double(t_line.LineActivityUnceratinty);
    auto lineEff = convert_to_CAM_float(t_line.LineEfficiency);
    auto lnEffUnc = convert_to_CAM_float(t_line.LineEfficiencyUncertainty);
    auto lineMDA = convert_to_CAM_double(t_line.LineMDA);

    // Validate destination bounds before copying (floats are 4 bytes, doubles are 8 bytes)
    if( (LineParameterLocation::Energy + energyBytes.size()) > line.size() )
      throw std::out_of_range( "GenerateLine: Energy destination out of bounds" );
    if( (LineParameterLocation::EnergyUncertainty + enUncBytes.size()) > line.size() )
      throw std::out_of_range( "GenerateLine: EnergyUncertainty destination out of bounds" );
    if( (LineParameterLocation::Abundance + yieldBytes.size()) > line.size() )
      throw std::out_of_range( "GenerateLine: Abundance destination out of bounds" );
    if( (LineParameterLocation::AbundanceUncertainty + yieldUncBytes.size()) > line.size() )
      throw std::out_of_range( "GenerateLine: AbundanceUncertainty destination out of bounds" );
    if( (LineParameterLocation::LineActivity + activityBytes.size()) > line.size() )
      throw std::out_of_range( "GenerateLine: LineActivity destination out of bounds" );
    if( (LineParameterLocation::LineActivityUnceratinty + actUncBytes.size()) > line.size() )
      throw std::out_of_range( "GenerateLine: LineActivityUnceratinty destination out of bounds" );
    if( (LineParameterLocation::LineEfficiency + lineEff.size()) > line.size() )
      throw std::out_of_range( "GenerateLine: LineEfficiency destination out of bounds" );
    if( (LineParameterLocation::LineEfficiencyUncertainty + lnEffUnc.size()) > line.size() )
      throw std::out_of_range( "GenerateLine: LineEfficiencyUncertainty destination out of bounds" );
    if( (LineParameterLocation::LineMDA + lineMDA.size()) > line.size() )
      throw std::out_of_range( "GenerateLine: LineMDA destination out of bounds" );

    std::copy(energyBytes.begin(), energyBytes.end(), line.begin() + LineParameterLocation::Energy);
    std::copy(enUncBytes.begin(), enUncBytes.end(), line.begin() + LineParameterLocation::EnergyUncertainty);
    std::copy(yieldBytes.begin(), yieldBytes.end(), line.begin() + LineParameterLocation::Abundance);
    std::copy(yieldUncBytes.begin(), yieldUncBytes.end(), line.begin() + LineParameterLocation::AbundanceUncertainty);

    std::copy(activityBytes.begin(), activityBytes.end(), line.begin() + LineParameterLocation::LineActivity);
    std::copy(actUncBytes.begin(), actUncBytes.end(), line.begin() + LineParameterLocation::LineActivityUnceratinty);
    std::copy(lineEff.begin(), lineEff.end(), line.begin() + LineParameterLocation::LineEfficiency);
    std::copy(lnEffUnc.begin(), lnEffUnc.end(), line.begin() + LineParameterLocation::LineEfficiencyUncertainty);
    std::copy(lineMDA.begin(), lineMDA.end(), line.begin() + LineParameterLocation::LineMDA);

    // Set if it is the key line
    line[LineParameterLocation::IsKeyLine] = t_line.IsKeyLine ? 0x04 : 0x00;

    // Set the nuclide number
    line[LineParameterLocation::NuclideIndex] = static_cast<uint8_t>(t_line.NuclideIndex);

    line[LineParameterLocation::NoWeightMean] = t_line.NoWeightMean ? 0x02 : 0x00;

    return line;
}

// get the efficiency points used for curve fitting (energy, eff., eff unc.)
std::vector<EfficiencyPoint>& CAMIO::GetEfficiencyPoints() {
  if( !efficiencyPoints.empty() )
    return efficiencyPoints;

    auto range = blockAddresses.equal_range(CAMBlock::GEOM);
    if (range.first == range.second) {
        throw std::runtime_error("There is no efficiency calibration data in the loaded file");
    }

    // Clear any existing points
    efficiencyPoints.clear();

    // Read the geometry block which will populate efficiencyPoints. Keep failure transactional so
    // callers never observe a partially parsed calibration after a malformed block.
    try {
        for (auto& it = range.first; it != range.second; ++it) {
            size_t pos = it->second;
            uint16_t records = ReadUInt16(*readData, pos + 0x1E);
            ReadGeometryBlock(pos, records);
        }
    } catch (...) {
        efficiencyPoints.clear();
        throw;
    }

    return efficiencyPoints;
}

CAMIO::EfficiencyModel CAMIO::GetEfficiencyModel() const
{
  return efficiencyModel;
}

// generate a block
std::vector<byte_type> CAMIO::GenerateBlock(CAMBlock block, size_t loc,
                                         const std::vector<std::vector<byte_type>>& records,
                                         uint16_t blockNo, bool hasCommon) {

    // Return just the default for ACQP with the header
    if (block == CAMBlock::ACQP) {
        auto acqpHead = GenerateBlockHeader(block, loc);

        enter_CAM_value("PHA ", acqpCommon, 0x80, cam_type::cam_string);
        enter_CAM_value(0x04, acqpCommon, 0x88, cam_type::cam_word); //BITES
        enter_CAM_value(0x01, acqpCommon, 0x8D, cam_type::cam_word); //ROWS
        enter_CAM_value(0x01, acqpCommon, 0x91, cam_type::cam_word); //GROUPS
        enter_CAM_value(0x04, acqpCommon, 0x55, cam_type::cam_word); //BACKGNDCHNS
        acqpHead.insert(acqpHead.end(), acqpCommon.begin(), acqpCommon.end());
        // channels added in the generate header data section
        return acqpHead;
    }
    if (block == CAMBlock::PROC) {
        auto procHead = GenerateBlockHeader(block, loc);
        procHead.insert(procHead.end(), procCommon.begin(), procCommon.end());
        return procHead;
    }
    if (block == CAMBlock::SAMP)
    {
        auto sampHead = GenerateBlockHeader(block, loc);
        enter_CAM_value(1.0, sampCommon, 0x90, cam_type::cam_float);
        sampHead.insert(sampHead.end(), sampCommon.begin(), sampCommon.end());
        return sampHead;
    }
    if (block == CAMBlock::SPEC)
    {
        auto dataHead = GenerateBlockHeader(block, loc);
        uint16_t offset = ReadUInt16(dataHead, 0x28);
        dataHead.insert(dataHead.end(), offset, 0);
        //TODO padd with zeros from 0x28 of the header
        dataHead.insert(dataHead.end(), specData.begin(), specData.end());
        return dataHead;
    }

    // Check for valid entries
    //if (block != CAMBlock::NUCL && block != CAMBlock::NLINES) {
    //    throw std::runtime_error("Only blocks ACQP, PROC, NUCL and NLINES are supported");
    //}
    //if (records.empty()) {
    //    throw std::runtime_error("Records parameter cannot be null or empty");
    //}

    // Get the size of the block
    uint32_t blockSize = static_cast<uint32_t>(block == CAMBlock::NUCL ? 
                        BlockSize::NUCL : BlockSize::NLINES);

    // Build an empty container for the block
    std::vector<uint8_t> blockBytes(blockSize, 0);

    // Copy the common data only for the first block
    size_t destIndex = block_header_size;
    if (hasCommon) {
        if (block == CAMBlock::NUCL) {
            // Validate destination bounds before copying
            if( (block_header_size + nuclCommon.size()) > blockBytes.size() )
              throw std::out_of_range( "GenerateBlock: nuclCommon destination out of bounds" );

            std::copy(nuclCommon.begin(), nuclCommon.end(),
                     blockBytes.begin() + block_header_size);
            destIndex += nuclCommon.size();
        } else if (block == CAMBlock::NLINES) {
            // Validate destination bounds before copying
            if( (block_header_size + nlineCommon.size()) > blockBytes.size() )
              throw std::out_of_range( "GenerateBlock: nlineCommon destination out of bounds" );

            std::copy(nlineCommon.begin(), nlineCommon.end(),
                     blockBytes.begin() + block_header_size);
            destIndex += nlineCommon.size();
        }
    }

    // Copy in the records
    uint16_t totalRec = 0;
    uint16_t totRecLines = 0;

    auto it = records.begin();
    while (it != records.end() && destIndex + it->size() < blockSize) {

        // Validate destination bounds before copying (checking <= instead of < to be safe)
        if( (destIndex + it->size()) > blockBytes.size() )
          throw std::out_of_range( "GenerateBlock: record destination out of bounds" );

        std::copy(it->begin(), it->end(), blockBytes.begin() + destIndex);
        destIndex += it->size();
        totalRec++;

        if (block == CAMBlock::NUCL) {
            totRecLines += GetNumLines(*it);
        }
        ++it;
    }

    // Get the header
    auto header = GenerateBlockHeader(block, loc, totalRec, totRecLines, blockNo, hasCommon);

    // Validate destination bounds before copying header
    if( header.size() > blockBytes.size() )
      throw std::out_of_range( "GenerateBlock: header destination out of bounds" );

    // Copy the header to byte array
    std::copy(header.begin(), header.end(), blockBytes.begin());

    return blockBytes;
}

// generate a block header
std::vector<byte_type> CAMIO::GenerateBlockHeader(CAMBlock block, size_t loc, uint16_t numRec,
                                               uint16_t numLines, uint16_t blockNum, bool hasCommon) const {
    //if (block != CAMBlock::ACQP && block != CAMBlock::NUCL && 
    //    block != CAMBlock::NLINES && block != CAMBlock::PROC) {
    //    throw std::runtime_error("Only blocks ACQP, NUCL and NLINES are supported");
    //}

    std::vector<uint8_t> header(0x30, 0);

    // Note: the `+ 4` here assumes exactly four blocks (ACQP, SAMP, PROC, SPEC) precede the
    //  NUCL/NLINES chain, which is not true in general (SAMP, SPEC and GEOM are all optional).
    //  `CreateCAMFile()` therefore passes `blockNum == 0` and patches the real link in afterwards;
    //  see `set_next_block_link` there.
    uint16_t blockRec = blockNum >= 1 ? blockNum + 4 : 0;

    // Default values for ACQP
    std::array<uint16_t, 20> values = {
        uint16_t(0x0100),                                          // 0x04  0 Has Common block (1 =?, 5 = yes, 7 = no)
        static_cast<uint16_t>(BlockSize::ACQP),          // 0x06  1 Block size
        0x0000,                                          // 0x08  2
        0x0000,                                          // 0x0E  3
        sec_header_length,                               // 0x10  4 Section header length
        0x0000,                                          // 0x12  5
        0x0000,                                          // 0x14  6
        0x0000,                                          // 0x16  7
        0x0000,                                          // 0x18  8
        0x003C,                                          // 0x1A  9  Always 3C
        0x0000,                                          // 0x1C  10 
        numRec,                                          // 0x1E  11 number of records
        static_cast<uint16_t>(RecordSize::ACQP),         // 0x20  12 Size of record block
        0x02EA,                                          // 0x22  13 address of records
        0x01FB,                                          // 0x24  14 address of record tabular
        0x0019,                                          // 0x26  15 Always 19
        0x03E6,                                          // 0x28  16 Addresss of entries in block
        0x0009,                                          // 0x2A  17 Always 9
        0x0000,                                          // 0x2C  18
        static_cast<uint16_t>(sec_header_length + numRec * static_cast<uint16_t>(RecordSize::ACQP) + 0x02EA) // 0x2E  19 Computed size of block
    };
    std::vector<uint16_t> temp = { values[4] , values[11] , values[12] , values[13] , values[17]};
    // Modify values based on block type
    switch (block) {
      case CAMBlock::ACQP:
      case CAMBlock::ENERGY_CAL_METHOD:
      case CAMBlock::ENERGY_CAL_METHOD2:
      case CAMBlock::ANALYSIS_SEQUENCE:
      case CAMBlock::K_EDGE_CONFIG:
        // No action - we dont generate these blocks (other than ACQP), so they just get the
        //  ACQP defaults above.  Note there is intentionally no `default:` case, so that adding
        //  a new CAMBlock gives a compiler warning here.
        //  GEOM, DISP and PEAK were in this list on master, but are generated on this branch and
        //  so have their own cases below.
        break;

        case CAMBlock::DISP:
        {
            // Values from the DISP block of Genie-produced CNFs (cs137.CNF, Ba-133.cnf and
            //  example_peaks_fits.cnf), which agree on all of it.  `numRec` is the ROI count.
            values[0] = 0x0500;             // commonFlag
            values[5] = 0xF628;             // \ block-type descriptor, as for PEAK
            values[6] = 0x000C;             // /
            values[7] = 0x1200;
            values[11] = numRec;            // number of regions
            values[12] = sm_disp_rec_size;
            values[13] = sm_disp_rec_offset;
            values[14] = 0x7FFF;
            values[15] = 0x0000;
            values[16] = 0x0002;
            values[17] = 0x000C;

            const uint32_t usedSize = static_cast<uint32_t>(values[4]) +
                                       static_cast<uint32_t>(sm_disp_rec_offset) +
                                       static_cast<uint32_t>(sm_disp_rec_size) * numRec;
            const uint32_t totalSize = std::max<uint32_t>(
                                        static_cast<uint32_t>(sm_genie_disp_block_size),
                                        ((usedSize + 0x1FFu) / 0x200u) * 0x200u );
            values[1] = static_cast<uint16_t>(totalSize & 0xFFFF);
            values[2] = static_cast<uint16_t>((totalSize >> 16) & 0xFFFF);
            // Saturate rather than wrap: this field is 16 bits, and masking turned a
            //  264-peak block's used size of 65779 into 243 - a value a reader would take
            //  as a nearly-empty block.  Clamping at least keeps it monotonic.
            values[19] = static_cast<uint16_t>( std::min<uint32_t>(usedSize, 0xFFFFu) );
            break;
        }

        case CAMBlock::PEAK:
        {
            // Values taken from the PEAK block of a Genie-produced CNF; `numRec` carries the peak
            //  count.  See `GeneratePeakBlock()`.
            const uint16_t recOffsetPeak = 0x00F3;  //243
            const uint16_t recSizePeak = static_cast<uint16_t>( RecordSize::PEAK );

            values[0] = 0x0500;             // commonFlag
            values[3] = 0x1400;             // block-type flag; byte 0x0F is 0x14 for PEAK
            values[5] = 0xF890;             // \  block-type descriptor, identical in every
            values[6] = 0x000D;             //  > Genie-written PEAK block seen (cf. the NUCL,
            values[7] = 0x2400;             // /  NLINES and PROC cases below/above)
            values[11] = numRec;            // number of peak records
            values[12] = recSizePeak;
            values[13] = recOffsetPeak;
            values[14] = 0x7FFF;
            values[15] = 0x0000;
            values[16] = 0x7FFF;            // no entries
            values[17] = 0x0000;

            // Genie computes this without counting the per-record 0x01 marker byte.
            const uint32_t usedSize = static_cast<uint32_t>(values[4]) +
                                       static_cast<uint32_t>(recOffsetPeak) +
                                       static_cast<uint32_t>(recSizePeak) * numRec;
            const uint32_t totalSize = std::max<uint32_t>(
                                        static_cast<uint32_t>(sm_genie_peak_block_size),
                                        ((usedSize + 0x1FFu) / 0x200u) * 0x200u );
            values[1] = static_cast<uint16_t>(totalSize & 0xFFFF);
            values[2] = static_cast<uint16_t>((totalSize >> 16) & 0xFFFF);
            // Saturate rather than wrap: this field is 16 bits, and masking turned a
            //  264-peak block's used size of 65779 into 243 - a value a reader would take
            //  as a nearly-empty block.  Clamping at least keeps it monotonic.
            values[19] = static_cast<uint16_t>( std::min<uint32_t>(usedSize, 0xFFFFu) );
            break;
        }

        case CAMBlock::GEOM:
        {
            // numRec is always 1 (a single logical "record" holding all efficiency-point
            // entries); numLines is repurposed to carry the efficiency point count.
            // Values from a real Genie-written GEOM block (Ba-133 test file).
            const uint16_t entOffsetGeom = sm_geom_ent_offset;
            const uint16_t entSizeGeom = sm_geom_ent_size;
            const uint16_t recOffsetGeom = sm_geom_rec_offset;
            const uint16_t numPointsGeom = numLines;

            values[0] = 0x0500;          // commonFlag
            // The 32-bit value at 0x12 differs in all three real files (0x05673CA0, 0x000DCA90,
            //  0x000D5290), so it is not a class signature Genie can be checking; two of the three
            //  do agree on the high word and on 0x16, which is also the PEAK/NUCL convention, so
            //  match that much and leave the low word zero.
            values[6] = 0x000D;
            values[7] = 0x2400;
            values[11] = 1;              // numRec
            values[12] = sm_geom_rec_size;  // recSize (3842, as in a real block)
            values[13] = recOffsetGeom;  // recOffset
            values[14] = 0x7FFF;
            values[15] = 0x0000;
            values[16] = entOffsetGeom;  // entOffset
            values[17] = entSizeGeom;    // entSize

            const uint32_t usedSize = static_cast<uint32_t>(values[4]) +
                                       static_cast<uint32_t>(recOffsetGeom) +
                                       static_cast<uint32_t>(entOffsetGeom) +
                                       static_cast<uint32_t>(entSizeGeom) * numPointsGeom;
            // Pad to a 512-byte multiple, and to at least the end of the record declared above -
            //  keep this in step with `GenerateGeometryBlock()`, which allocates the buffer.
            const uint32_t recordEnd = static_cast<uint32_t>(values[4])
                                        + static_cast<uint32_t>(recOffsetGeom) + sm_geom_rec_size;
            const uint32_t totalSize = ((std::max(usedSize,recordEnd) + 0x1FF) / 0x200) * 0x200;

            // The block-size field at 0x06 is 32 bits (values[1] is its low word, values[2] its
            //  high word - see the SPEC case below); writing only the low word silently wrapped
            //  past 4080 efficiency points.
            values[1] = static_cast<uint16_t>(totalSize & 0xFFFF);
            values[2] = static_cast<uint16_t>((totalSize >> 16) & 0xFFFF);
            // Saturate rather than wrap: this field is 16 bits, and masking turned a
            //  264-peak block's used size of 65779 into 243 - a value a reader would take
            //  as a nearly-empty block.  Clamping at least keeps it monotonic.
            values[19] = static_cast<uint16_t>( std::min<uint32_t>(usedSize, 0xFFFFu) );
            break;
        }

        case CAMBlock::PROC:
            values[0] = 0x0100;
            values[1] = static_cast<uint16_t>(BlockSize::PROC);
            values[5] = 0x1C90;
            values[6] = 0x000E;
            values[7] = 0xBE00;
            values[8] = 0x0001;
            values[11] = 0x0000;
            values[12] = 0x0000;
            values[13] = 0x7FFF;
            values[14] = 0x7FFF;
            values[15] = 0x0000;
            values[16] = 0x7FFF;
            values[17] = 0x0000;
            values[19] = 0x0800;
            break;

        case CAMBlock::NUCL:
            values[0] = hasCommon ? 0x0500 : 0x0700;
            values[1] = static_cast<uint16_t>(BlockSize::NUCL);
            values[3] = 0x2800 + blockRec;
            values[5] = 0x5E90;
            values[6] = 0x0010;
            values[7] = 0x4800;
            values[12] = static_cast<uint16_t>(RecordSize::NUCL); // 0x0237
            values[13] = 0x0401; // 0x03F5;
            values[14] = 0x7FFF;
            values[15] = 0x0000;
            values[16] = 0x0239; // 0x0235;
            values[17] = 0x0003;
            values[19] = values[4] + values[11] * values[12] + 
                        (hasCommon ? values[13] : 0) + values[17] + 
                        (numLines - 1) * 3;
            break;

        case CAMBlock::NLINES:
            values[0] = hasCommon ? 0x0500 : 0x0700;
            values[1] = static_cast<uint16_t>(BlockSize::NLINES);
            values[3] = 0x2800 + blockRec;
            values[5] = 0x2290;
            values[6] = 0x0015;
            values[7] = 0x1200;
            values[12] = 0x0085;
            values[13] = 0x0018;
            values[14] = 0x7FFF;
            values[15] = 0x0000;
            values[16] = 0x7FFF;
            values[17] = 0x0000;
            values[19] = values[4] + values[11] * values[12] + 
                        (hasCommon ? values[13] : 0) + values[17];
            break;
        case CAMBlock::SAMP:
            values[0] = 0x0500;
            values[1] = static_cast<uint16_t>(BlockSize::SAMP);
            values[11] = 0x0000;
            values[12] = 0x0000;
            values[13] = 0x7FFF;
            values[14] = 0x7FFF;
            values[15] = 0x0000;
            values[16] = 0x7FFF;
            values[17] = 0x0000;
            values[19] = 0x0A00;
            break;

        case CAMBlock::SPEC:
            values[0] = 0x0500;
            values[1] = 0x0000;
            values[12] = 0x0004;
            values[11] = 0x0000;
            values[13] = 0x0000;
            values[14] = 0x0000;
            values[15] = 0x0000;
            values[16] = 0x01D0;
            values[17] = 0x0000;
            values[19] = 0x0001;

            // Round num_channels up to the next power-of-two that the CAM format uses.
            // Note: num_channels is uint32_t but values[17] is uint16_t.
            uint32_t rounded = 0x200;
            while( rounded < num_channels && rounded < 0x8000 )
              rounded <<= 1;
            if( rounded < num_channels )
              rounded = num_channels;
            values[17] = static_cast<uint16_t>( rounded & 0xFFFF );
            // The block size at file offsets 0x06-0x09 is a uint32_t in the CAM format.
            // Compute in uint32_t to avoid overflow, then split across values[1] and values[2].
            {
              const uint32_t spec_block_size = static_cast<uint32_t>(values[4])
                + static_cast<uint32_t>(values[16])
                + static_cast<uint32_t>(values[17]) * static_cast<uint32_t>(values[12]);
              values[1] = static_cast<uint16_t>(spec_block_size & 0xFFFF);
              values[2] = static_cast<uint16_t>((spec_block_size >> 16) & 0xFFFF);
            }
            break;


    }

    // Copy in the block code
    uint32_t blockCode = static_cast<uint32_t>(block);

    // Validate destination bounds before memcpy (header is fixed size 0x30)
    if( sizeof(uint32_t) > header.size() )
      throw std::out_of_range( "GenerateBlockHeader: block code destination out of bounds" );
    if( (0x0A + sizeof(uint32_t)) > header.size() )
      throw std::out_of_range( "GenerateBlockHeader: location destination out of bounds" );

    std::memcpy(header.data(), &blockCode, sizeof(uint32_t));

    // Copy in the location
    std::memcpy(header.data() + 0x0A, &loc, sizeof(uint32_t));

    // loop through the values that dosen't include the block code or location
    size_t headerIndex = 0x04;
    for (size_t i = 0; i < values.size(); i++) {
        // Skip the already written address
        if (headerIndex == 0x0A) {
            headerIndex += 0x04;
        }

        // Validate destination bounds before memcpy
        if( (headerIndex + sizeof(uint16_t)) > header.size() )
          throw std::out_of_range( "GenerateBlockHeader: value destination out of bounds at index " + std::to_string(i) + " (headerIndex=" + std::to_string(headerIndex) + ")" );

        std::memcpy(header.data() + headerIndex, &values[i], sizeof(uint16_t));
        headerIndex += 0x02;
    }

    return header;
}

// get the numbers of lines in a file
uint16_t CAMIO::GetNumLines(const std::vector<byte_type>& nuclRecord) {
    // Check if the record is large enough to contain at least the base NUCL record
    if (nuclRecord.size() < static_cast<size_t>(RecordSize::NUCL)) {
        throw std::out_of_range("There are no Lines associated with this record");
    }

    // Calculate number of lines from the record size
    // The size beyond RecordSize::NUCL is due to the lines, where each line takes 3 bytes
    return static_cast<uint16_t>((nuclRecord.size() - static_cast<size_t>(RecordSize::NUCL)) / 3);
}

// compute an estimate of the uncerainty based on the last non-zero digit
float CAMInputOutput::CAMIO::ComputeUncertainty(float value)
{
    size_t precision = 6;
    std::ostringstream oss;
    oss << std::scientific << std::setprecision(precision) << value;
    std::string val_str = oss.str();

    size_t index = 0;
    size_t exp_pos = 0;
    // Loop through the string starting at the 2nd character (after "0.")
    for (size_t i = 1; i < val_str.size(); i++) {
        char cur_char = val_str[i];
        if (cur_char == 'E' || cur_char == 'e')
        {
            exp_pos = i;
            break;
        }
        if (cur_char != '0' ) {
            index = i;
        }
    }

    // Get the power of the original number
    int power = 0;
    if (exp_pos != std::string::npos) {
        power = std::stoi(val_str.substr(exp_pos + 1));
    }

    // If index is 0, there is no precision; use 0.5
    if (index == 0) {
        index = static_cast<size_t>(power) < 0 ? 1 : static_cast<size_t>(power) + 1;
    }

    // Calculate the uncertainty
    float uncertainty = 5.0f * std::pow(10.0f, power - static_cast<int>(index));
    return uncertainty;
}

KEdgeInfo CAMIO::GetKEdgeInfo()
{
    KEdgeInfo info;

    // Read K-edge parameters from SAMP block
    auto sampRange = blockAddresses.equal_range(CAMBlock::SAMP);
    if( sampRange.first != sampRange.second )
    {
        for( auto it = sampRange.first; it != sampRange.second; ++it )
        {
            const size_t pos = it->second;
            
            // Validate we can read the header size field
            if( (pos + 0x12) > readData->size() )
                continue;
            
            const uint16_t headSize = ReadUInt16( *readData, pos + 0x10 );
            const size_t dataStart = pos + static_cast<size_t>(headSize);

            // Temperature at offset 0x22a (CAM_F_STEMP)
            const size_t tempOffset = dataStart + 0x22a;
            if( (tempOffset + 4) <= readData->size() )
            {
                const float temp = convert_from_CAM_float( *readData, tempOffset );
                // Sanity check: temperature should be in a reasonable range (-50 to +100 C)
                if( (temp > -50.0f) && (temp < 100.0f) && (temp != 0.0f) )
                {
                    info.temperature = temp;
                    info.hasInfo = true;
                }
            }

            // U-235 enrichment at offset 0xd3b (CAM_F_PRKEDDCL235)
            const size_t u235Offset = dataStart + 0xd3b;
            if( (u235Offset + 4) <= readData->size() )
            {
                const float u235 = convert_from_CAM_float( *readData, u235Offset );
                // Sanity check: enrichment should be 0-100%
                if( (u235 > 0.0f) && (u235 <= 100.0f) )
                {
                    info.u235Enrichment = u235;
                    info.hasInfo = true;
                }
            }

            // Pu atomic weight at offset 0xd3f (CAM_F_PRKEDDPUAWT)
            const size_t puOffset = dataStart + 0xd3f;
            if( (puOffset + 4) <= readData->size() )
            {
                const float puAwt = convert_from_CAM_float( *readData, puOffset );
                // Sanity check: Pu atomic weight should be reasonable (238-244 g/mol)
                if( (puAwt >= 238.0f) && (puAwt <= 244.0f) )
                {
                    info.puAtomicWeight = puAwt;
                    info.hasInfo = true;
                }
            }
        }
    }

    // Read path length from block 0x00012024 at offset 0x60 (CAM_F_KCPATHLEN)
    auto pathRange = blockAddresses.equal_range( CAMBlock::K_EDGE_CONFIG );
    if( pathRange.first != pathRange.second )
    {
        for( auto it = pathRange.first; it != pathRange.second; ++it )
        {
            const size_t pos = it->second;
            
            // Validate we can read the header size field
            if( (pos + 0x12) > readData->size() )
                continue;
            
            const uint16_t headSize = ReadUInt16( *readData, pos + 0x10 );
            const size_t pathOffset = pos + static_cast<size_t>(headSize) + 0x60;

            if( (pathOffset + 4) <= readData->size() )
            {
                const float pathLen = convert_from_CAM_float( *readData, pathOffset );
                // Sanity check: path length should be positive and reasonable (0 to 100 cm)
                if( (pathLen > 0.0f) && (pathLen < 100.0f) )
                {
                    info.pathLength = pathLen;
                    info.hasInfo = true;
                }
            }
        }
    }

    return info;
}


bool CAMIO::GetGPSData( double &latitude, double &longitude,
                        double &speed, SpecUtils::time_point_t &position_time )
{
  // EXPERIMENTAL: These offsets match what AddGPSData writes, but have NOT been
  //  validated against files produced by Canberra/Mirion Genie 2000.
  //
  // AddGPSData writes to sampCommon at:
  //   0x8D0: latitude  (cam_double, 8 bytes)
  //   0x928: longitude (cam_double, 8 bytes)
  //   0x938: speed     (cam_double, 8 bytes)
  //   0x940: position_time (cam_datetime, 8 bytes) - optional

  latitude = 0.0;
  longitude = 0.0;
  speed = 0.0;
  position_time = SpecUtils::time_point_t{};

  auto range = blockAddresses.equal_range( CAMBlock::SAMP );
  if( range.first == range.second )
    return false;

  for( auto it = range.first; it != range.second; ++it )
  {
    const size_t pos = it->second;

    if( (pos + 0x12) > readData->size() )
      continue;

    const uint16_t headSize = ReadUInt16( *readData, pos + 0x10 );
    const size_t dataStart = pos + static_cast<size_t>(headSize);

    // Check we can read latitude and longitude (the minimum for valid GPS)
    if( (dataStart + 0x8D0 + 8) > readData->size() )
      continue;
    if( (dataStart + 0x928 + 8) > readData->size() )
      continue;

    const double lat = convert_from_CAM_double( *readData, dataStart + 0x8D0 );
    const double lon = convert_from_CAM_double( *readData, dataStart + 0x928 );

    // Check if we actually have GPS data (both lat and lon should be non-zero)
    if( lat == 0.0 && lon == 0.0 )
      continue;

    // Basic sanity check on coordinates
    if( lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0 )
      continue;

    latitude = lat;
    longitude = lon;

    // Read speed if available
    if( (dataStart + 0x938 + 8) <= readData->size() )
      speed = convert_from_CAM_double( *readData, dataStart + 0x938 );

    // Read position time if available
    if( (dataStart + 0x940 + 8) <= readData->size() )
    {
      try
      {
        position_time = convert_from_CAM_datetime( *readData, dataStart + 0x940 );
      }catch( std::exception & )
      {
      }
    }

    return true;
  }

  return false;
}


uint32_t CAMIO::GetNumChannelsFromAcqp()
{
  auto range = blockAddresses.equal_range( CAMBlock::ACQP );
  if( range.first == range.second )
    return 0;

  for( auto it = range.first; it != range.second; ++it )
  {
    const size_t pos = it->second;
    const uint16_t headSize = ReadUInt16( *readData, pos + 0x10 );
    const size_t dataStart = pos + static_cast<size_t>(headSize);

    // Channel count is stored as a uint32 (cam_longword) at acqpCommon offset 0x89.
    // The Python CNFreader reads the byte at 0x8A (which is the second byte of this uint32)
    // and multiplies by 256, which is equivalent for channel counts that are multiples of 256.
    if( (dataStart + 0x89 + 4) > readData->size() )
      continue;

    uint32_t nchan = 0;
    std::memcpy( &nchan, &(*readData)[dataStart + 0x89], sizeof(uint32_t) );

    if( nchan > 0 && nchan <= 1000000 )
      return nchan;
  }

  return 0;
}


// Helper to read a null-terminated/padded string from the data buffer.
// Returns empty string if the region is all null/whitespace.
static std::string read_padded_string( const std::vector<byte_type> &data,
                                       size_t offset, size_t max_len )
{
  if( (offset + max_len) > data.size() )
    return std::string();

  // Find the actual string length (stop at first null)
  size_t len = 0;
  while( len < max_len && data[offset + len] != 0 )
    ++len;

  std::string result( reinterpret_cast<const char *>(&data[offset]), len );

  // Trim trailing whitespace
  while( !result.empty() && (result.back() == ' ' || result.back() == '\t') )
    result.pop_back();

  // Trim leading whitespace
  size_t start = 0;
  while( start < result.size() && (result[start] == ' ' || result[start] == '\t') )
    ++start;
  if( start > 0 )
    result.erase( 0, start );

  return result;
}


bool CAMIO::GetSampleStrings( std::string &sample_id,
                              std::string &sample_type,
                              std::string &sample_units,
                              std::string &sample_geometry,
                              std::string &user_name,
                              std::string &sample_desc )
{
  sample_id.clear();
  sample_type.clear();
  sample_units.clear();
  sample_geometry.clear();
  user_name.clear();
  sample_desc.clear();

  auto range = blockAddresses.equal_range( CAMBlock::SAMP );
  if( range.first == range.second )
    return false;

  for( auto it = range.first; it != range.second; ++it )
  {
    const size_t pos = it->second;

    if( (pos + 0x12) > readData->size() )
      continue;

    const uint16_t headSize = ReadUInt16( *readData, pos + 0x10 );
    const size_t dataStart = pos + static_cast<size_t>(headSize);

    // Offsets derived from the Python CNFreader (which documents the format
    //  as observed in Genie 2000 files).
    // All offsets are relative to the start of the SAMP data area (after the 0x30 header).
    // Sample title at 0x00 is already read by GetSampleTitle().
    sample_id       = read_padded_string( *readData, dataStart + 0x40, 16 );
    sample_type     = read_padded_string( *readData, dataStart + 0x80, 16 );
    sample_units    = read_padded_string( *readData, dataStart + 0x94, 16 );
    sample_geometry = read_padded_string( *readData, dataStart + 0xA4, 16 );
    user_name       = read_padded_string( *readData, dataStart + 0x2A6, 24 );
    sample_desc     = read_padded_string( *readData, dataStart + 0x33E, 256 );

    return true;
  }

  return false;
}


// assign key the line for a nuclide
void CAMInputOutput::CAMIO::AssignKeyLines()
{
    // Loop through the nucludies
    for(size_t n = 0; n < nucs.size(); n++)
    {
        // If the caller explicitly marked a key line for this nuclide, honor it rather than
        //  second-guessing them - otherwise a GUI preview and the written file can disagree.
        bool already_assigned = false;
        for( size_t l = 0; !already_assigned && (l < lines.size()); l++ )
        {
            const size_t nucIndex = static_cast<size_t>(ReadUInt16(lines[l], LineParameterLocation::NuclideIndex));
            already_assigned = ( (n == (nucIndex - uint16_t(1)))
                                 && (lines[l][LineParameterLocation::IsKeyLine] == 0x04) );
        }

        if( already_assigned )
          continue;

        size_t largestScoreIndex = 0, lastIndex = 0, numNucs = 0;
        float keySocore = 0., energy = 0.;
        // Find all the lines for that nuclide and keep track of the largest abundance
        for (size_t l = 0; l < lines.size(); l++)
        {
            // Get the lines for each of the nuclides and compute the key line score
            size_t nucIndex = static_cast<size_t>(ReadUInt16(lines[l], LineParameterLocation::NuclideIndex));
            if (n == nucIndex - uint16_t(1))
            {
                numNucs++;
                float abundance = convert_from_CAM_float(lines[l], LineParameterLocation::Abundance);
                energy = convert_from_CAM_float(lines[l], LineParameterLocation::Energy);
                float _score = energy / 1000 + abundance / 10;
                if (_score > keySocore)
                {
                    keySocore = _score;
                    // Keep this available if we need to roll back if there is an interfearence 
                    lastIndex = largestScoreIndex;
                    largestScoreIndex = l;               
                }
            }
        }
        // Check for intereferences, lines are sorted by energy, so just look forawrd and back 1
        // but don't check for single line nuclides
        if (largestScoreIndex > 0 && largestScoreIndex < lines.size() - 1 && numNucs > 1)
        {
            float lowerE = convert_from_CAM_float(lines[largestScoreIndex - 1], LineParameterLocation::Energy);
            float higherE = convert_from_CAM_float(lines[largestScoreIndex + 1], LineParameterLocation::Energy);
            float scoreE = convert_from_CAM_float(lines[largestScoreIndex], LineParameterLocation::Energy);
            if (lowerE >= (scoreE - key_line_intf_limit) || higherE <= (scoreE + key_line_intf_limit))
            {
                largestScoreIndex = lastIndex;
            }
        }

        // Set the key line 
        lines[largestScoreIndex][LineParameterLocation::IsKeyLine] = 0x04;

        // for some reason the order of the nuclides doesn't matter, this functuion will still 
        // find interferences even if a single line nuclide is added after the key line has been found
        // for a mult line nuclide - probably because it loops over all the lines. 
    }
}
} // namespace CAMInputOutput 
