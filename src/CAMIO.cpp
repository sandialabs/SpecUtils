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

#include <iomanip>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <algorithm>
#include <cstring>
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

std::vector<byte_type>
acqpCommon(static_cast<size_t>(CAMInputOutput::CAMIO::BlockSize::ACQP) - 0x30); // Initialize with zeros

std::vector<byte_type>
sampCommon(static_cast<size_t>(CAMInputOutput::CAMIO::BlockSize::SAMP ) - 0x30);

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
static void ConvertHalfLife(CAMInputOutput::Nuclide& nuc) {
    std::string unit = nuc.HalfLifeUnit;
    std::transform(unit.begin(), unit.end(), unit.begin(), ::toupper);
    unit = unit.substr(0, unit.find_first_of(' '));

    if (unit == "Y") {
        nuc.HalfLife /= 31557600;
        nuc.HalfLifeUncertainty /= 31557600;
    }
    else if (unit == "D") {
        nuc.HalfLife /= 86400;
        nuc.HalfLifeUncertainty /= 86400;
    } 
    else if (unit == "H") {
        nuc.HalfLife /= 3600;
        nuc.HalfLifeUncertainty /= 3600;
    } 
    else if (unit == "M") {
        nuc.HalfLife /= 60;
        nuc.HalfLifeUncertainty /= 60;
    } 
    else if (unit == "S") {
        // Already in seconds
    } else {
        throw std::runtime_error("Half Life Unit " + unit + " not recognized");
    }
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
    //get the total seconds between the input time and the epoch
    const date::year_month_day epoch(date::year(1970), date::month(1u), date::day(1u));
    const date::sys_days epoch_days = epoch;
    assert(epoch_days.time_since_epoch().count() == 0); //true if using unix epoch, lets see on the various systems

    const auto time_from_epoch = date::floor<std::chrono::seconds>(date_time - epoch_days);
    const int64_t sec_from_epoch = time_from_epoch.count();

    //covert to modified julian in usec
    uint64_t j_sec = (sec_from_epoch + 3506716800UL) * 10000000UL;
    bytes = to_bytes(j_sec);
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

    float val = *reinterpret_cast<float*>(bytearr);

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

    const int64_t secs = time_raw / 10000000L;
    const int64_t sec_from_epoch = secs - 3506716800L;

    answer += std::chrono::seconds(sec_from_epoch);
    answer += std::chrono::microseconds(secs % 10000000L);

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
        //convert to seconds
        span *= 3157600.0;
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

// CAMIO constructor
CAMIO::CAMIO() {
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
        size_t loc;
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
            // Add other block types as needed
            break;
        }
    }
}

// Helper function to read a uint16_t from the data buffer
static uint16_t ReadUInt16(const std::vector<byte_type>& data, size_t offset) {
    if( offset + sizeof(uint16_t) > data.size() )
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
    // Get record offset and entry offset
    uint16_t commonFlag = ReadUInt16(*readData, pos + 0x04);
    uint16_t recOffset = commonFlag == 0x700 ? 0 : ReadUInt16(*readData, pos + 0x22);
    uint16_t entOffset = ReadUInt16(*readData, pos + 0x28);
    uint16_t recSize = ReadUInt16(*readData, pos + 0x20);
    uint16_t entSize = ReadUInt16(*readData, pos + 0x2a);
    uint16_t headSize = ReadUInt16(*readData, pos + 0x10);

    if( (pos + recOffset + 222 + 8) <= readData->size() )
    {
      std::string type_str( 9, '\0');
      std::memcpy(&(type_str[0]), &(*readData)[pos + recOffset + 222], 8);
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

        // Use explicit size_t casts to avoid uint16_t overflow in offset calculation
        size_t loc = pos + static_cast<size_t>(headSize) + static_cast<size_t>(recOffset) +
                     static_cast<size_t>(entOffset) + (i * static_cast<size_t>(recSize));

        // Validate that loc is within bounds before entering loop
        if( loc >= readData->size() )
          break;

        // Loop through the entries
        // Each entry starts with a byte that matches the record number (1-based)
        while (loc < readData->size() && (*readData)[loc] == static_cast<uint8_t>(i + 1)) {
            // Validate bounds before calling convert functions
            const size_t maxParamOffset = std::max({
                static_cast<size_t>(EfficiencyPointParameterLocation::Energy) + 4,
                static_cast<size_t>(EfficiencyPointParameterLocation::Efficiency) + 4,
                static_cast<size_t>(EfficiencyPointParameterLocation::EfficiencyUncertainty) + 4
            });
            validate_bounds( *readData, loc, maxParamOffset, "ReadGeometryBlock: reading efficiency point" );

            EfficiencyPoint point{};
            point.Index = static_cast<int>(i);
            point.Energy = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(EfficiencyPointParameterLocation::Energy));
            point.Efficiency = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(EfficiencyPointParameterLocation::Efficiency));
            point.EfficiencyUncertainty = convert_from_CAM_float(*readData, loc + static_cast<uint32_t>(EfficiencyPointParameterLocation::EfficiencyUncertainty));

            efficiencyPoints.push_back(point);
            loc += entSize;
        }
    }
}

// read the lines block
void CAMIO::ReadLinesBlock(size_t pos, uint16_t records) {
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
    // Get record offset
    uint16_t commonFlag = ReadUInt16(*readData, pos + 0x04);
    uint16_t recOffset = commonFlag == 0x700 ? 0 : ReadUInt16(*readData, pos + 0x22);
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
    // Get record offset and size
    uint16_t commonFlag = ReadUInt16(*readData, pos + 0x04);
    uint16_t recOffset = commonFlag == 0x700 ? 0 : ReadUInt16(*readData, pos + 0x22);
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
            static_cast<size_t>(PeakParameterLocation::Width) + 4
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
        peak.RightChannel = peak.LeftChannel + ReadUInt32(*readData, loc + static_cast<uint32_t>(PeakParameterLocation::Width)) - 1;

        tempPeaks.push_back(peak);
    }

    // Store the peaks in a member variable or process them as needed
    peaks = std::move(tempPeaks);
}

// get all the nuclide lines
std::vector<Line>& CAMIO::GetLines() {
    auto range = blockAddresses.equal_range(CAMBlock::NLINES);
    if (range.first == range.second) {
        throw std::runtime_error("There is no nuclide line data in the loaded file");
    }

    for (auto& it = range.first; it != range.second; ++it) {
        size_t pos = it->second;

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

        uint16_t recOffset = ReadUInt16(*readData, pos + 0x04) == 0x700 ? 0 : ReadUInt16(*readData, pos + 0x22);
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
            nuc.HalfLifeUncertainty = convert_from_CAM_duration(*readData, loc + 0x89);

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

    bool secondBlock = false;

    for (auto& it = range.first; it != range.second; ++it) {
        size_t pos = it->second;

        uint16_t recOffset = ReadUInt16(*readData, pos + 0x04) == 0x700 || secondBlock ? 
                            0 : ReadUInt16(*readData, pos + 0x22);
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
        secondBlock = true;
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

    bool secondBlock = false;
    for (auto& it = range.first; it != range.second; ++it) {
        size_t pos = it->second;

        uint16_t recOffset = ReadUInt16(*readData, pos + 0x04) == 0x700 || secondBlock ?
            0 : ReadUInt16(*readData, pos + 0x22);
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

            // these are actually record parameters but we don't deal with multpule specturm in a single file
            char type_buf[9] = { 0 };
            std::memcpy(type_buf, &(*readData)[loc + 0x2DC], sizeof(type_buf) - 1);
            type_buf[8] = '\0';
            det_info.Type = std::string(type_buf);

            char mca_type_buf[25] = { 0 };
            std::memcpy(mca_type_buf, &(*readData)[loc + 0x9C], sizeof(mca_type_buf) - 1);
            mca_type_buf[24] = '\0';
            det_info.MCAType = std::string(mca_type_buf);

            char name_buf[17] = { 0 };
            std::memcpy(name_buf, &(*readData)[loc + 0x108], sizeof(name_buf) - 1);
            name_buf[16] = '\0';
            det_info.Name = std::string(name_buf);

            char sn_buf[0x9] = { 0 };
            std::memcpy(sn_buf, &(*readData)[loc + 0x1CB], sizeof(sn_buf) - 1);
            sn_buf[8] = '\0';
            det_info.SerialNo = std::string(sn_buf);
        }

        return det_info;

    }

    return det_info; // Should never reach here
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

    for (auto& it = range.first; it != range.second; ++it) {
        size_t pos = it->second;
        uint16_t headSize = ReadUInt16(*readData, pos + 0x10);
        uint16_t timeOffset = ReadUInt16(*readData, pos + 0x24);

        // Check for potential overflow in offset calculation (timeOffset is file-controlled)
        const size_t timePos = pos + static_cast<size_t>(headSize) + static_cast<size_t>(timeOffset) + 0x01;

        return convert_from_CAM_datetime(*readData, timePos);
    }

    return SpecUtils::time_point_t{}; // Should never reach here
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

        return convert_from_CAM_duration(*readData, liveTimePos);
    }

    return 0.0; // Should never reach here
}

// gets the read time in float seconds
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

        return convert_from_CAM_duration(*readData, realTimePos);
    }

    return 0.0; // Should never reach here
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
        // Use size_t to avoid uint16_t overflow in offset calculation
        const size_t eCalOffset = 0x30 + static_cast<size_t>(ReadUInt16(*readData, pos + 0x22)) + 0xDC;

      //CONSTANT or SQRT
      //std::string type_str( eCalOffset + 100 + 1, '\0');
      //std::memcpy(&(type_str[0]), &(*readData)[pos], eCalOffset + 100);
      //std::cout << "FWHM_type: '" << type_str << "'" << std::endl;

        // Validate bounds before reading calibration coefficients
        const size_t calibStart = pos + eCalOffset;
        const size_t calibSize = fileShapeCal.size() * 4;
        validate_bounds( *readData, calibStart, calibSize, "GetShapeCalibration: reading coefficients" );

        for (size_t i = 0; i < fileShapeCal.size(); i++) {
            fileShapeCal[i] = convert_from_CAM_float(*readData, pos + eCalOffset + i * 4);
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

    fileEneCal.resize(4);

    for (auto& it = range.first; it != range.second; ++it) {
        size_t pos = it->second;
        // Use size_t to avoid uint16_t overflow in offset calculation
        const size_t eCalOffset = 0x30 + static_cast<size_t>(ReadUInt16(*readData, pos + 0x22)) + 0x44;

        // Validate we can read all energy calibration coefficients (4 floats = 16 bytes)
        // eCalOffset is partially file-controlled via ReadUInt16
        const size_t calibStart = pos + eCalOffset;
        const size_t calibSize = fileEneCal.size() * 4;
        validate_bounds( *readData, calibStart, calibSize, "GetEnergyCalibration: reading calibration coefficients" );

        for (size_t i = 0; i < fileEneCal.size(); i++) {
            fileEneCal[i] = convert_from_CAM_float(*readData, pos + eCalOffset + i * 4);
        }
    }

    return fileEneCal;
}

// create a file from added data
std::vector<byte_type>& CAMIO::CreateFile() {

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
        auto specBlock = GenerateBlock(CAMBlock::SPEC, loc);
        blockList.push_back(specBlock);
        loc += static_cast<size_t>(num_channels + 0x30);
    }
    size_t startRecord = 0;
    size_t numRecords = 125;
    uint16_t blockNo = 0;

    while (startRecord < lines.size()) {
        blockNo = ((startRecord + numRecords) > lines.size()) ? 0 : blockNo + 1;
        
        std::vector<std::vector<uint8_t>> lineSubset(lines.begin() + startRecord, lines.end());
        auto block = GenerateBlock(CAMBlock::NLINES, loc, lineSubset, blockNo, startRecord == 0);
        
        numRecords = ReadUInt16(block, 0x1E);
        startRecord += numRecords;
        
        blockList.push_back(block);
        loc += static_cast<uint32_t>(BlockSize::NLINES);
    }

    // Create and place the Nucs in the blocks array
    startRecord = 0;
    numRecords = 29;
    blockNo = static_cast<uint16_t>(blockList.size() - 2);

    while (startRecord < nucs.size()) {
        blockNo = startRecord + numRecords > nucs.size() ? 0 : blockNo + 1;
        
        std::vector<std::vector<uint8_t>> nucSubset(nucs.begin() + startRecord, nucs.end());
        auto block = GenerateBlock(CAMBlock::NUCL, loc, nucSubset, blockNo, startRecord == 0);
        
        numRecords = ReadUInt16(block, 0x1E);
        startRecord += numRecords;
        
        blockList.push_back(block);
        loc += static_cast<uint32_t>(BlockSize::NUCL);
    }


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
    writebytes.resize(fileLength);

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

        // Copy the block
        uint32_t blockLoc = ReadUInt32(block, 0x0a);
        uint16_t blockSize = ReadUInt16(block, 0x06);

        // Validate source bounds - block must have at least blockSize bytes
        if( block.size() < blockSize )
          throw std::out_of_range( "GenerateFile: block size (" + std::to_string(block.size()) + ") is smaller than blockSize field (" + std::to_string(blockSize) + ")" );

        // Validate destination bounds before copying block data
        if( (blockLoc > (std::numeric_limits<size_t>::max() - blockSize)) || ((blockLoc + blockSize) > writebytes.size()) )
          throw std::out_of_range( "GenerateFile: block destination (" + std::to_string(blockLoc) + " + " + std::to_string(blockSize) + ") exceeds writebytes size (" + std::to_string(writebytes.size()) + ")" );

        std::copy(block.begin(), block.begin() + blockSize, writebytes.begin() + blockLoc);

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

    // Convert half life into seconds
    double halfLife = nuc.HalfLife;
    double halfLifeUnc = nuc.HalfLifeUncertainty;
    std::string unit = nuc.HalfLifeUnit;
    std::transform(unit.begin(), unit.end(), unit.begin(), ::toupper);

    if (unit == "Y") {
        halfLife *= 31557600;
        halfLifeUnc *= 31557600;
    } else if (unit == "D") {
        halfLife *= 86400;
        halfLifeUnc *= 86400;
    } else if (unit == "H") {
        halfLife *= 3600;
        halfLifeUnc *= 3600;
    } else if (unit == "M") {
        halfLife *= 60;
        halfLifeUnc *= 60;
    } else if (unit == "S") {
        // Already in seconds
    } else {
        throw std::runtime_error("Half Life Unit not recognized");
    }

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

    std::vector<uint8_t> nucBytes = GenerateNuclide(nuc, lineNums);
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
    const bool noWtMn, const float enUnc, const float yieldUnc, const float halfLifeUnc)

{
    // check if the input has uncertainty, if not compute it from the value
    float energyUnc = (enUnc < size_t(0)) ? ComputeUncertainty(energy) : enUnc;
    float abundanceUnc = (yieldUnc < 0) ? ComputeUncertainty(yield) : yieldUnc;
    float t12Unc = (halfLifeUnc < size_t(0)) ? ComputeUncertainty(halfLife) : halfLifeUnc;


    int nucNo = writeNuclides.size() + 1;
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
    // The key line is set later
    Line line(energy, energyUnc, yield, abundanceUnc, nucNo, false, noWtMn);

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
        for (size_t i = 0; i < coefficients.size(); i++)
        {
            enter_CAM_value(coefficients[i], acqpCommon, 0x32E + i * 0x4, cam_type::cam_float);
        }
        enter_CAM_value(coefficients.size(), acqpCommon, 0x46C, cam_type::cam_word);
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

// Add the count start time
void CAMIO::AddAcquitionTime(const SpecUtils::time_point_t& start_time)
{
    enter_CAM_value(0x01, acqpCommon, acqp_rec_tab_loc, cam_type::cam_byte);
    enter_CAM_value(start_time, acqpCommon, static_cast<size_t>(acqp_rec_tab_loc + uint16_t(0x01)), cam_type::cam_datetime);

    //set the sampling time to the aqusition start time
    enter_CAM_value(start_time, sampCommon, 0xB4, cam_type::cam_datetime);
}
// Add the real time
void CAMIO::AddRealTime(const float real_time)
{
    enter_CAM_value(real_time, acqpCommon, static_cast<size_t>(acqp_rec_tab_loc + uint16_t(0x09)), cam_type::cam_duration);
}

// Add the live time
void CAMIO::AddLiveTime(const float live_time)
{
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
    specData.resize(num_channels);
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
    uint32_t numLines = static_cast<uint32_t>(lineNums.size());
    std::vector<uint8_t> nuc(static_cast<size_t>(static_cast<uint32_t>(RecordSize::NUCL) + numLines * 3));

    // Set the number of line parameter
    nuc[0] = static_cast<uint8_t>((numLines - 1) * 3 + static_cast<uint16_t>(RecordSize::NUCL) + 0x03);

    // Set the spacer
    nuc[1] = 0x02;
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

    // Read the geometry block which will populate efficiencyPoints
    for (auto& it = range.first; it != range.second; ++it) {
        size_t pos = it->second;
        uint16_t records = ReadUInt16(*readData, pos + 0x1E);
        ReadGeometryBlock(pos, records);
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
        static_cast<uint16_t>(values[4] + values[11] * values[12] + values[13]) // 0x2E  19 Computed size of block
    }; 
    std::vector<uint16_t> temp = { values[4] , values[11] , values[12] , values[13] , values[17]};
    // Modify values based on block type
    switch (block) {
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
            //size_t num_chans = specData.size();
            if (num_channels <= 0x200)
            {
                values[17] = 0x200;
            }
            if (num_channels > 0x200 && num_channels <= 0x400)
            {
                values[17] = 0x400;
            }
            else if (num_channels > 0x400 && num_channels <= 0x800)
            {
                values[17] = 0x800;
            }
            else if (num_channels > 0x800 && num_channels <= 0x1000)
            {
                values[17] = 0x1000;
            }
            else if (num_channels > 0x1000 && num_channels <= 0x2000)
            {
                values[17] = 0x2000;
            }
            else if (num_channels > 0x2000 && num_channels <= 0x4000)
            {
                values[17] = 0x4000;
            }
            else if (num_channels > 0x4000 && num_channels <= 0x8000)
            {
                values[17] = 0x8000;
            }
            else if (num_channels > 0x8000 && num_channels <= 0x10000)
            {
                values[17] = 0x10000;
            }
            else
            {
                values[17] = num_channels;
            }
            std::vector<uint16_t> temp = { values[4] ,values[6] , values[12] , values[15], values[16] , values[17] };
            values[1] = values[4] + values[16] + values[17] * values[12];

            if (values[17] == 0x4000)
            {
                values[2] = 0x01;
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

// assign key the line for a nuclide 
void CAMInputOutput::CAMIO::AssignKeyLines()
{
    // Loop through the nucludies
    for(size_t n = 0; n < nucs.size(); n++)
    {
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
