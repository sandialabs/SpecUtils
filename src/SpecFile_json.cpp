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

#include "SpecUtils_config.h"

#include <vector>
#include <string>
#include <cstring>
#include <fstream>
#include <assert.h>
#include <iostream>
#include <stdexcept>

// Using nlohmann/json.hpp adds about 62 kb to executable size, over some 
//  really niave string-based searching/parsing.
#define USE_NLOHMANN_JSON_IMP 0

#if( USE_NLOHMANN_JSON_IMP )
#include "3rdparty/nlohmann/json.hpp"
#endif

#include "SpecUtils/SpecFile.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/Filesystem.h"

using namespace std;



namespace SpecUtils
{
bool SpecFile::load_json_file( const std::string &filename )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  reset();
  
  //Only seend pretty small ones, therefor limit to 5 MB, JIC
  if( SpecUtils::file_size(filename) > 5*1024*1024 )
    return false;
  
#ifdef _WIN32
  ifstream file( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream file( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  if( !file.is_open() )
    return false;
  const bool loaded = load_from_json( file );
  if( loaded )
    filename_ = filename;
  
  return loaded;
}//bool load_aram_file( const std::string &filename )
  
  
bool SpecFile::load_from_json( std::istream &input )
{
  // Windows release executable size:
  //  This function empty:     2460160 bytes
  //  Minimal implementation:  2468864 bytes (e.g., +8.5 kb)
  //  nlohmann based imp.      2532864 bytes (e.g., +71 kb over no imp)
  if( !input )
    return false;
  
  const istream::iostate origexceptions = input.exceptions();
  input.exceptions( istream::goodbit );  //make so stream never throws
  const istream::pos_type start_pos = input.tellg();
  input.unsetf(ios::skipws);
  
  // Determine stream size
  input.seekg( 0, ios::end );
  const size_t file_size = static_cast<size_t>( input.tellg() - start_pos );
  input.seekg( start_pos );
  
  //I've only seen pretty small json files, so assume if over 5MB, not a JSON
  if ((file_size > 5 * 1024 * 1024) || (file_size < 1024))
  {
    input.exceptions(origexceptions);
    return false;
  }


  try
  {
    string filedata;

    // We'll read in the first little bit to check if it looks like json
    filedata.resize(64);
    if (!input.read(&(filedata[0]), 64))
      throw runtime_error("Failed to read first 64 bytes.");

    size_t pos = filedata.find("{");
    if (pos > 8)
      return false;

#if( USE_NLOHMANN_JSON_IMP )
    using json = nlohmann::json;
    input.seekg(start_pos);
    json data = json::parse(input);
    input.exceptions(origexceptions);

    vector<string> warnings; //currently unused
    string comment, serial_number;
    if (data.count("comment"))
      comment = data["comment"];
    if (data.count("serial_number"))
      serial_number = data["serial_number"];
    const auto &bank_0 = data["rates"]["user"]["bank_0"];
    float run_time = bank_0["run_time"];
    float dead_time = bank_0["dead_time"];
    auto counts = make_shared<vector<float>>();
    *counts = data["histo"]["registers"].get<std::vector<float>>();
#else

    // Now we'll read in the whole file
    filedata.resize(file_size + 1);

    assert(file_size > 64);
    input.read(&(filedata[64]), static_cast<streamsize>(file_size - 64));
    filedata[file_size] = 0; //jic.

    input.exceptions(origexceptions);

    auto skip_seperators = [](const char* pos) -> const char* {
      if (!pos)
        return pos;

      const string whitespace = " \t:\n\r";
      while ((*pos) && (whitespace.find(*pos) != string::npos))
        pos += 1;
      return pos;
    };


    // Lamda to find a substring in `filedata` that is starting at or after specied location.
    //  Throws exception if cant be found, or is further away than allowed.
    const auto find_after = [&filedata](const std::string& searchstr, 
                                  const size_t start_pos, const size_t max_dist) -> size_t {
        if (start_pos >= filedata.size())
          throw std::exception();
        const size_t pos = filedata.find(searchstr, start_pos);
        if( (pos == string::npos) || ((pos - start_pos) > (max_dist + searchstr.size())) )
          throw std::exception(); //throw runtime_error("Didnt find " + searchstr);
        return pos;
    };//find_after

    auto str_value = [&filedata,&find_after](const string& key, const size_t start_pos, const size_t max_dist) -> string {
      size_t pos = find_after(key, start_pos, max_dist);
      if (pos == string::npos)
        throw std::exception(); //throw runtime_error("No '" + key + "' key.");
      pos += key.size();

      const string whitespace = " \t:\n\r";
      while ((pos < filedata.size()) && (whitespace.find(filedata[pos]) != string::npos))
        pos += 1;

      if ( (pos >= (filedata.size() - 2)) || (filedata[pos] != '\"'))
        throw std::exception(); //throw runtime_error("No value for '" + key + "'.");
      
      pos += 1;
      const size_t end_quot_pos = filedata.find('\"', pos);
      if (end_quot_pos == string::npos)
        throw std::exception(); //throw runtime_error("String for value '" + key + "' didnt end.");
      return filedata.substr(pos, end_quot_pos - pos);
    };//str_value

    auto float_value = [&filedata, &find_after](const string& key, const size_t start_pos, const size_t max_dist) -> float {
      size_t pos = find_after(key, start_pos, max_dist);
      if (pos == string::npos)
        throw std::exception(); //throw runtime_error("No '" + key + "' key.");
      pos += key.size();

      const string whitespace = " \t:\n\r";
      while ((pos < filedata.size()) && (whitespace.find(filedata[pos]) != string::npos))
        pos += 1;

      if( pos >= filedata.size() )
        throw std::exception();

      const size_t end_pos = filedata.find_first_of(",]}\"\n\r", pos);
      if (end_pos == string::npos)
        throw std::exception(); //throw runtime_error("No flt value '" + key + "' key.");

      float value;
      if (!parse_float(&(filedata[pos]), end_pos - pos, value))
        throw std::exception(); //throw runtime_error("Invalid flt value for '" + key + "'");
      
      return value;
    };//float_value
    
    vector<string> warnings;
    string comment, serial_number;
    try
    {
      comment = str_value("\"comment\"", 0, filedata.size());
    }catch (std::exception& e)
    {
      //cerr << "Didnt find comment: " << e.what() << endl;
    }
    
    try
    {
      serial_number = str_value("\"serial_number\"", 0, filedata.size());
    }
    catch (std::exception& e)
    {
      //cerr << "Didnt find serial_number: " << e.what() << endl;
    }

    
    float run_time = 0.0f, dead_time = 0.0f;

    try
    {
      size_t pos = 0;
      while (true)
      {
        pos = find_after("\"user\"", pos, filedata.size());
        try
        {
          size_t bank_0_pos = find_after("\"bank_0\"", pos, 16);
          run_time = float_value("\"run_time\"", bank_0_pos, 64);
          dead_time = float_value("\"dead_time\"", bank_0_pos, 128);
          break;
        }
        catch (std::exception &e)
        {
          //cerr << "(1) Didnt get run/dead time: " << e.what() << endl;
          pos += 6;
        }
      }
    }
    catch (std::exception& e)
    {
      //cerr << "(2) Didnt get run/dead time: " << e.what() << endl;
    }
    
    
    pos = 0;
    pos = find_after("\"histo\"", pos, filedata.size() );
    pos = find_after("\"registers\"", pos, 16);
    const size_t counts_start = find_after("[", pos, 16);
    const size_t counts_end = find_after("]", counts_start, filedata.size() );
    
    const char* const begin_counts = &(filedata[counts_start + 1]);
    const char* const end_counts = &(filedata[counts_end]);
    
    auto counts = make_shared<vector<float>>();
    const bool read_all_counts = split_to_floats(begin_counts, counts_end - counts_start - 1, *counts);
    if (counts->size() < 16)
      throw runtime_error("Failed to read channel counts.");
    if (!read_all_counts)
      warnings.push_back("All channel data may not have been read in");
#endif   
    
    if (run_time < std::numeric_limits<float>::epsilon())
    {
      run_time = 0.0f;
      warnings.push_back("Didnt find realtime value");
    }

    if (dead_time < std::numeric_limits<float>::epsilon())
    {
      dead_time = 0.0f;
      warnings.push_back("Didnt find deadtime");
    }

    if (dead_time > run_time)
    {
      run_time = dead_time = 0.0f;
      warnings.push_back("Deadtime was larger than realtime, setting both to zero.");
    }

    auto meas = make_shared<Measurement>();

    meas->real_time_ = run_time;
    meas->live_time_ = run_time - dead_time;
    meas->gamma_counts_ = counts;
    //meas->gamma_count_sum_ = std::accumulate(begin(*counts), end(*counts), 0.0);
    meas->gamma_count_sum_ = 0.0;
    for (const float& v : *counts)
      meas->gamma_count_sum_ += v;

    if( !comment.empty() )
      remarks_.push_back(comment);
    
    if (!warnings.empty())
      meas->parse_warnings_.insert(end(meas->parse_warnings_), begin(warnings), end(warnings));

    measurements_.push_back(meas);
    instrument_type_ = "Gamma Handheld";
    manufacturer_ = "Bridgeport Instruments";
    instrument_model_ = "eMorpho";
    instrument_id_ = serial_number;

    cleanup_after_load();
    
    return true;
  }catch( std::exception & )
  {
    // cerr << "Failed to parse: " << e.what() << endl;
    reset();
    input.seekg( start_pos );
    input.clear();
    input.exceptions(origexceptions);
  }//try / catch
  
  return false;
}//bool load_from_aram( std::istream &input )
}//namespace SpecUtils





