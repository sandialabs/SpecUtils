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

#include <limits>
#include <locale>
#include <string>
#include <cstring>
#include <sstream>
#include <stdint.h>
#include <algorithm>


#if(PERFORM_DEVELOPER_CHECKS)
#include <boost/algorithm/string.hpp>
#endif

#include "rapidxml/rapidxml.hpp"

#include "SpecUtils/StringAlgo.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
//Pull in WideCharToMultiByte(...), etc from Windows
#define NOMINMAX
#include <windows.h>

#undef min
#undef max
#endif

static_assert( (SpecUtils_USE_FAST_FLOAT + SpecUtils_USE_FROM_CHARS + SpecUtils_USE_BOOST_SPIRIT + SpecUtils_USE_STRTOD) == 1,
              "You must have exactly one of the following turned on: SpecUtils_USE_FAST_FLOAT, "
              "SpecUtils_USE_FROM_CHARS, SpecUtils_USE_BOOST_SPIRIT, SpecUtils_USE_STRTOD" );

#if( SpecUtils_USE_FROM_CHARS )
// LLVM 14, MSVC >= 2019, and gcc 12 seem to support floating point from_char, but Apple LLVM requires
// minimum deployment targets of macOS 13.3, and iOS 16, and I'm unsure about Android status.
// With MSVC 2019, from_chars is about 50% slower than boost; fast_float is just a hair slower than boost.
//  I dont know if this is inherent, because of me doing something stupid (likely), or just that boost 
//  is really hard to beat.
//  The test was over 259924 lines, and a total of 48776423 bytes; repeated 100 times, with two runs each way.
// Windows MSVC 2019:
//   Boost: 43175 ms, 43800 ms  (i.e., ~105 MB/s)
//   from_chars : 67889 ms, 66024 ms (i.e., ~70 MB/s)
//   fast_float (3.5.1): 47113 ms, 47724 ms
// Windows MSVC 2022 was also tested with similar-ish results as 2019.
// 
//  This is not near the ~1 GB/s fast_float benchmarks list (and much closer to the strod speeds fast_float benchmark gets),
//  so perhaps I'm doing something really wrong.
  #include <charconv>

#elif( SpecUtils_USE_FAST_FLOAT )
  // https://github.com/fastfloat/fast_float is nearly a drop-in replacement for std::from_chars, and and just about as fast as boost::spirit
  #include "fast_float.h"
#elif( SpecUtils_USE_BOOST_SPIRIT )
  #include <boost/version.hpp>

  #if( BOOST_VERSION < 104500 )
    #include <boost/config/warning_disable.hpp>
    #include <boost/spirit/include/qi.hpp>
    #include <boost/spirit/include/phoenix_core.hpp>
    #include <boost/spirit/include/phoenix_operator.hpp>
    #include <boost/spirit/include/phoenix_stl.hpp>
  #endif

  #include <boost/fusion/adapted.hpp>
  #include <boost/spirit/include/qi.hpp>
#elif( SpecUtils_USE_STRTOD )
  #include <cstdlib>
#endif




//#include <boost/config.hpp>
//BOOST_NO_CXX11_HDR_CODECVT
#ifndef _WIN32
#if( defined(__clang__) || !defined(__GNUC__) || __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ > 8) )
#define HAS_STD_CODECVT 1
#else
#define HAS_STD_CODECVT 0
#endif

//GCC 4.8 doesnt have codecvt, so we'll use boost for utf8<-->utf16
#if( HAS_STD_CODECVT )
#include <codecvt>
#else
#include <boost/locale/encoding_utf.hpp>
#endif
#endif //#ifndef _WIN32


using namespace std;

namespace
{
  //adapted from http://stackoverflow.com/questions/3152241/case-insensitive-stdstring-find
  template<typename charT>
  struct char_iequal
  {
    char_iequal( const std::locale &loc ) : m_loc(loc) {}
    bool operator()(charT ch1, charT ch2) {
      return std::toupper(ch1, m_loc) == std::toupper(ch2, m_loc);
    }
  private:
    const std::locale &m_loc;
  };
  
  //The below may not be intrinsic friendly; might be interesting to look and
  // see if we could get better machine code by making intrinsic friendly (maybe
  // even have the float parsing functions inline)
  inline bool is_in( const char val, const char *delim )
  {
    while( *delim )
    {
      if( val == *delim )
        return true;
      ++delim;
    }
    return false;
  }
  
  inline const char *next_word( const char *input, const char *delim )
  {
    while( *input && is_in(*input, delim) )
      ++input;
    return input;
  }
  
  inline const char *next_word( const char *input, const char * const end, const char * const delim )
  {
    while( (input < end) && is_in(*input, delim) )
      ++input;
    return input;
  }
  
  inline const char *end_of_word( const char *input, const char *delim )
  {
    while( *input && !is_in(*input, delim) )
      ++input;
    return input;
  }
  
  inline const char *end_of_word( const char *input, const char * const end, const char * const delim )
  {
    while( (input < end) && !is_in(*input, delim) )
      ++input;
    return input;
  }
  
#if( SpecUtils_USE_STRTOD )
  bool split_to_floats_strtod( const char *input, const size_t length, 
                              const char * const delims,
                              const bool cambio_fix,
                              vector<float> &results )
  {
    const char * const end = input + length;
    
    const char *pos = next_word( input, end, delims );
    
    while( pos < end )
    {
      const char d = *pos;
      if( !isdigit(d) && (d != '+') && (d != '-') && (d != '.') )
        return false;
      
      const char * const word_end = end_of_word( pos, end, delims );
      
      try
      {
        double dvalue = stod( string(pos, (word_end - pos)) );
        
        if( cambio_fix && (dvalue == 0.0) )
        {
          const char nextchar[2] = { *(pos+1), '\0' };
          if( !strstr(delims, nextchar) )  //If the next char is a delimiter, then we dont want to apply the fix, otherwise apply the fix
            dvalue = std::numeric_limits<float>::min();
        }//if( value == 0.0 )
        
        results.push_back( static_cast<float>(dvalue) );
      }catch( std::exception & )
      {
        return false;
      }
      
      pos = next_word( word_end, end, delims );
    }//while( pos < end )
    
    return true;
  }//split_to_floats_strtod(...)
#endif //SpecUtils_USE_STRTOD
}//namespace

namespace SpecUtils
{
  bool istarts_with( const std::string &line, const char *label )
  {
    const size_t len1 = line.size();
    const size_t len2 = strlen(label);
    
    if( (len1 < len2) || !len2 )
      return false;
    
    const bool answer = ::rapidxml::internal::compare( line.c_str(), len2, label, len2, false );
    
#if(PERFORM_DEVELOPER_CHECKS)
    const bool correctAnswer = boost::algorithm::istarts_with( line, label );
    
    if( (answer != correctAnswer) && len2 )
    {
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg),
               "Got %i when should have got %i for label '%s' and string '%s'",
               int(answer), int(correctAnswer), label, line.c_str() );
      log_developer_error( __func__, errormsg );
    }//if( answer != correctAnswer )
#endif
    
    return answer;
  }//istarts_with(...)
  
  bool istarts_with( const std::string &line, const std::string &label )
  {
    const size_t len1 = line.size();
    const size_t len2 = label.size();
    
    if( (len1 < len2) || !len2 )
      return false;
    
    const bool answer = ::rapidxml::internal::compare( line.c_str(), len2, label.c_str(), len2, false );
    
#if(PERFORM_DEVELOPER_CHECKS)
    const bool correctAnswer = boost::algorithm::istarts_with( line, label );
    
    if( (answer != correctAnswer) && len2 )
    {
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg),
               "Got %i when should have got %i for label '%s' and string '%s'",
               int(answer), int(correctAnswer), label.c_str(), line.c_str() );
      log_developer_error( __func__, errormsg );
    }//if( answer != correctAnswer )
#endif
    
    return answer;
  }//istarts_with(...)
  
  
  bool starts_with( const std::string &line, const char *label )
  {
    const size_t len1 = line.size();
    const size_t len2 = strlen(label);
    
    if( (len1 < len2) || !len2 )
      return false;
    
    const bool answer = ::rapidxml::internal::compare( line.c_str(), len2, label, len2, true );
    
#if(PERFORM_DEVELOPER_CHECKS)
    const bool correctAnswer = boost::algorithm::starts_with( line, label );
    
    if( (answer != correctAnswer) && len2 )
    {
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg),
               "Got %i when should have got %i for label '%s' and string '%s'",
               int(answer), int(correctAnswer), label, line.c_str() );
      log_developer_error( __func__, errormsg );
    }//if( answer != correctAnswer )
#endif
    
    return answer;
  }//istarts_with(...)
  
  
  bool iends_with( const std::string &line, const std::string &label )
  {
    const size_t len1 = line.size();
    const size_t len2 = label.size();
    
    if( (len1 < len2) || !len2 )
      return false;
    
    const char * const lineend = line.c_str() + (len1 - len2);
    
    const bool answer = ::rapidxml::internal::compare( lineend, len2, label.c_str(), len2, false );
    
#if(PERFORM_DEVELOPER_CHECKS)
    const bool correctAnswer = boost::algorithm::iends_with( line, label );
    
    if( answer != correctAnswer && !label.empty() )
    {
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg),
               "Got %i when should have got %i for label '%s' and string '%s'",
               int(answer), int(correctAnswer), label.c_str(), line.c_str() );
      log_developer_error( __func__, errormsg );
    }//if( answer != correctAnswer )
#endif
    
    return answer;
  }//bool iends_with( const std::string &line, const std::string &label )
  
  
  size_t ifind_substr_ascii( const std::string &line, const char * const label )
  {
    const size_t len1 = line.size();
    const size_t len2 = strlen(label);
    
    if( (len1 < len2) || !len2 )
      return string::npos;
    
    const auto case_insens_comp = []( const char &lhs, const char &rhs ) -> bool {
      return (::rapidxml::internal::lookup_tables<0>::lookup_upcase[static_cast<unsigned char>(lhs)]
          == ::rapidxml::internal::lookup_tables<0>::lookup_upcase[static_cast<unsigned char>(rhs)]);
    };
    
    const char * const in_begin = line.c_str();
    const char * const in_end = in_begin + len1;
    const char * const pos = std::search( in_begin, in_end, label, label + len2, case_insens_comp );
    
    if( pos == in_end )
      return string::npos;
    return pos - in_begin;
  }//size_t ifind_substr_ascii( const std::string &input, const char * const substr );
  
  
  void erase_any_character( std::string &line, const char *chars_to_remove )
  {
    if( !chars_to_remove )
      return;
    
    auto should_remove = [chars_to_remove]( const char &c ) -> bool {
      for( const char *p = chars_to_remove; *p; ++p )
        if( c == *p )
          return true;
      return false;
    };//should_remove
    
    line.erase( std::remove_if(line.begin(), line.end(), should_remove), line.end() );
  }//void erase_any_character( std::string &line, const char *chars_to_remove );
  
  
  bool icontains( const char *line, const size_t length,
                 const char *label, const size_t labellen )
  {
    if( !length || !labellen )
      return false;
    
    const char *start = line;
    const char *end = start + length;
    const char *it = std::search( start, end, label, label+labellen,
                                 char_iequal<char>(std::locale()) );
    const bool answer = (it != end);
    
#if(PERFORM_DEVELOPER_CHECKS)
    const string cppstr = string( start, end );
    const string cpplabel = string( label, label + labellen );
    const bool correctAnswer = boost::algorithm::icontains( cppstr, cpplabel );
    
    if( answer != correctAnswer )
    {
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg),
               "Got %i when should have got %i for label '%s' and string '%s'",
               int(answer), int(correctAnswer), cpplabel.c_str(), cppstr.c_str() );
      log_developer_error( __func__, errormsg );
    }//if( answer != correctAnswer )
#endif
    
    return answer;
  }//icontains(...)
  
  bool icontains( const std::string &line, const char *label )
  {
    const size_t labellen = strlen(label);
    return icontains( line.c_str(), line.size(), label, labellen );
  }//icontains(...)
  
  
  bool icontains( const std::string &line, const std::string &label )
  {
    return icontains( line.c_str(), line.size(), label.c_str(), label.size() );
  }//icontains(...)
  
  
  bool contains( const std::string &line, const char *label )
  {
    if( !label || !strlen(label) )
      return false;
    
    const bool answer = (line.find(label) != string::npos);
    
#if(PERFORM_DEVELOPER_CHECKS)
    const bool correctAnswer = boost::algorithm::contains( line, label );
    
    if( answer != correctAnswer )
    {
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg),
               "Got %i when should have got %i for label '%s' and string '%s'",
               int(answer), int(correctAnswer), label, line.c_str() );
      log_developer_error( __func__, errormsg );
    }//if( answer != correctAnswer )
#endif
    
    return answer;
  }//contains(...)
  
  bool iequals_ascii( const char *str, const char *test )
  {
    const bool answer = ::rapidxml::internal::compare( str, strlen(str), test, strlen(test), false );
    
#if(PERFORM_DEVELOPER_CHECKS)
    const bool correctAnswer = boost::algorithm::iequals( str, test );
    
    if( answer != correctAnswer )
    {
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg),
               "Got %i when should have got %i for label '%s' and string '%s'",
               int(answer), int(correctAnswer), test, str );
      log_developer_error( __func__, errormsg );
    }//if( answer != correctAnswer )
#endif
    
    return answer;
  }//bool iequals_ascii
  
  bool iequals_ascii( const std::string &str, const char *test )
  {
    const bool answer = ::rapidxml::internal::compare( str.c_str(), str.length(), test, strlen(test), false );
    
#if(PERFORM_DEVELOPER_CHECKS)
    const bool correctAnswer = boost::algorithm::iequals( str, test );
    
    if( answer != correctAnswer )
    {
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg),
               "Got %i when should have got %i for label '%s' and string '%s'",
               int(answer), int(correctAnswer), test, str.c_str() );
      log_developer_error( __func__, errormsg );
    }//if( answer != correctAnswer )
#endif
    
    return answer;
  }
  
  bool iequals_ascii( const std::string &str, const std::string &test )
  {
    const bool answer = ::rapidxml::internal::compare( str.c_str(), str.size(), test.c_str(), test.size(), false );
    
#if(PERFORM_DEVELOPER_CHECKS)
    const bool correctAnswer = boost::algorithm::iequals( str, test );
    
    if( answer != correctAnswer )
    {
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg),
               "Got %i when should have got %i for label '%s' and string '%s'",
               int(answer), int(correctAnswer), test.c_str(), str.c_str() );
      log_developer_error( __func__, errormsg );
    }//if( answer != correctAnswer )
#endif
    
    return answer;
  }
  
#if( defined(_WIN32) && _DEBUG )
  //I get the MSVC debug runtime asserts on for character values less than -1, which since chars are signed by default
  //  happens on unicode characters.  So I'll make a crappy workaround
  // Note whitespaces are considered (space \n \r \t)
  //  https://social.msdn.microsoft.com/Forums/vstudio/en-US/d57d4078-1fab-44e3-b821-40763b119be0/assertion-in-isctypec?forum=vcgeneral
  bool not_whitespace( char c )
  {
    return !rapidxml::internal::lookup_tables<0>::lookup_whitespace[static_cast<unsigned char>(c)];
  }
#endif
  
  
  // trim from start
  static inline std::string &ltrim(std::string &s)
  {
#if( defined(_WIN32) && _DEBUG )
    s.erase( s.begin(), std::find_if( s.begin(), s.end(), &not_whitespace ) );
#else
    s.erase( s.begin(), std::find_if( s.begin(), s.end(), [](int val)->bool { return !std::isspace(val); } ) );
#endif
    
    if( s.size() )
    {
      const size_t pos = s.find_first_not_of( '\0' );
      if( pos != 0 && pos != string::npos )
        s.erase( s.begin(), s.begin() + pos );
      else if( pos == string::npos )
        s.clear();
    }
    
    return s;
  }
  
  // trim from end
  static inline std::string &rtrim(std::string &s)
  {
#if( defined(_WIN32) && _DEBUG )
    s.erase( std::find_if( s.rbegin(), s.rend(), &not_whitespace ).base(), s.end() );
#else
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int val)->bool { return !std::isspace(val); } ).base(), s.end());
#endif
    //remove null terminating characters.  Boost doesn't do this, but is
    //  necessary when reading fixed width binary data.
    const size_t pos = s.find_last_not_of( '\0' );
    if( pos != string::npos && (pos+1) < s.size() )
      s.erase( s.begin() + pos + 1, s.end() );
    else if( pos == string::npos )
      s.clear();  //string is all '\0' characters
    
    return s;
  }
  
  // trim from both ends
  void trim( std::string &s )
  {
/*
#if(PERFORM_DEVELOPER_CHECKS)
    string copystr = s;
#endif
*/
    
    ltrim( rtrim(s) );
    
/*
#if(PERFORM_DEVELOPER_CHECKS)
    //boost::algorithm::trim doesnt remove trailing null characters.
    
    size_t pos = copystr.find_first_not_of( '\0' );
    if( pos != 0 && pos != string::npos )
      copystr.erase( copystr.begin(), copystr.begin() + pos );
    else if( pos == string::npos )
      copystr.clear();
    pos = copystr.find_last_not_of( '\0' );
    if( pos != string::npos && (pos+1) < copystr.size() )
      copystr.erase( copystr.begin() + pos + 1, copystr.end() );
    
    boost::algorithm::trim( copystr );
    
    if( copystr != s )
    {
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg),
               "Trimmed strings not equal expect: '%s' (len %i), got: '%s' (len %i, from boost)",
               s.c_str(), int(s.size()), copystr.c_str(), int(copystr.size()) );
#if( !SpecUtils_BUILD_FUZZING_TESTS )
      log_developer_error( __func__, errormsg );
#endif
    }
#endif
*/
  }//trim(...)
  
  std::string trim_copy( std::string str )
  {
    trim( str );
    return str;
  }//trim_copy(...)
  
  void split( std::vector<std::string> &resutls,
             const std::string &input, const char *delims )
  {
    resutls.clear();
    
    //The below implementation is not well tested, but in principle might be faster
    //  than boost::algorithm::split (should be tested more and them used)
    //It would aslso reduce final binary size by about 9 kb
    size_t prev_delim_end = 0;
    size_t delim_start = input.find_first_of( delims, prev_delim_end );
    
    while( delim_start != std::string::npos )
    {
      if( (delim_start-prev_delim_end) > 0 )
        resutls.push_back( input.substr(prev_delim_end,(delim_start-prev_delim_end)) );
      
      prev_delim_end = input.find_first_not_of( delims, delim_start + 1 );
      if( prev_delim_end != std::string::npos )
        delim_start = input.find_first_of( delims, prev_delim_end + 1 );
      else
        delim_start = std::string::npos;
    }//while( this_pos < input.size() )
    
    if( prev_delim_end < input.size() )
      resutls.push_back( input.substr(prev_delim_end) );
    
#if(PERFORM_DEVELOPER_CHECKS)
    vector<string> coorectResults;
    boost::algorithm::split( coorectResults, input, boost::is_any_of(delims),
                            boost::token_compress_on );
    while( coorectResults.size() && coorectResults[0].empty() )
      coorectResults.erase( coorectResults.begin() );
    while( coorectResults.size() && coorectResults[coorectResults.size()-1].empty() )
      coorectResults.erase( coorectResults.end()-1 );
    
    if( resutls != coorectResults )
    {
      stringstream errormsg;
      
      errormsg << "Error splitting '";
      for( size_t i = 0; i < input.size(); ++i )
        errormsg << "\\x" << std::hex << int(input[i]);
      errormsg << "' by seperators '";
      for( size_t i = 0; i < strlen(delims); ++i )
        errormsg << "\\x" << std::hex << int(delims[i]);
      errormsg << "'\n\texpected [";
      for( size_t i = 0; i < coorectResults.size(); ++i )
      {
        if( i )
          errormsg << ", ";
        errormsg << "'";
        for( size_t j = 0; j < coorectResults[i].size(); ++j )
          errormsg << "\\x" << std::hex << int(coorectResults[i][j]);
        errormsg << "'";
      }
      
      errormsg << "]\n\tGot [";
      
      for( size_t i = 0; i < resutls.size(); ++i )
      {
        if( i )
          errormsg << ", ";
        errormsg << "'";
        for( size_t j = 0; j < resutls[i].size(); ++j )
          errormsg << "\\x" << std::hex << int(resutls[i][j]);
        errormsg << "'";
      }
      errormsg << "]";
      
      log_developer_error( __func__, errormsg.str().c_str() );
    }//if( resutls != coorectResults )
#endif
  }//void split(...)
  
  void split_no_delim_compress( std::vector<std::string> &resutls,
                               const std::string &input, const char *delims )
  {
    resutls.clear();
    
    //ToDo: this functions logic can probably be cleaned up a bit
    
    size_t prev_delim_end = 0;  //first character after a delim
    size_t delim_start = input.find_first_of( delims, prev_delim_end ); //first delim character >= prev_delim_end
    
    while( delim_start != std::string::npos )
    {
      const size_t sublen = delim_start - prev_delim_end;
      resutls.push_back( input.substr(prev_delim_end,sublen) );
      
      prev_delim_end = delim_start + 1;
      if( prev_delim_end >= input.size() )
      {
        //We have a delim as the last character in the string
        delim_start = std::string::npos;
        resutls.push_back( "" );
      }else
      {
        delim_start = input.find_first_of( delims, prev_delim_end );
      }
    }//while( this_pos < input.size() )
    
    if( prev_delim_end < input.size() )
      resutls.push_back( input.substr(prev_delim_end) );
    
#if(PERFORM_DEVELOPER_CHECKS)
    vector<string> coorectResults;
    boost::algorithm::split( coorectResults, input, boost::is_any_of(delims),
                            boost::token_compress_off );
    
    if( resutls != coorectResults )
    {
      stringstream errormsg;
      
      errormsg << "Error splitting (no-token compress) '";
      errormsg << input;
      //for( size_t i = 0; i < input.size(); ++i )
      //  errormsg << "\\x" << std::hex << int(input[i]);
      errormsg << "' by seperators '";
      errormsg << delims;
      //for( size_t i = 0; i < strlen(delims); ++i )
      //  errormsg << "\\x" << std::hex << int(delims[i]);
      errormsg << "'\n\texpected [";
      for( size_t i = 0; i < coorectResults.size(); ++i )
      {
        if( i )
          errormsg << ", ";
        errormsg << "'";
        errormsg << coorectResults[i];;
        //for( size_t j = 0; j < coorectResults[i].size(); ++j )
        //errormsg << "\\x" << std::hex << int(coorectResults[i][j]);
        errormsg << "'";
      }
      
      errormsg << "]\n\tGot [";
      
      for( size_t i = 0; i < resutls.size(); ++i )
      {
        if( i )
          errormsg << ", ";
        errormsg << "'";
        errormsg << resutls[i];
        //for( size_t j = 0; j < resutls[i].size(); ++j )
        //  errormsg << "\\x" << std::hex << int(resutls[i][j]);
        errormsg << "'";
      }
      errormsg << "]";
      
      log_developer_error( __func__, errormsg.str().c_str() );
    }//if( resutls != coorectResults )
#endif
  }//void split_no_delim_compress(...)
  
  
  void to_lower_ascii( string &input )
  {
#if(PERFORM_DEVELOPER_CHECKS)
    string strcopy = input;
    boost::algorithm::to_lower( strcopy );
#endif
    
    //For non-ASCII we need to convert to wide string, select a locale, and then call locale templated version of tolower (I guess)
    //std::locale loc("en_US.UTF-8");
    //for( size_t i = 0; i < input.size(); ++i )
    //  input[i] = std::tolower( input[i], loc );
    
    for( size_t i = 0; i < input.size(); ++i )
      input[i] = static_cast<char>( tolower(input[i]) );
    
#if(PERFORM_DEVELOPER_CHECKS)
    if( strcopy != input )
    {
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg),
               "Failed to lowercase string.  Expected: '%s', got: '%s'",
               strcopy.c_str(), input.c_str() );
      log_developer_error( __func__, errormsg );
    }
#endif
  }//to_lower_ascii(...)
  
  std::string to_lower_ascii_copy( std::string input )
  {
    to_lower_ascii( input );
    return input;
  }
  
  void to_upper_ascii( string &input )
  {
#if(PERFORM_DEVELOPER_CHECKS)
    string strcopy = input;
    boost::algorithm::to_upper( strcopy );
#endif
    
    for( size_t i = 0; i < input.size(); ++i )
      input[i] = static_cast<char>( toupper(input[i]) );
    
#if(PERFORM_DEVELOPER_CHECKS)
    if( strcopy != input )
    {
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg),
               "Failed to uppercase string.  Expected: '%s', got: '%s'",
               strcopy.c_str(), input.c_str() );
      log_developer_error( __func__, errormsg );
    }
#endif
  }//void to_upper_ascii( string &input )
  
  void ireplace_all( std::string &input, const char *pattern, const char *replacement )
  {
    //This function does not handle UTF8!
    if( input.empty() )
      return;
    
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
    /*
     // This dev test can fail sometimes if the replacement string contains the pattern string
    string strcopy = input, original = input;
    size_t reslen = strcopy.size() + 1;
    while( reslen != strcopy.size() )
    {
      reslen = strcopy.size();
      boost::algorithm::ireplace_all( strcopy, pattern, replacement );
    }
     */
#endif
    
    const size_t paternlen = strlen(pattern);
    if( !paternlen )
      return;
    
    //If we are replacing "XX" with "X" in the sequence "XXX" we want the
    //  result to be "X", however we have to protect against the case where
    //  the replacement string contains the search string
    const bool replace_contains_pattern = icontains( replacement, pattern );
    
    const size_t replacment_len = strlen(replacement);
    bool found = true;
    const char *start = input.c_str(), *end = input.c_str() + input.size();
    
    while( found )
    {
      const char * const it = std::search( start, end, pattern, pattern+paternlen,
                                          char_iequal<char>(std::locale()) );
      found = (it != end);
      if( found )
      {
        size_t delstart = it - input.c_str();
        input.erase( delstart, paternlen );
        input.insert( delstart, replacement );
        start = input.c_str() + delstart + (replace_contains_pattern ? replacment_len : size_t(0));
        end = input.c_str() + input.size();
      }//if( found )
    }//while( found )
    
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
    /*
    if( strcopy != input )
    {
      stringstream msg;
      msg << "Failed to replace '";
      for( size_t i = 0; i < strlen(pattern); ++i )
        msg << "\\x" << hex << int(pattern[i]);
      msg << "' with '";
      for( size_t i = 0; i < strlen(replacement); ++i )
        msg << "\\x" << hex << int(replacement[i]);
      msg << "' in '";
      for( size_t i = 0; i < original.size(); ++i )
        msg << "\\x" << hex << int(original[i]);
      msg << "'.\n\tExpected: '";
      for( size_t i = 0; i < strcopy.size(); ++i )
        msg << "\\x" << hex << int(strcopy[i]);
      msg << "' (length " << strcopy.size() << ")\n\tGot:      '";
      for( size_t i = 0; i < input.size(); ++i )
        msg << "\\x" << hex << int(input[i]);
      msg << "' (length " << input.size() << ").";
      
      log_developer_error( __func__, msg.str().c_str() );
    }
     */
#endif
  }//void ireplace_all(...)
  
  /*
   std::string ireplace_all_copy( const std::string &input,
   const char *pattern, const char *replacement )
   {
   string result;
   
   if( input.empty() )
   return result;
   
   const size_t paternlen = strlen(pattern);
   if( !paternlen )
   return result;
   
   #if(PERFORM_DEVELOPER_CHECKS)
   string strcopy = input, original = input;
   size_t reslen = strcopy.size() + 1;
   while( reslen != strcopy.size() )
   {
   reslen = strcopy.size();
   boost::algorithm::ireplace_all( strcopy, pattern, replacement );
   }
   #endif
   
   
   
   const size_t replacment_len = strlen(replacement);
   
   
   vector<const char *> good_begin, good_end;
   
   size_t newstrlen = 0;
   const char *start = input.c_str();
   const char * const end = input.c_str() + input.size();
   
   while( start < end )
   {
   const char * const it = std::search( start, end, pattern, pattern+paternlen,
   char_iequal<char>(std::locale()) );
   
   good_begin.push_back( start );
   good_end.push_back( it );
   
   newstrlen += (it - start);
   start = it;
   
   if( it != end )
   {
   newstrlen += replacment_len;
   start += paternlen;
   }else
   {
   good_begin.push_back( end );
   good_end.push_back( end );
   }
   }//while( found )
   
   if( good_begin.empty() )
   return input;
   
   result.reserve( newstrlen + 1 );  //+1, because I'm not sure if reserve takes into account '\0'
   
   for( size_t i = 0; i < good_begin.size(); ++i )
   {
   result.insert( result.end(), good_begin[i], good_end[i] );
   if( good_begin[i] != good_end[i] )
   result.insert( result.end(), replacement, replacement+replacment_len );
   }
   
   #if(PERFORM_DEVELOPER_CHECKS)
   if( strcopy != result )
   {
   stringstream msg;
   msg << "Failed to replace '";
   for( size_t i = 0; i < strlen(pattern); ++i )
   msg << "\\x" << hex << int(pattern[i]);
   msg << "' with '";
   for( size_t i = 0; i < strlen(replacement); ++i )
   msg << "\\x" << hex << int(replacement[i]);
   msg << "' in '";
   for( size_t i = 0; i < original.size(); ++i )
   msg << "\\x" << hex << int(original[i]);
   msg << "'.\n\tExpected: '";
   for( size_t i = 0; i < strcopy.size(); ++i )
   msg << "\\x" << hex << int(strcopy[i]);
   msg << "' (length " << strcopy.size() << ")\n\tGot:      '";
   for( size_t i = 0; i < result.size(); ++i )
   msg << "\\x" << hex << int(result[i]);
   msg << "' (length " << result.size() << ").";
   
   log_developer_error( __func__, msg.str().c_str() );
   }
   #endif
   
   return result;
   }//ireplace_all_copy(...)
   */
  
  template<typename IterType>
  size_t utf8_iterate( IterType& it, const IterType& last )
  {
    if(it == last)
      return 0;
    
    unsigned char c;
    size_t res = 1;
    for(++it; last != it; ++it, ++res)
    {
      c = *reinterpret_cast<const unsigned char *>(it);
      
      //if highest value digit is not set, or if highest two digits are set
      //If the most significant bit isnt set, then its an ascii character.
      //  If the two most significant bits are set, then its the start of
      //  a UTF8 character; middle/end UTF8 bytes have most significant bit set,
      //  and second most significant bit not set
      //In principle we can count the number of leading '1' bits of the first
      //  byte in the character to get multibyte character length, but instead
      //  we will manually iterate incase there is an incorrect encoding.
      //
      //see: http://www.cprogramming.com/tutorial/unicode.html
      // 0x80 --> 10000000  //means not an ascii character
      // 0xC0 --> 11000000  //means start of new character
      const unsigned char not_ascii_bit = 0x80u;
      const unsigned char utf8_start_bits = 0xC0u;

      const bool is_ascii = !(c & not_ascii_bit);
      const bool is_utf8_start = ((c & utf8_start_bits) == utf8_start_bits);
      
      if( is_ascii || is_utf8_start  )
        break;
    }
    
    return res;
  }//size_t utf8_iterate( IterType& it, const IterType& last )
  
  
  size_t utf8_iterate( const char * &it )
  {
    //Assumes null-terminated string
    if( !(*it) )
      return 0;

    unsigned char c;
    size_t res = 0;
    for( ; *it; ++it, ++res)
    {
      c = *it;
      if( !(c & 0x80) || ((c & 0xC0) == 0xC0))
        break;
    }
    
    return res;
  }//size_t utf8_iterate( IterType& it )
  
  
  size_t utf8_str_len( const char * const str, const size_t str_size_bytes )
  {
    size_t len = 0;
    if( !str )
      return len;
  
    const char * const end = str + str_size_bytes;
    for( const char *ptr = str; ptr != end; utf8_iterate(ptr, end) )
    {
      ++len;
    }
  
    return len;
  }//size_t utf8_str_len( const char * const str, size_t str_size_bytes )

  size_t utf8_str_len( const char * const str )
  {
    size_t len = 0;
    if( !str )
      return len;
  
    for( const char *ptr = str; *ptr; ++len )
    {
      const size_t nbytes = utf8_iterate(ptr);
      if( nbytes == 0 )
        break;
    }
  
    return len;
  }
  
  
  void utf8_limit_str_size( std::string &str, const size_t max_bytes )
  {
    const size_t index = utf8_str_size_limit( str.c_str(), str.size() + 1, max_bytes + 1 );
    str = str.substr( 0, index );
  }
  
  
  size_t utf8_str_size_limit( const char * const str,
                             size_t num_in_bytes, const size_t max_bytes )
  {
    if( !str )
      return 0;
    
    if( !num_in_bytes )
      num_in_bytes = strlen(str) + 1;
    
    if( num_in_bytes<=1 || max_bytes<=1 )
      return 0;
    
    if( num_in_bytes <= max_bytes )
      return num_in_bytes - 1;
    
    
    for( size_t index = max_bytes - 1; index != 0; --index )
    {
      if( (str[index-1] & 0x80) == 0x00 ) //character before index is ascii character
        return index;
      
      if( (str[index] & 0xC0) == 0xC0 )  //character at index is first byte of a UTF-8 character
        return index;
      
      if( (str[index] & 0x80) == 0x00 )  //character at index index is ascii character, so we can replace it will null byte
        return index;
    }
    
    return 0;
  }
  
  
  bool valid_utf8( const char * const str, const size_t num_in_bytes )
  {
    int bytesToProcess = 0;
    
    for( size_t i = 0; i < num_in_bytes; ++i )
    {
      const uint8_t c = reinterpret_cast<const uint8_t &>( str[i] );
      if( bytesToProcess == 0 )
      {
        // Determine how many bytes to expect
        if( (c & 0x80) == 0 )
          continue;            // 1-byte character (ASCII)
        else if( (c & 0xE0) == 0xC0 )
          bytesToProcess = 1;  // 2-byte character
        else if( (c & 0xF0) == 0xE0 )
          bytesToProcess = 2;  // 3-byte character
        else if( (c & 0xF8) == 0xF0 )
          bytesToProcess = 3;  // 4-byte character
        else
          return false; // Invalid leading byte
      }else
      {
        // Expecting continuation byte
        if( (c & 0xC0) != 0x80 )
          return false; // Not a valid continuation byte
        bytesToProcess--;
        assert( bytesToProcess >= 0 );
      }
    }//for( size_t i = 0; i < num_in_bytes; ++i )
    
    assert( bytesToProcess >= 0 );
    
    return (bytesToProcess == 0);
  }//bool valid_utf8( const char * const str, size_t num_in_bytes )
  
  
  template <class T>
  bool split_to_integral_types( const char *input, const size_t length,
                               std::vector<T> &results )
  {
    static_assert( std::is_same<T,int>::value || std::is_same<T,long long>::value,
                  "split_to_integral_types only implemented for int and long long" );
    
    results.clear();

    const size_t num_float_guess = (std::max)( size_t(1), (std::min)(length / 3, size_t(32768)) );
    results.reserve(num_float_guess);
    
    if( !input || !length )
      return true;
    
#if( SpecUtils_USE_FROM_CHARS || SpecUtils_USE_FAST_FLOAT )
    const char *start = input;
    const char * const end = input + length;

    while( start < end )
    {
      T result;
#if( SpecUtils_USE_FROM_CHARS )
      const std::from_chars_result status = std::from_chars(start, end, result);
#else
      const fast_float::from_chars_result status = fast_float::from_chars(start, end, result);
#endif

      if( status.ec != std::errc() )
        return false;
      
      results.push_back( result );
      
      start = status.ptr + 1;
      if( start >= end )
        return true;

      // For most of input `start` will be the character of the next number, but we need to test for this
      if (((*start) < '0') || ((*start) > '9'))
      {
        // We will allow the delimiters " \t\n\r,"; I'm sure there is a much better way of testing for these
        while ((start < end)
          && (((*start) == ' ')
            || ((*start) == '\t')
            || ((*start) == '\n')
            || ((*start) == '\r')
            || ((*start) == ',')
            // We want to allow numbers to start with a '+', which from_chars prohibits,
            // but we also want require the next character to be a number or a decimal,
            // and also for the '+' to not be the last character
            || (((*start) == '+') && ((start + 1) < end) && ((((*(start + 1)) >= '0') && ((*(start + 1)) <= '9')) || ((*(start + 1)) == '.')))
            )
          )
        {
          ++start;
        }
      }//if (((*start) < '0') || ((*start) > '9'))
    }//while( start < end )
    
    return true;
    
#elif( SpecUtils_USE_BOOST_SPIRIT && (BOOST_VERSION >= 104500) )
    namespace qi = boost::spirit::qi;
    
    const char *begin = input;
    const char *end = begin + length;
    
    bool ok;
    if( std::is_same<T,int>::value )
    {
      ok = qi::phrase_parse( begin, end, (*qi::int_) % qi::eol, qi::lit(",")|qi::space, results );
    }else if( std::is_same<T,long long>::value )
    {
      ok = qi::phrase_parse( begin, end, (*qi::long_long) % qi::eol, qi::lit(",")|qi::space, results );
    }else
    {
      assert(0);
      ok = false;
    }
    
    
    //If we didnt consume the entire input, make sure only delimiters are left
    if( ok && begin != end )
    {
      for( const char *pos = begin; pos != end; ++pos )
      {
        if( !is_in( *pos, " \t\n\r," ) )
          return false;
      }
    }//if( ok && begin != end )
    
    return ok;
#else
    // SpecUtils_USE_STRTOD or (BOOST_VERSION < 104500)
    //  (terribly inefficient, but probably not really used)
    
    const char * const delims = " \t\n\r,";
    const char * const end = input + length;
    
    const char *pos = next_word( input, end, delims );
    
    while( pos < end )
    {
      const char d = *pos;
      if( !isdigit(d) && (d != '+') && (d != '-') )
        return false;
      
      const char * const word_end = end_of_word( pos, end, delims );
      
      try
      {
        T val;
        if( std::is_same<T,int>::value )
          val = static_cast<T>( stoi( string(pos, word_end - pos), nullptr, 10) ); //cast to prevent warning
        else
          val = static_cast<T>( stoll( string(pos, word_end - pos), nullptr, 10) );
        
        results.push_back( val );
      }catch( std::exception & )
      {
        return false;
      }
    
      pos = next_word( word_end, end, delims );
    }//while( pos < end )
    
    return true;
#endif
  }//split_to_integral_types
  
  
  bool split_to_ints( const char *input, const size_t length,
                     std::vector<int> &results )
  {
    return split_to_integral_types( input, length, results );
  }//bool split_to_ints(...)
  
  
  bool split_to_long_longs( const char *input, const size_t length,
                           std::vector<long long> &results )
  {
    return split_to_integral_types( input, length, results );
  }
  
  bool split_to_floats( const std::string &input, std::vector<float> &results )
  {
    return split_to_floats( input.c_str(), input.length(), results );
  }
  
  bool split_to_floats( const char *input, const size_t length, vector<float> &results )
  {
    results.clear();
    
    // This initial guess almost always over-predicts (when checked on a representative dataset)
    //  but not by too much, and it reduces total time by about 20%
    const size_t num_float_guess = (std::max)( size_t(1), (std::min)(length / 2, size_t(32768)) );
    results.reserve(num_float_guess);
    
#if( SpecUtils_USE_FROM_CHARS || SpecUtils_USE_FAST_FLOAT )
     //  TODO: test this implementation against old implementation for the regression test
     const char *start = input;
     const char *end = input + length;

     while( start < end )
     {
       float result;
#if( SpecUtils_USE_FROM_CHARS )
       const auto [ptr, ec] = std::from_chars(start, end, result);
#else
       // C++17 is required to use structured bindings `auto [ptr, ec] = ...`, so we'll use `from_chars_result`
       const fast_float::from_chars_result status = fast_float::from_chars(start, end, result);
       const char * const &ptr = status.ptr;
       const std::errc &ec = status.ec;
#endif

       if(ec == std::errc() )
       {
         //cout << "Result: " << result << ", (ptr-start) -> " << (status.ptr-start) << endl;
         results.push_back( result );
       }else
       {
         //cout << "Error reading '" << string(start,end) << "' isn't a number." << endl;
         return false;
       }
     
       start = ptr + 1;
       if (start >= end)
         return true;

       // For most of input `start` will be the character of the next number, but we need to test for this
       if (((*start) < '0') || ((*start) > '9'))
       {
         // We will allow the delimiters " \t\n\r,"; I'm sure there is a much better way of testing for these
         while ((start < end)
           && (((*start) == ' ')
             || ((*start) == '\t')
             || ((*start) == '\n')
             || ((*start) == '\r')
             || ((*start) == ',')
             // We want to allow numbers to start with a '+', which from_chars prohibits, 
             // but we also want require the next character to be a number or a decimal, 
             // and also for the '+' to not be the last character 
             || (((*start) == '+') && ((start + 1) < end) && ((((*(start + 1)) >= '0') && ((*(start + 1)) <= '9')) || ((*(start + 1)) == '.')))
             )
           )
         {
           ++start;
         }
       }//if (((*start) < '0') || ((*start) > '9'))
     }//while( start < end )
     
     return true;

#elif( SpecUtils_USE_BOOST_SPIRIT )
    if( !input || !length )
      return true;
    
    namespace qi = boost::spirit::qi;
    
    const char *begin = input;
    const char *end = begin + length;
    
    //Note that adding the comma increases the parse time by about 15% over just
    //  qi::space. Could perform a check to see if a comma comes after the first
    //  number and then choose the parser expression to use...
    //Not sure difference between qi::space and qi::blank from testing various
    //  strings
#if( BOOST_VERSION < 104500 )
    // namespace ascii = boost::spirit::ascii;
    // namespace phoenix = boost::phoenix;
    
    //const bool ok = qi::phrase_parse( begin, end,
    //  (
    //      qi::float_[phoenix::push_back(phoenix::ref(results), qi::_1)]
    //          >> *(qi::lit(",")|qi::space >> qi::float_[phoenix::push_back(phoenix::ref(results), qi::_1)])
    // )
    //, qi::lit(",")|qi::space|qi::eol );
    char *hack_input = const_cast<char *>(input);
    const char orig_char = hack_input[length];
    hack_input[length] = '\0';
    const bool ok = split_to_floats( hack_input, results, " \t\n\r,", false );
    hack_input[length] = orig_char;
    return ok;
#else
    const bool ok = qi::phrase_parse( begin, end, (*qi::float_) % qi::eol, qi::lit(",")|qi::space, results );
    
    // Note that strings like "661.7,-0.1.7,9.3", will parse into 4 numbers {661.6,-0.1,0.7,9.3}
    
#endif
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
    if( !ok )
    {
      if( *input && isdigit(*input) )
      {
        char errormsg[1024];
        snprintf( errormsg, sizeof(errormsg), "Parsing failed: '%s'",
                 string(begin,end).c_str() );
        log_developer_error( __func__, errormsg );
      }//if( *input && isdigit(*input) )
    }else
    {
      if( begin != end && results.size() )
      {
        char errormsg[1024];
        snprintf( errormsg, sizeof(errormsg), "Trailing unparsed string '%s'",
                 string(begin,end).c_str() );
        log_developer_error( __func__, errormsg );
      }//if( begin != end )
      
      
      vector<float> checked_result;
      string datacopy( input, input+length );
      trim( datacopy );
      split_to_floats( (char *)datacopy.c_str(), checked_result, " \t\n\r,", false );
      
      if( checked_result.size() == results.size() )
      {
        for( size_t i = 0; i < results.size(); ++i )
        {
          const float a = results[i];
          const float b = checked_result[i];
          if( fabs(a-b) > 0.000001*((std::max)(fabs(a),fabs(b)) ))
          {
            char errormsg[1024];
            snprintf( errormsg, sizeof(errormsg),
                     "Out of tolerance difference for floats %.9g using "
                     "boost::spirit vs %.9g using alternative split_to_float on float %i",
                     a, b, int(i) );
            log_developer_error( __func__, errormsg );
          }
        }//for( size_t i = 0; i < results.size(); ++i )
      }else
      {
        char errormsg[1024];
        snprintf( errormsg, sizeof(errormsg),
                 "Parsed wrong number of floats %i using boost::spirit and %i "
                 "using strtok for '%s'",
                 int(results.size()), int(checked_result.size()),
                 string(input,end).c_str() );
        log_developer_error( __func__, errormsg );
      }//if( checked_result.size() == results.size() ) / else
    } //if( ok ) / else
#endif
    
    //If we didnt consume the entire input, make sure only delimiters are left
    if( ok && begin != end )
    {
      for( const char *pos = begin; pos != end; ++pos )
      {
        if( !is_in( *pos, " \t\n\r," ) )
          return false;
      }
    }//if( ok && begin != end )
    
    return ok;
#elif( SpecUtils_USE_STRTOD )
    return split_to_floats_strtod( input, length, " \t\n\r,", false, results );
#else
    static_assert( 0, "No float parsing method defined???" );
#endif //SpecUtils_USE_FROM_CHARS / else
  }//bool split_to_floats(...)
  
  
  bool parse_double( const char *input, const size_t length, double &result )
  {
    result = 0.0;
    
#if( SpecUtils_USE_FROM_CHARS || SpecUtils_USE_FAST_FLOAT )
    const char *begin = next_word( input, input + length, " \t\n\r,+" ); // Fast forward through whitespace and '+'
    const char *end = input + length;
    
#if( SpecUtils_USE_FROM_CHARS )
    const auto status = std::from_chars(begin, end, result);
#else
    const auto status = fast_float::from_chars(begin, end, result);
#endif
    
    const bool ok = (status.ec == std::errc());
    //if( ok && ((status.ptr + 1) != end) )
    //  return false;
    return ok;
#elif( SpecUtils_USE_BOOST_SPIRIT )
    namespace qi = boost::spirit::qi;
    const char *begin = input;
    const char *end = begin + length;
    const bool ok = qi::phrase_parse( begin, end, qi::double_, qi::space, result );
    //  if( ok && (begin != end) )
    //    return false;
    return ok;
#elif( SpecUtils_USE_STRTOD )
    ;
    try
    {
      result = std::stod( std::string(input, length) );
    }catch( std::exception & )
    {
      return false;
    }
    return true;
#endif
  }


  bool parse_float( const char *input, const size_t length, float &result )
  {
    result = 0.0f;
    
#if( SpecUtils_USE_FROM_CHARS || SpecUtils_USE_FAST_FLOAT )
    const char *begin = next_word( input, input + length, " \t\n\r,+" ); // Fast forward through whitespace and '+'
    const char *end = input + length;
    
#if( SpecUtils_USE_FROM_CHARS )
    const auto status = std::from_chars(begin, end, result);
#else
    const auto status = fast_float::from_chars(begin, end, result);
#endif
    const bool ok = (status.ec == std::errc());
    //if( ok && ((status.ptr + 1) != end) )
    //  return false;
    return ok;
#elif( SpecUtils_USE_BOOST_SPIRIT )
    namespace qi = boost::spirit::qi;
    const char *begin = input;
    const char *end = begin + length;
    const bool ok = qi::phrase_parse( begin, end, qi::float_, qi::space, result );
    //  if( ok && (begin != end) )
    //    return false;
    return ok;
#elif( SpecUtils_USE_STRTOD )
    try
    {
      result = std::stof( std::string(input, length) );
    }catch( std::exception & )
    {
      return false;
    }
    return true;
#endif
  }//parse_float(...)
  

  bool parse_int( const char *input, const size_t length, int &result )
  {
#if( SpecUtils_USE_FROM_CHARS || SpecUtils_USE_FAST_FLOAT )
    const char *begin = next_word( input, input + length, " \t\n\r,+" ); // Fast forward through whitespace and '+'
    const char *end = input + length;
    
#if( SpecUtils_USE_FROM_CHARS )
    const auto status = std::from_chars(begin, end, result);
#else
    const auto status = fast_float::from_chars(begin, end, result);
#endif
    const bool ok = (status.ec == std::errc());
    //if( ok && ((status.ptr + 1) != end) )
    //  return false;
    return ok;
#elif( SpecUtils_USE_BOOST_SPIRIT )
    namespace qi = boost::spirit::qi;
    
    const char *begin = input;
    const char *end = begin + length;
    
    result = 0;
    const bool ok = qi::phrase_parse( begin, end, qi::int_, qi::space, result );
    //  if( ok && (begin != end) )
    //    return false;
    return ok;
#elif( SpecUtils_USE_STRTOD )
    std::string temp_str( input, length );
    try
    {
      result = std::stoi(temp_str);
    }catch( std::exception & )
    {
      return false;
    }
    return true;
#endif
  }


  bool split_to_floats( const char *input, vector<float> &contents,
                       const char * const delims,
                       const bool cambio_zero_compress_fix )
  {
#if(PERFORM_DEVELOPER_CHECKS)
    if( string(delims).find_first_of( ".0123456789+-.eE" ) != string::npos )
    {
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg), "Invalid delimiter: '%s'", delims );
      log_developer_error( __func__, errormsg );
      return false;
    }
#endif
    
    contents.clear();
    
    if( !input || !(*input) )
      return false;
    
#if( SpecUtils_USE_FROM_CHARS || SpecUtils_USE_FAST_FLOAT || SpecUtils_USE_BOOST_SPIRIT )
    const char *pos = input;
    // TODO: 20240307: strings like "661.7,-0.1.7,9.3" are parsing and returning true - we could/should fix this
    //bool all_fields_okay = true;
    
    while( *pos )
    {
      pos = next_word( pos, delims );
      
      if( *pos == '\0' )
        return true;
      
      const char * start_pos = pos;
      
#if( SpecUtils_USE_FROM_CHARS || SpecUtils_USE_FAST_FLOAT )
      // from_chars doesnt allow leading '+' sign
      if( (*start_pos) == '+' )
      {
        ++start_pos;
        
        // If the next digit after the plus sign isnt a digit, return false
        if( !(*start_pos) || ((*start_pos) < '0') || ((*start_pos) > '9') )
          return false;
      }
#endif
      
      const char * const end = end_of_word( start_pos, delims );
      
#if( SpecUtils_USE_FROM_CHARS )
      float value;
      const auto status = std::from_chars(start_pos, end, value);
      const bool ok = (status.ec == std::errc());
      pos = status.ptr;
#elif( SpecUtils_USE_FAST_FLOAT )
      float value;
      const auto status = fast_float::from_chars(start_pos, end, value);
      const bool ok = (status.ec == std::errc());
      pos = status.ptr;
#else
      //Using a double here instead of a float causes about a 2.5% slow down.
      //  Using a float would be fine, but then you hit a limitation
      //  that the value before decimal point must be less than 2^32 (note, we are
      //  unlikely to ever see this in channel counts).
      double value;
      const bool ok = boost::spirit::qi::parse( pos, end, boost::spirit::qi::double_, value );
#endif
      
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
      if( !ok )
      {
        if( input && isdigit(*input) )
        {
          char errormsg[1024];
          snprintf( errormsg, sizeof(errormsg), "Parsing failed: '%s'",
                   string(start_pos, end).c_str() );
          log_developer_error( __func__, errormsg );
        }
      }else
      {
        if( pos != end )
        {
          char errormsg[1024];
          snprintf( errormsg, sizeof(errormsg), "Trailing unparsed string '%s'",
                   string(pos,end).c_str() );
          log_developer_error( __func__, errormsg );
        }//if( begin != end )
        
        const float a = static_cast<float>( value );
        const float b = (float) atof( string(start_pos, end).c_str() );
        if( fabs(a-b) > 0.000001f*((std::max)(fabs(a),fabs(b)) ))
        {
          char errormsg[1024];
          snprintf( errormsg, sizeof(errormsg),
                   "Out of tolerance diffence for floats %.9g using "
                   "boost::spirit vs %.9g using alternative split_to_float on float %i",
                   a, b, int(contents.size()) );
          log_developer_error( __func__, errormsg );
        }
      }//if( !ok )
#endif //PERFORM_DEVELOPER_CHECKS
      
      if( !ok )
        return false;
      
      //all_fields_okay &= (pos == end);
      
      //If the next char is a delimiter, then we dont want to apply the fix, otherwise apply the fix
      if( cambio_zero_compress_fix && (value == 0.0)
         && !is_in( *(start_pos+1), delims ) )
      {
        value = std::numeric_limits<float>::min();
      }//if( value == 0.0 )
      
      contents.push_back( static_cast<float>(value) );
    }//while( *pos )
    
    return !(*pos);
#elif( SpecUtils_USE_STRTOD )
    const size_t length = strlen( input );
    return split_to_floats_strtod( input, length, delims, cambio_zero_compress_fix, contents );
#endif
  }//vector<float> split_to_floats( ... )
  

std::string convert_from_utf16_to_utf8(const std::wstring &winput)
{
#ifdef _WIN32
  std::string answer;
  int requiredSize = WideCharToMultiByte(CP_UTF8, 0, winput.c_str(), -1, 0, 0, 0, 0);
  if( requiredSize > 0 )
  {
    std::vector<char> buffer(requiredSize);
    WideCharToMultiByte(CP_UTF8, 0, winput.c_str(), -1, &buffer[0], requiredSize, 0, 0);
    answer.assign(buffer.begin(), buffer.end() - 1);
  }
  return answer;
#else
  
  try
  {
#if( HAS_STD_CODECVT )
    return std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().to_bytes( winput );
#else
    return boost::locale::conv::utf_to_utf<char>(winput.c_str(), winput.c_str() + winput.size());
#endif
  }catch( std::exception & )
  {
  }
  
  return "";
#endif
}//std::string convert_from_utf16_to_utf8(const std::wstring &winput)
  
  
std::wstring convert_from_utf8_to_utf16( const std::string &input )
{
//#if( defined(_WIN32) && defined(_MSC_VER) )
#ifdef _WIN32
  std::wstring answer;
  int requiredSize = MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, 0, 0);
  if(requiredSize > 0)
  {
    std::vector<wchar_t> buffer(requiredSize);
    MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, &buffer[0], requiredSize);
    answer.assign(buffer.begin(), buffer.end() - 1);
  }
  
  return answer;
#else
  
  try
  {
#if( HAS_STD_CODECVT )
    return std::wstring_convert<std::codecvt_utf8<wchar_t>>().from_bytes(input);
#else
    return boost::locale::conv::utf_to_utf<wchar_t>(input.c_str(), input.c_str() + input.size());
#endif
  }catch( std::exception & )
  {
  }
  
  return L"";
#endif
}//std::wstring convert_from_utf8_to_utf16( const std::string &str );
  
  // A function to convert floats to their shortest text representation with at least the
  //  specified precision (e.g., number of sig figs),
  // Definitely not a very efficient function, or even optimal, but better than nothing.
  string printCompact_trial( double val, size_t precision, const bool use_scientific )
  {
    assert( precision > 0 );
    if( !precision )
      precision = 1;
    
    //Note that using %.{precision}G will cause snprintf to use the
    //  IEEE 754 rounding rule of "round to nearest and ties to even"...
    
    char buffer[256] = { '\0' };
    if( use_scientific )
    {
      snprintf( buffer, sizeof(buffer), ("%." + to_string(precision) + "E").c_str(), val );
    }else
    {
      // We add 1 to precision to avoid double-rounding that leads to errors (I think 1 is
      //  probably enough, but we're not exactly being efficient anyway)
      //  Ex. 1.2345 to precision=3, if "%.3f" then will be "1.235", which we would then round to
      //      "1.24", which is not correct
      size_t ndec = precision + 2;
      if( (fabs(val) < 1.0) && (fabs(val) > std::numeric_limits<float>::min()) )
        ndec = 2 + precision + std::ceil( fabs( std::log10( fabs(val) ) ) );
      
      snprintf( buffer, sizeof(buffer), ("%." + to_string(ndec) + "f").c_str(), val );
    }
    
    string power;
    string decimal = buffer;
    
    const string::size_type e_pos = decimal.find( 'E' );
    if( e_pos != string::npos )
    {
      power = decimal.substr( e_pos );
      decimal = decimal.substr( 0, e_pos );
    }//if( scientific notation )
    
    if( decimal.find('.') != string::npos )
    {
      while( decimal.size() && (decimal.back() == '0') )
        decimal.resize( decimal.size() - 1 );
      
      if( decimal.size() && (decimal.back() == '.') )
        decimal.resize( decimal.size() - 1 );
    }//if( decimal.find('.') != string::npos )
    
    
    
    auto round_at_pos = []( string input, size_t pos ) -> string {
      //pos indicates the first digit we do not want
      if( pos >= input.size() )
        return input;
      
      assert( (input[pos] >= '0') && (input[pos] <= '9') );
      
      for( size_t i = pos + 1; i < input.size(); ++i )
      input[i] = '0';
      
      // We'll attempt to do some rounding - man, is this horrible
      bool no_round = (input[pos] < '5');
      if( input[pos] == '5' )
      {
        // round to nearest and ties to even
        char prevchar = '\0';
        if( (pos > 0) && (input[pos-1] != '.') )
          prevchar = input[pos-1];
        else if( pos > 1 )
          prevchar = input[pos-2];
        
        const int digval = prevchar - '0';
        no_round = !(digval % 2);
      }//if( input[pos] == '5' )
      
      
      if( no_round )
      {
        input = input.substr( 0, pos );
      }else
      {
        input = input.substr( 0, pos );
        
        const bool is_negative = (input[0] == '-');
        if( is_negative )
          input = input.substr( 1 );
        
        for( size_t index = input.size() - 1; index > 0; --index )
        {
          if( input[index] != '.' )
          {
            assert( input[index] >= '0' && input[index] <= '9' );
            
            if( input[index] != '9' )
            {
              input[index] += 1;
              break;
            }
            
            assert( input[index] == '9' );
            
            input[index] = '0';
          }//if( input[index] != '.' )
          
          if( index == 1 )
          {
            if( input[0] == '.' )
            {
              input = "1" + input;
            }else if( input[0] != '9' )
            {
              input[0] += 1;
            }else
            {
              input[0] = '0';
              input = "1" + input;
              break;
            }
          }
        }//for( size_t index = decimal.size() - 1; index > 0; --index )
        
        if( is_negative )
          input = "-" + input;
      }//if( we need to round our last digit up )
      
      return input;
    };//round_at_pos
    
    
    bool hit_dec = false, hit_nonzero = false;
    size_t num_digit = 0;
    for( size_t i = 0; i < decimal.size(); ++i )
    {
      if( decimal[i] == '-' )
        continue;
      
      if( decimal[i] == '.' )
      {
        hit_dec = true;
        
        if( (num_digit >= precision) && hit_nonzero && ((i+1) < decimal.size()) )
        {
          decimal = round_at_pos( decimal, i+1 );
          break;
        }
        
        continue;
      }//if( decimal[i] == '.' )
      
      assert( (decimal[i] >= '0') && (decimal[i] <= '9') );
      
      if( decimal[i] != '0' )
        hit_nonzero = true;
      
      if( !hit_nonzero )
        continue;
      
      num_digit += 1;
      
      if( num_digit >= precision )
      {
        // If were at the last character, theres nothing to do
        if( (i+1) == decimal.size() )
          break;
        
        // If we're already past the decimal, get rid of everything past current character
        if( hit_dec )
        {
          decimal = round_at_pos( decimal, i + 1 );
          break;
        }//if( hit_dec )
      }//if( we already have enough digits )
    }//for( loop over digits before "E" )
    
    
    
    // Remove trailing zeros after the decimal
    const bool has_per = (decimal.find( '.' ) != string::npos );
    if( has_per )
    {
      while( decimal.size() && (decimal.back() == '0') )
        decimal.resize( decimal.size() - 1 );
      
      // Dont leave a dangling decimal
      if( decimal.size() && decimal.back() == '.' )
        decimal.resize( decimal.size() - 1 );
    }//if( has_per )
    
    
    if( !power.empty() )
    {
      power = power.substr(1);
      int intpow = atoi( power.c_str() );
      
      if( (decimal.find('.') == string::npos) && (decimal.size() >= 2) )
      {
        // decimal could now be "10", so we could increment the power, and remove a zero
        size_t ndecdigit = decimal.size();
        if( ndecdigit && (decimal[0] == '-') )
          ndecdigit -= 1;
        
        if( (ndecdigit > 0) && (decimal.back() == '0') )
        {
          intpow += 1;
          decimal.resize( decimal.size() - 1 );
        }
      }//if( decimal is maybe reducable )
      
      power = "E" + std::to_string( intpow );
    }//if( !power.empty() )
    
    return decimal + power;
  };//printCompact

   
  string printCompact( const double val, const size_t precision )
  {
    if( IsNan(val) )
      return "nan";
    
    if( IsInf(val) )
      return "inf";
    
    const string as_science = printCompact_trial( val, precision, true );
    const string as_fixed = printCompact_trial( val, precision, false );
    
    return ((as_fixed.size() <= as_science.size()) ? as_fixed : as_science);
    
    //char buffer[64] = { '\0' };
    //snprintf( buffer, sizeof(buffer), ("%." + std::to_string(precision) + "G").c_str(), val );
    //return buffer;
  }//string printCompact( double val, const size_t precision )
  

  std::string sequencesToBriefString( const std::set<int> &sample_numbers )
  {
    if( sample_numbers.empty() )
      return "";
      
    stringstream editVal;
    
    int added = 0;
    int firstInRange = *(sample_numbers.begin());
    int previous = firstInRange;
    
    for( set<int>::const_iterator iter = sample_numbers.begin();
        iter != sample_numbers.end(); ++iter )
    {
      const int thisval = *iter;
      
      if( (thisval > (previous+1)) )
      {
        editVal << string(added ? "," : "");
        if( previous == firstInRange )
          editVal << previous;
        else if( previous == (firstInRange+1) )
          editVal << firstInRange << "," << previous;
        else
          editVal << firstInRange << "-" << previous;
        
        ++added;
        firstInRange = thisval;
      }//if( thisval > (previous+1) )
      
      previous = thisval;
    }//for( loop over smaple_numbers )
    
    editVal << string(added ? "," : "");
    if( previous == firstInRange )
      editVal << previous;
    else if( previous == (firstInRange+1) )
      editVal << firstInRange << "," << previous;
    else
      editVal << firstInRange << "-" << previous;
    
    return editVal.str();
  }
  
  
  unsigned int levenshtein_distance( const string &source, const string &target,
                                    const size_t max_str_len )
  {
    //This function largely derived from code found at:
    //  http://www.merriampark.com/ldcpp.htm  (by Anders Johnasen).
    //  There was no accompanying license information, and since I found many
    //  similar-but-separate implementations on the internet, I take the code in
    //  this function to be licensed under a 'do-what-you-will' public domain
    //  license. --Will Johnson 20100824
    
    
    //This function is case insensitive.
    if( !max_str_len )
      return 0;
    
    const size_t n = (std::min)( source.length(), max_str_len );
    const size_t m = (std::min)( target.length(), max_str_len );
    if( !n )
      return static_cast<unsigned int>(m);
    
    if( !m )
      return static_cast<unsigned int>(n);
    
    // TODO: it looks like this function could be implemented with a much smaller memory footprint for larger string.
    vector< vector<size_t> > matrix( n+1, vector<size_t>(m+1,0) );
    
    for( size_t i = 0; i <= n; i++)
      matrix[i][0]=i;
    
    for( size_t j = 0; j <= m; j++)
      matrix[0][j]=j;
    
    for( size_t i = 1; i <= n; i++)
    {
      const string::value_type s_i = std::toupper( source[i-1] );
      
      for( size_t j = 1; j <= m; j++)
      {
        const string::value_type t_j = std::toupper( target[j-1] );
        
        size_t cost = (s_i == t_j) ? 0 : 1;
        
        const size_t above = matrix[i-1][j];
        const size_t left = matrix[i][j-1];
        const size_t diag = matrix[i-1][j-1];
        size_t cell = (std::min)( above + 1, (std::min)(left + 1, diag + cost));
        
        // Step 6A: Cover transposition, in addition to deletion,
        // insertion and substitution. This step is taken from:
        // Berghel, Hal ; Roach, David : "An Extension of Ukkonen's
        // Enhanced Dynamic Programming ASM Algorithm"
        // (http://www.acm.org/~hlb/publications/asm/asm.html)
        
        if( i>2 && j>2 )
        {
          size_t trans = matrix[i-2][j-2]+1;
          if( source[i-2] != t_j )
            trans++;
          if( s_i != target[j-2] )
            trans++;
          if( cell > trans )
            cell = trans;
        }//if()
        
        matrix[i][j]=cell;
      }//for( loop over 'target' letters j )
    }//for( loop over 'source' letters i )
    
    return static_cast<unsigned int>(matrix[n][m]);
  }//unsigned int levenshtein_distance( const string &, const string &)

}//namespace SpecUtils
