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

extern "C" int LLVMFuzzerTestOneInput( const uint8_t *data, size_t size ) 
{
  const string datastr( (const char *)data, size );

  std::uniform_int_distribution<size_t> distribution(0,size);

  std::random_device generator; //std::random_device is a uniformly-distributed integer random number generator that produces non-deterministic random numbers.
  const size_t str1_len = distribution(generator);
  const string str_1 = datastr.substr( 0, str1_len );
  const string str_2 = datastr.substr( str1_len );

  using namespace SpecUtils;
  {
    string c1 = datastr, c2 = str_1, c3 = str_2;
    trim( c1 );
    trim( c2 );
    trim( c3 );
  }

  
  {
    trim_copy( datastr );
    trim_copy( str_1 );
    trim_copy( str_2 );
  }
  
  {
    string c1 = datastr, c2 = str_1, c3 = str_2;
    to_lower_ascii( c1 );
    to_lower_ascii( c2 );
    to_lower_ascii( c3 );
  }

  {
    to_lower_ascii_copy( datastr );
    to_lower_ascii_copy( str_1 );
    to_lower_ascii_copy( str_2 );
  }
  
  {
    string c1 = datastr, c2 = str_1, c3 = str_2;
    to_upper_ascii( c1 );
    to_upper_ascii( c2 );
    to_upper_ascii( c3 );
  }

  {
    iequals_ascii( datastr, str_1 );
    iequals_ascii( datastr, str_2 );
    iequals_ascii( str_1, str_2 );
    iequals_ascii( str_1, datastr );
    iequals_ascii( str_2, datastr );
    iequals_ascii( str_2, str_1 );
  }

  {
    contains( datastr, str_1.c_str() );
    contains( datastr, str_2.c_str() );
    contains( str_1, str_2.c_str() );
    contains( str_1, datastr.c_str() );
    contains( str_2, datastr.c_str() );
    contains( str_2, str_1.c_str() );
  }
  
  {
    icontains( datastr, str_1 );
    icontains( datastr, str_2 );
    icontains( str_1, str_2 );
    icontains( str_1, datastr );
    icontains( str_2, datastr );
    icontains( str_2, str_1 );
  }

  {
    starts_with( datastr, str_1.c_str() );
    starts_with( datastr, str_2.c_str() );
    starts_with( str_1, str_2.c_str() );
    starts_with( str_1, datastr.c_str() );
    starts_with( str_2, datastr.c_str() );
    starts_with( str_2, str_1.c_str() );
  }
  
  {
    iends_with( datastr, str_1 );
    iends_with( datastr, str_2 );
    iends_with( str_1, str_2 );
    iends_with( str_1, datastr );
    iends_with( str_2, datastr );
    iends_with( str_2, str_1 );
  }
 

  {
    string s = datastr;
    erase_any_character( s, str_1.c_str() );
    s = datastr;
    erase_any_character( s, str_2.c_str() );
    s = str_1;
    erase_any_character( s, str_2.c_str() );
    s = str_1;
    erase_any_character( s, datastr.c_str() );
    s = str_2;
    erase_any_character( s, datastr.c_str() );
    s = str_2;
    erase_any_character( s, str_1.c_str() );
  }

  {
    string c1 = datastr, c2 = str_1, c3 = str_2;
    ireplace_all( c1, c2.c_str(), c3.c_str() );
    c1 = datastr, c2 = str_1, c3 = str_2;
    ireplace_all( c1, c3.c_str(), c2.c_str() );
    c1 = datastr, c2 = str_1, c3 = str_2;
    ireplace_all( c2, c1.c_str(), c2.c_str() );
    c1 = datastr, c2 = str_1, c3 = str_2;
    ireplace_all( c2, c2.c_str(), c1.c_str() );
    c1 = datastr, c2 = str_1, c3 = str_2;
    ireplace_all( c3, c1.c_str(), c2.c_str() );
    c1 = datastr, c2 = str_1, c3 = str_2;
    ireplace_all( c3, c2.c_str(), c1.c_str() );
  }

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

  {
    std::vector<std::string> results;
    split_no_delim_compress( results, str_1, str_2.c_str() );
    split_no_delim_compress( results, str_2, str_1.c_str() );
    split_no_delim_compress( results, datastr, str_2.c_str() );
    split_no_delim_compress( results, str_2, datastr.c_str() );
    split_no_delim_compress( results, str_1, datastr.c_str() );
    split_no_delim_compress( results, datastr, str_1.c_str() );
    split_no_delim_compress( results, datastr, datastr.c_str() );
    split_no_delim_compress( results, str_1, datastr.c_str() );
    split_no_delim_compress( results, str_1, str_1.c_str() );
    split_no_delim_compress( results, str_2, str_2.c_str() );
    split_no_delim_compress( results, datastr, datastr.c_str() );
    split_no_delim_compress( results, datastr, "" );
    split_no_delim_compress( results, str_1, "" );
    split_no_delim_compress( results, str_2, "" );
  }
  
  utf8_str_len( (const char *)data, size );
  utf8_str_len( datastr.c_str(), datastr.size() );
  utf8_str_len( str_1.c_str(), str_1.size() );
  utf8_str_len( str_2.c_str(), str_2.size() );
  
  utf8_str_len( datastr.c_str() );
  utf8_str_len( str_1.c_str() );
  utf8_str_len( str_2.c_str() );
  
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

  if( size >= sizeof(float) )
  {
    float fdummy;
    parse_float( (const char *)data, size, fdummy );
    parse_float( datastr.c_str(), datastr.size(), fdummy );
    parse_float( str_1.c_str(), str_1.size(), fdummy );
    parse_float( str_2.c_str(), str_2.size(), fdummy );
  }

  if( size >= sizeof(int) )
  {
    int idummy;
    parse_int( (const char *)data, size, idummy );
    parse_int( datastr.c_str(), datastr.size(), idummy );
    parse_int( str_1.c_str(), str_1.size(), idummy );
    parse_int( str_2.c_str(), str_2.size(), idummy );
  }

  std::vector<float> fvdummy;
  split_to_floats( datastr.c_str(), fvdummy, str_1.c_str(), true );
  split_to_floats( datastr.c_str(), fvdummy, str_1.c_str(), false );
  split_to_floats( datastr.c_str(), fvdummy, str_2.c_str(), false );
  split_to_floats( datastr.c_str(), fvdummy, str_2.c_str(), true );
  split_to_floats( datastr.c_str(), fvdummy, " ,\r\n\t", true );
  split_to_floats( datastr.c_str(), fvdummy, " ,\r\n\t", false );

  split_to_floats( (const char *)data, size, fvdummy );
  split_to_floats( str_1.c_str(), str_1.size(), fvdummy );
  split_to_floats( str_2.c_str(), str_2.size(), fvdummy );

  split_to_floats( datastr, fvdummy );
  split_to_floats( str_1, fvdummy );
  split_to_floats( str_2, fvdummy );

  std::vector<int> fidummy;
  split_to_ints( (const char *)data, size, fidummy );
  split_to_ints( str_1.c_str(), str_1.size(), fidummy );
  split_to_ints( str_2.c_str(), str_2.size(), fidummy );

  std::vector<long long> flldummy;
  split_to_long_longs( (const char *)data, size, flldummy );
  split_to_long_longs( str_1.c_str(), str_1.size(), flldummy );
  split_to_long_longs( str_2.c_str(), str_2.size(), flldummy );

  convert_from_utf8_to_utf16( datastr );
  convert_from_utf8_to_utf16( str_1 );
  convert_from_utf8_to_utf16( str_2 );
  
  {
    const size_t wlen = sizeof(uint8_t) / sizeof(wchar_t);
    const wchar_t * const wbegin = (const wchar_t *)data;
    const wchar_t * const wend = wbegin + wlen;
    const wstring wdata( wbegin, wend );
    convert_from_utf16_to_utf8( wdata );
  }
  
  {
    const size_t ilen = sizeof(uint8_t) / sizeof(int);
    const int * const ibegin = (const int *)data;
    const int * const iend = ibegin + ilen;
    set<int> sdata( ibegin, iend );
    sequencesToBriefString( sdata );
  }

  
  levenshtein_distance( datastr, datastr );
  levenshtein_distance( datastr, str_1 );
  levenshtein_distance( str_1, datastr );
  levenshtein_distance( datastr, str_2 );
  levenshtein_distance( str_2, datastr );
  levenshtein_distance( str_1, str_1 );
  levenshtein_distance( str_1, str_2 );
  levenshtein_distance( str_2, str_1 );
  levenshtein_distance( str_2, str_2 );
  levenshtein_distance( datastr, "" );
  levenshtein_distance( "", datastr );
  levenshtein_distance( str_1, "" );
  levenshtein_distance( "", str_1 );
  levenshtein_distance( str_2, "" );
  levenshtein_distance( "", str_2 );
  
  
  // Filesystem functions we can test
  lexically_normalize_path( datastr );
  lexically_normalize_path( str_1 );
  lexically_normalize_path( str_2 );
  lexically_normalize_path( datastr + "../" );
  lexically_normalize_path( "/" + datastr + "../" ); //we could go wild doing various combination of path delimeiters
  
  
  likely_not_spec_file( datastr );
  likely_not_spec_file( str_1 );
  likely_not_spec_file( str_2 );
  
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
  
  file_extension( datastr );
  file_extension( str_1 );
  file_extension( str_2 );
  
  temp_file_name( datastr, datastr );
  temp_file_name( datastr, str_1 );
  temp_file_name( datastr, str_2 );
  temp_file_name( str_1, datastr );
  temp_file_name( str_2, datastr );
  temp_file_name( str_1, str_1 );
  temp_file_name( str_1, str_2 );
  temp_file_name( str_2, str_1 );
  temp_file_name( str_2, str_2 );
  
  fs_relative( datastr, datastr );
  fs_relative( datastr, str_1 );
  fs_relative( datastr, str_2 );
  fs_relative( str_1, datastr );
  fs_relative( str_2, datastr );
  fs_relative( str_1, str_1 );
  fs_relative( str_2, str_2 );
  fs_relative( str_1, str_2 );
  fs_relative( str_2, str_1 );
  fs_relative( datastr, "" );
  fs_relative( str_1, "" );
  fs_relative( str_2, "" );
  fs_relative( "", datastr );
  fs_relative( "", str_1 );
  fs_relative( "", str_2 );

  try{ filename( datastr ); }catch(const std::exception&){}
  try{ filename( str_1 ); }catch(const std::exception&){}
  try{ filename( str_2 ); }catch(const std::exception&){}

  try{ parent_path( datastr ); } catch(const std::exception&){}
  try{ parent_path( str_1 ); } catch(const std::exception&){}
  try{ parent_path( str_2 ); } catch(const std::exception&){}
  
  
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
  
  return 0;
}//LLVMFuzzerTestOneInput
