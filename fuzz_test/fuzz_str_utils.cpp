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

#include <stdint.h>
#include <stddef.h>

#include <cstdint>
#include <sstream>

#include <random>

#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/Filesystem.h"
#include "SpecUtils/DateTime.h"


using namespace std;

// A little helper to figure out what call things are crashing on.
#define PRINT_WAYPOINTS 0
 
#if( PRINT_WAYPOINTS )
#include <chrono>
#include <iomanip>
#include <iostream>

std::chrono::time_point<std::chrono::system_clock> g_start_waypoint = std::chrono::system_clock::now();
std::chrono::time_point<std::chrono::system_clock> g_last_waypoint = std::chrono::system_clock::now();
#endif

// First argument is course location (we'll increment it as we go throughout this file), and
//  the second argument is to get a little more fine grained if necessary.
void print_waypoint( int i, int j = 0 )
{
#if( PRINT_WAYPOINTS )
  if( i == 0 )
    g_start_waypoint = g_last_waypoint = std::chrono::system_clock::now();
  
  const auto current = std::chrono::system_clock::now();
  std::chrono::duration<long, std::milli> diff = std::chrono::duration_cast<std::chrono::milliseconds>( current - g_last_waypoint );
  std::chrono::duration<long, std::milli> diff_total = std::chrono::duration_cast<std::chrono::milliseconds>( current - g_start_waypoint );
  std::cout << "At - " << std::setw(2) << i;
  if( j )
    cout << "." << j;
  std::cout << " (" << std::setw(5) << diff.count() << "ms since last waypoint, "
            << std::setw(5) << diff_total.count() << "ms since start)" << std::endl;
  
  g_last_waypoint = current;
#endif
}//print_waypoint(...)


extern "C" int LLVMFuzzerTestOneInput( const uint8_t *data, size_t size ) 
{
  print_waypoint( 0 );
  
  const string datastr( (const char *)data, size );

  std::uniform_int_distribution<size_t> distribution(0,size);

  std::random_device generator; //std::random_device is a uniformly-distributed integer random number generator that produces non-deterministic random numbers.
  const size_t str1_len = distribution(generator);
  const string str_1 = datastr.substr( 0, str1_len );
  const string str_2 = datastr.substr( str1_len );

  print_waypoint( 1 );
  
  using namespace SpecUtils;
  {
    string c1 = datastr, c2 = str_1, c3 = str_2;
    trim( c1 );
    trim( c2 );
    trim( c3 );
  }

  print_waypoint( 2 );
  
  {
    trim_copy( datastr );
    trim_copy( str_1 );
    trim_copy( str_2 );
  }
  
  print_waypoint( 3 );
  
  {
    string c1 = datastr, c2 = str_1, c3 = str_2;
    to_lower_ascii( c1 );
    to_lower_ascii( c2 );
    to_lower_ascii( c3 );
  }

  print_waypoint( 4 );
  
  {
    to_lower_ascii_copy( datastr );
    to_lower_ascii_copy( str_1 );
    to_lower_ascii_copy( str_2 );
  }
  
  print_waypoint( 5 );
  
  {
    string c1 = datastr, c2 = str_1, c3 = str_2;
    to_upper_ascii( c1 );
    to_upper_ascii( c2 );
    to_upper_ascii( c3 );
  }

  print_waypoint( 6 );
  
  {
    iequals_ascii( datastr, str_1 );
    iequals_ascii( datastr, str_2 );
    iequals_ascii( str_1, str_2 );
    iequals_ascii( str_1, datastr );
    iequals_ascii( str_2, datastr );
    iequals_ascii( str_2, str_1 );
  }
  
  print_waypoint( 7 );

  {
    const size_t max_strlen = 256;
    const string str_1_short = str_1.size() < max_strlen ? str_1 : str_1.substr(0,max_strlen);
    const string str_2_short = str_2.size() < max_strlen ? str_2 : str_2.substr(0,max_strlen);
    const string datastr_short = datastr.size() < max_strlen ? datastr : datastr.substr(0,max_strlen);
    
    contains( datastr_short, str_1_short.c_str() );
    contains( datastr_short, str_2_short.c_str() );
    contains( str_1_short, str_2_short.c_str() );
    contains( str_1_short, datastr_short.c_str() );
    //contains( str_2_short, datastr.c_str() );
    contains( str_2_short, str_1_short.c_str() );
  }
  
  
  print_waypoint( 8 );
  
  {
    icontains( datastr, str_1 );
    icontains( datastr, str_2 );
    icontains( str_1, str_2 );
    icontains( str_1, datastr );
    icontains( str_2, datastr );
    icontains( str_2, str_1 );
  }
  
  print_waypoint( 9 );

  {
    starts_with( datastr, str_1.c_str() );
    starts_with( datastr, str_2.c_str() );
    starts_with( str_1, str_2.c_str() );
    starts_with( str_1, datastr.c_str() );
    starts_with( str_2, datastr.c_str() );
    starts_with( str_2, str_1.c_str() );
  }
  
  print_waypoint( 10 );
  
  {
    iends_with( datastr, str_1 );
    iends_with( datastr, str_2 );
    iends_with( str_1, str_2 );
    iends_with( str_1, datastr );
    iends_with( str_2, datastr );
    iends_with( str_2, str_1 );
  }
 
  print_waypoint( 11 );

  {
    const size_t max_delims = 10;
    const string str_1_short = str_1.size() < max_delims ? str_1 : str_1.substr(0,max_delims);
    const string str_2_short = str_2.size() < max_delims ? str_2 : str_2.substr(0,max_delims);
    const string datastr_short = datastr.size() < max_delims ? datastr : datastr.substr(0,max_delims);
    
    string s = datastr;
    erase_any_character( s, str_1_short.c_str() );
    s = datastr;
    erase_any_character( s, str_2_short.c_str() );
    s = str_1_short;
    erase_any_character( s, str_2_short.c_str() );
    s = str_1_short;
    erase_any_character( s, datastr_short.c_str() );
    s = str_2_short;
    erase_any_character( s, datastr_short.c_str() );
    s = str_2_short;
    erase_any_character( s, str_1_short.c_str() );
  }

  print_waypoint( 12 );
  
  {
    string c1 = datastr, c2 = str_1, c3 = str_2;
    ireplace_all( c1, c2.c_str(), c3.c_str() );
    c1 = datastr, c2 = str_1, c3 = str_2;
    ireplace_all( c1, c3.c_str(), c2.c_str() );
    c1 = datastr, c2 = str_1, c3 = str_2;
    ireplace_all( c2, c1.c_str(), c2.c_str() );
    //c1 = datastr, c2 = str_1, c3 = str_2;
    //ireplace_all( c2, c2.c_str(), c1.c_str() );
    //c1 = datastr, c2 = str_1, c3 = str_2;
    //ireplace_all( c3, c1.c_str(), c2.c_str() );
    //c1 = datastr, c2 = str_1, c3 = str_2;
    //ireplace_all( c3, c2.c_str(), c1.c_str() );
  }

  print_waypoint( 13 );
  
  {
    std::vector<std::string> results;
    split( results, str_1, str_2.c_str() );
    split( results, str_2, str_1.c_str() );
    split( results, datastr, str_2.c_str() );
    split( results, str_2, datastr.c_str() );
    split( results, str_1, datastr.c_str() );
    split( results, datastr, str_1.c_str() );
    split( results, datastr, datastr.c_str() );
    split( results, str_1, datastr.c_str() );
    split( results, str_1, str_1.c_str() );
    split( results, str_2, str_2.c_str() );
    split( results, datastr, datastr.c_str() );
    split( results, datastr, "" );
    split( results, str_1, "" );
    split( results, str_2, "" );
  }
  
  print_waypoint( 14 );

  {
    std::vector<std::string> results;
    
    // Having a long list of delimiters can cause things to go pretty slowly, so we'll
    //  limit things.
    const size_t max_delims = 5;
    const string str_1_short = str_1.size() < max_delims ? str_1 : str_1.substr(0,max_delims);
    const string str_2_short = str_2.size() < max_delims ? str_2 : str_2.substr(0,max_delims);
    const string datastr_short = datastr.size() < max_delims ? datastr : datastr.substr(0,max_delims);
    
    split_no_delim_compress( results, str_1, str_2_short.c_str() );
    split_no_delim_compress( results, str_2, str_1_short.c_str() );
    split_no_delim_compress( results, datastr, str_2_short.c_str() );
    //split_no_delim_compress( results, str_2, datastr_short.c_str() );
    //split_no_delim_compress( results, str_1, datastr_short.c_str() );
    split_no_delim_compress( results, datastr, str_1_short.c_str() );
    //split_no_delim_compress( results, datastr, datastr_short.c_str() );
    split_no_delim_compress( results, str_1, datastr_short.c_str() );
    //split_no_delim_compress( results, str_1, str_1_short.c_str() );
    //split_no_delim_compress( results, str_2, str_2_short.c_str() );
    split_no_delim_compress( results, datastr, datastr_short.c_str() );
    split_no_delim_compress( results, datastr, "" );
    //split_no_delim_compress( results, str_1, "" );
    //split_no_delim_compress( results, str_2, "" );
  }
  
  print_waypoint( 15 );
  
  utf8_str_len( (const char *)data, size );
  utf8_str_len( datastr.c_str(), datastr.size() );
  utf8_str_len( str_1.c_str(), str_1.size() );
  utf8_str_len( str_2.c_str(), str_2.size() );
  
  print_waypoint( 16 );
  
  utf8_str_len( datastr.c_str() );
  utf8_str_len( str_1.c_str() );
  utf8_str_len( str_2.c_str() );
  
  print_waypoint( 17 );
  
  {
    string s = datastr;
    utf8_limit_str_size( s, str1_len );
    s = datastr;
    utf8_limit_str_size( s, size );
    s = str_1;
    utf8_limit_str_size( s, str1_len );
    s = str_2;
    utf8_limit_str_size( s, str1_len );
    s = str_1;
    utf8_limit_str_size( s, size );
    s = str_2;
    utf8_limit_str_size( s, size );
  }

  print_waypoint( 18 );
  
  if( size >= sizeof(float) )
  {
    float fdummy;
    parse_float( (const char *)data, size, fdummy );
    parse_float( datastr.c_str(), datastr.size(), fdummy );
    parse_float( str_1.c_str(), str_1.size(), fdummy );
    parse_float( str_2.c_str(), str_2.size(), fdummy );
  }

  print_waypoint( 19 );
  
  if( size >= sizeof(int) )
  {
    int idummy;
    parse_int( (const char *)data, size, idummy );
    parse_int( datastr.c_str(), datastr.size(), idummy );
    parse_int( str_1.c_str(), str_1.size(), idummy );
    parse_int( str_2.c_str(), str_2.size(), idummy );
  }

  print_waypoint( 20 );
  
  {
    const size_t max_delims = 16;
    const string str_1_short = str_1.size() < max_delims ? str_1 : str_1.substr(0,max_delims);
    const string str_2_short = str_2.size() < max_delims ? str_2 : str_2.substr(0,max_delims);
    const string datastr_short = datastr.size() < max_delims ? datastr : datastr.substr(0,max_delims);
    
    std::vector<float> fvdummy;
    split_to_floats( datastr.c_str(), fvdummy, str_1_short.c_str(), true );
    split_to_floats( datastr.c_str(), fvdummy, str_1_short.c_str(), false );
    split_to_floats( datastr.c_str(), fvdummy, str_2_short.c_str(), false );
    split_to_floats( datastr.c_str(), fvdummy, str_2_short.c_str(), true );
    split_to_floats( datastr.c_str(), fvdummy, " ,\r\n\t", true );
    split_to_floats( datastr.c_str(), fvdummy, " ,\r\n\t", false );
  }
  
  print_waypoint( 21 );
    
  std::vector<float> fvdummy;
  split_to_floats( (const char *)data, size, fvdummy );
  split_to_floats( str_1.c_str(), str_1.size(), fvdummy );
  split_to_floats( str_2.c_str(), str_2.size(), fvdummy );
  
  print_waypoint( 22 );
  
  split_to_floats( datastr, fvdummy );
  split_to_floats( str_1, fvdummy );
  split_to_floats( str_2, fvdummy );
  
  
  print_waypoint( 23 );
  
  std::vector<int> fidummy;
  split_to_ints( (const char *)data, size, fidummy );
  split_to_ints( str_1.c_str(), str_1.size(), fidummy );
  split_to_ints( str_2.c_str(), str_2.size(), fidummy );
  
  print_waypoint( 24 );
  
  std::vector<long long> flldummy;
  split_to_long_longs( (const char *)data, size, flldummy );
  split_to_long_longs( str_1.c_str(), str_1.size(), flldummy );
  split_to_long_longs( str_2.c_str(), str_2.size(), flldummy );
  
  
  print_waypoint( 25 );
  
  convert_from_utf8_to_utf16( datastr );
  convert_from_utf8_to_utf16( str_1 );
  convert_from_utf8_to_utf16( str_2 );
  
  print_waypoint( 26 );
  
  {
    const size_t wlen = sizeof(uint8_t) / sizeof(wchar_t);
    const wchar_t * const wbegin = (const wchar_t *)data;
    const wchar_t * const wend = wbegin + wlen;
    const wstring wdata( wbegin, wend );
    convert_from_utf16_to_utf8( wdata );
  }
  
  print_waypoint( 27 );
  
  {
    const size_t ilen = sizeof(uint8_t) / sizeof(int);
    const int * const ibegin = (const int *)data;
    const int * const iend = ibegin + ilen;
    set<int> sdata( ibegin, iend );
    sequencesToBriefString( sdata );
  }

  print_waypoint( 28 );
  
  const size_t max_lev_str = 64;
  levenshtein_distance( datastr, datastr, max_lev_str );
  levenshtein_distance( datastr, str_1, max_lev_str );
  levenshtein_distance( str_1, datastr, max_lev_str );
  levenshtein_distance( datastr, str_2, max_lev_str );
  levenshtein_distance( str_2, datastr, max_lev_str );
  levenshtein_distance( str_1, str_1, max_lev_str );
  levenshtein_distance( str_1, str_2, max_lev_str );
  levenshtein_distance( str_2, str_1, max_lev_str );
  levenshtein_distance( str_2, str_2, max_lev_str );
  levenshtein_distance( datastr, "", max_lev_str );
  levenshtein_distance( "", datastr, max_lev_str );
  levenshtein_distance( str_1, "", max_lev_str );
  levenshtein_distance( "", str_1, max_lev_str );
  levenshtein_distance( str_2, "", max_lev_str );
  levenshtein_distance( "", str_2, max_lev_str );
  
  print_waypoint( 29 );
  
  // Filesystem functions we can test
  lexically_normalize_path( datastr );
  lexically_normalize_path( str_1 );
  lexically_normalize_path( str_2 );
  lexically_normalize_path( datastr + "../" );
  lexically_normalize_path( "/" + datastr + "../" ); //we could go wild doing various combination of path delimeiters
  
  print_waypoint( 30 );
  
  likely_not_spec_file( datastr );
  likely_not_spec_file( str_1 );
  likely_not_spec_file( str_2 );
  
  print_waypoint( 31 );
  
  append_path( datastr, datastr );
  append_path( datastr, str_1 );
  append_path( str_1, datastr );
  append_path( datastr, str_2 );
  append_path( str_2, datastr );
  append_path( str_1, str_1 );
  append_path( str_1, str_2 );
  append_path( str_2, str_1 );
  append_path( str_2, str_2 );
  append_path( str_1, "" );
  append_path( "", str_1 );
  append_path( datastr, "" );
  append_path( "", datastr );
  
  print_waypoint( 32 );
  
  try{ file_extension( datastr ); }catch( std::exception & ){ }
  try{ file_extension( str_1 ); }catch( std::exception & ){ }
  try{ file_extension( str_2 ); }catch( std::exception & ){ }
  
  print_waypoint( 33 );
  
  temp_file_name( datastr, datastr );
  temp_file_name( datastr, str_1 );
  temp_file_name( datastr, str_2 );
  temp_file_name( str_1, datastr );
  temp_file_name( str_2, datastr );
  temp_file_name( str_1, str_1 );
  temp_file_name( str_1, str_2 );
  temp_file_name( str_2, str_1 );
  temp_file_name( str_2, str_2 );
  
  print_waypoint( 34 );
  
  {
    // Having a large sizes can cause things to go pretty slowly, so we'll
    //  limit things, to 256 arbitrarily.
    //  We'll keep one full-length, and comment out a number of the possible cases
    const size_t max_fs_rel_len = 256;
    const string str_1_short = str_1.size() < max_fs_rel_len ? str_1 : str_1.substr(0,max_fs_rel_len);
    const string str_2_short = str_2.size() < max_fs_rel_len ? str_2 : str_2.substr(0,max_fs_rel_len);
    const string datastr_short = datastr.size() < max_fs_rel_len ? datastr : datastr.substr(0,max_fs_rel_len);
    
    if( datastr.size() < 1024)
      fs_relative( datastr, str_2 ); //full path
    else
      fs_relative( datastr_short, str_2_short ); //full path
    
    fs_relative( datastr_short, datastr_short );
    fs_relative( datastr_short, str_1_short );
    //fs_relative( datastr_short, str_2_short );
    fs_relative( str_1_short, datastr_short );
    //fs_relative( str_2_short, datastr_short );
    //fs_relative( str_1_short, str_1_short );
    //fs_relative( str_2_short, str_2_short );
    //fs_relative( str_1_short, str_2_short );
    fs_relative( str_2_short, str_1_short );
    fs_relative( datastr_short, "" );
    //fs_relative( str_1_short, "" );
    //fs_relative( str_2_short, "" );
    fs_relative( "", datastr_short );
    //fs_relative( "", str_1_short );
    //fs_relative( "", str_2_short );
  }

  print_waypoint( 35 );
  
  try{ filename( datastr ); }catch(const std::exception&){}
  try{ filename( str_1 ); }catch(const std::exception&){}
  try{ filename( str_2 ); }catch(const std::exception&){}

  print_waypoint( 36 );
  
  try{ parent_path( datastr ); } catch(const std::exception&){}
  try{ parent_path( str_1 ); } catch(const std::exception&){}
  try{ parent_path( str_2 ); } catch(const std::exception&){}
  
  print_waypoint( 37 );
  
  // Filesystem functions we cant really test here
  // bool remove_file( const std::string &name );
  // bool rename_file( const std::string &source, const std::string &destination );
  // int create_directory( const std::string &name );
  // static const size_t sm_recursive_ls_max_depth = 25;
  // static const size_t sm_ls_max_results = 100000;
  // typedef bool(*file_match_function_t)( const std::string &filename, void *userdata );
  // std::vector<std::string> recursive_ls( const std::string &sourcedir,
  //                                       const std::string &ending = "" );
  // std::vector<std::string> recursive_ls( const std::string &sourcedir,
  //                                       file_match_function_t match_fcn,
  //                                       void *user_data );
  // std::vector<std::string> ls_files_in_directory( const std::string &sourcedir,
  //                                                const std::string &ending = "" );
  // std::vector<std::string> ls_files_in_directory( const std::string &sourcedir,
  //                                                file_match_function_t match_fcn,
  //                                                void *match_data );
  // std::vector<std::string> ls_directories_in_directory( const std::string &src );
  // bool is_file( const std::string &name );
  // bool is_directory( const std::string &name );
  // bool can_rw_in_directory( const std::string &name );
  // size_t file_size( const std::string &path );
  // std::string temp_dir();
  // std::string get_working_path();
  // bool is_absolute_path( const std::string &path );
  // bool make_canonical_path( std::string &path, const std::string &cwd = "" );
  // void load_file_data( const char * const filename, std::vector<char> &data );
 
  
  // Date/Time functions we can test
  
  auto test_to_str = []( const SpecUtils::time_point_t dt ){
    if( SpecUtils::is_special(dt) )
      return;
    
    to_vax_string( dt );
    to_common_string( dt, true );
    to_common_string( dt, false );
    to_extended_iso_string( dt );
    SpecUtils::to_iso_string( dt );
  };//test_to_str(...)
  

  print_waypoint( 38 );
  
  {
    const size_t max_time_strlen = 96;
    const string str_1_short = str_1.size() < max_time_strlen ? str_1 : str_1.substr(0,max_time_strlen);
    const string str_2_short = str_2.size() < max_time_strlen ? str_2 : str_2.substr(0,max_time_strlen);
    const string datastr_short = datastr.size() < max_time_strlen ? datastr : datastr.substr(0,max_time_strlen);
    
    auto dt = time_from_string( datastr_short.c_str() );
    test_to_str( dt );
    dt = time_from_string( str_1_short.c_str() );
    test_to_str( dt );
    dt = time_from_string( str_2_short.c_str() );
    test_to_str( dt );
  
    print_waypoint( 39 );
    
    test_to_str( time_from_string( datastr_short, DateParseEndianType::LittleEndianFirst ) );
    //test_to_str( time_from_string( datastr_short, DateParseEndianType::LittleEndianOnly ) );
    test_to_str( time_from_string( datastr_short, DateParseEndianType::MiddleEndianFirst ) );
    //test_to_str( time_from_string( datastr_short, DateParseEndianType::MiddleEndianOnly ) );
  
    
    print_waypoint( 40 );
    
    test_to_str( time_from_string( str_1_short, DateParseEndianType::LittleEndianFirst ) );
    //test_to_str( time_from_string( str_1_short, DateParseEndianType::LittleEndianOnly ) );
    test_to_str( time_from_string( str_1_short, DateParseEndianType::MiddleEndianFirst ) );
    //test_to_str( time_from_string( str_1_short, DateParseEndianType::MiddleEndianOnly ) );
    
    print_waypoint( 41 );
    
    test_to_str( time_from_string( str_2_short, DateParseEndianType::LittleEndianFirst ) );
    //test_to_str( time_from_string( str_2_short, DateParseEndianType::LittleEndianOnly ) );
    test_to_str( time_from_string( str_2_short, DateParseEndianType::MiddleEndianFirst ) );
    //test_to_str( time_from_string( str_2_short, DateParseEndianType::MiddleEndianOnly ) );
  }
  
  print_waypoint( 42 );
  
  time_duration_string_to_seconds( datastr );
  time_duration_string_to_seconds( str_1 );
  time_duration_string_to_seconds( str_2 );
  
  print_waypoint( 43 );
  
  try{ delimited_duration_string_to_seconds( datastr ); }catch(std::exception &){ }
  try{ delimited_duration_string_to_seconds( str_1 ); }catch(std::exception &){ }
  try{ delimited_duration_string_to_seconds( str_2 ); }catch(std::exception &){ }
  
  print_waypoint( 44 );
  
  // Test for Parse Utils
  {
    const size_t max_len = std::max( size_t(1), distribution(generator));
    string dummy;
    stringstream strm( datastr ), strm_1( str_1 ), strm_2( str_2 );
    while( safe_get_line( strm, dummy, max_len) ){}
    while( safe_get_line( strm_1, dummy, max_len) ){}
    while( safe_get_line( strm_2, dummy, max_len) ){}
  }
  
  print_waypoint( 45 );
  
  {
    double lat, lon;
    
    const size_t max_deg_strlen = 64;
    const string str_1_short = str_1.size() < max_deg_strlen ? str_1 : str_1.substr(0,max_deg_strlen);
    const string str_2_short = str_2.size() < max_deg_strlen ? str_2 : str_2.substr(0,max_deg_strlen);
    const string datastr_short = datastr.size() < max_deg_strlen ? datastr : datastr.substr(0,max_deg_strlen);
    
    parse_deg_min_sec_lat_lon( datastr_short.c_str(), datastr_short.size(), lat, lon );
    parse_deg_min_sec_lat_lon( str_1_short.c_str(), str_1_short.size(), lat, lon );
    parse_deg_min_sec_lat_lon( str_2_short.c_str(), str_2_short.size(), lat, lon );
  }
  
  print_waypoint( 46 );
  
  conventional_lat_or_long_str_to_flt( datastr );
  conventional_lat_or_long_str_to_flt( str_1 );
  conventional_lat_or_long_str_to_flt( str_2 );
  
  print_waypoint( 47 );
  
  sample_num_from_remark( datastr );
  sample_num_from_remark( str_1 );
  sample_num_from_remark( str_2 );
  
  print_waypoint( 48 );
  
  try{ speed_from_remark( datastr ); }catch(std::exception &){}
  try{ speed_from_remark( str_1 ); }catch(std::exception &){}
  try{ speed_from_remark( str_2 ); }catch(std::exception &){}
  
  print_waypoint( 49 );
  
  detector_name_from_remark( datastr );
  detector_name_from_remark( str_1 );
  detector_name_from_remark( str_2 );
  
  print_waypoint( 50 );
  
  try{ dx_from_remark( datastr ); }catch(std::exception &){}
  try{ dx_from_remark( str_1 ); }catch(std::exception &){}
  try{ dx_from_remark( str_2 ); }catch(std::exception &){}
  
  print_waypoint( 51 );
  
  try{ dy_from_remark( datastr ); }catch(std::exception &){}
  try{ dy_from_remark( str_1 ); }catch(std::exception &){}
  try{ dy_from_remark( str_2 ); }catch(std::exception &){}
  
  print_waypoint( 52 );
  
  try{ dose_units_usvPerH( (const char *)data, size ); }catch(std::exception &){}
  //dose_units_usvPerH( datastr.c_str(), datastr.size() );
  //dose_units_usvPerH( str_1.c_str(), str_1.size() );
  //dose_units_usvPerH( str_2.c_str(), str_2.size() );
  
  print_waypoint( 53 );
  
  convert_n42_instrument_type_from_2006_to_2012( datastr );
  convert_n42_instrument_type_from_2006_to_2012( str_1 );
  convert_n42_instrument_type_from_2006_to_2012( str_1 );
  
  print_waypoint( 54 );
  
  // ParseUtils functions it may not make total sense to test.
  //void expand_counted_zeros( const std::vector<float> &data, std::vector<float> &results );
  //void compress_to_counted_zeros( const std::vector<float> &data, std::vector<float> &results );
  //bool valid_latitude( const double latitude );
  //bool valid_longitude( const double longitude );
  //std::istream &read_binary_data( std::istream &input, T &val );
  //size_t write_binary_data( std::ostream &input, const T &val );
  
  
  return 0;
}//LLVMFuzzerTestOneInput
