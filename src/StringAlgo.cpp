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

#include <string>
#include <locale>

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

#if(PERFORM_DEVELOPER_CHECKS)
#include <boost/algorithm/string.hpp>
#endif

#include "rapidxml/rapidxml.hpp"

#include "SpecUtils/StringAlgo.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
//Pull in WideCharToMultiByte(...), etc from WIndows
#define NOMINMAX
#include <windows.h>

#undef min
#undef max
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
  
  //The below may not be instrinsic friendly; might be interesting to look and
  // see if we could get better machine code by making instrinic friendly (maybe
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
  
  inline const char *end_of_word( const char *input, const char *delim )
  {
    while( *input && !is_in(*input, delim) )
      ++input;
    return input;
  }
  
}//namespace

namespace SpecUtils
{
  bool istarts_with( const std::string &line, const char *label )
  {
    const size_t len1 = line.size();
    const size_t len2 = strlen(label);
    
    if( len1 < len2 )
      return false;
    
    const bool answer = ::rapidxml::internal::compare( line.c_str(), len2, label, len2, false );
    
#if(PERFORM_DEVELOPER_CHECKS)
    const bool correctAnswer = boost::algorithm::istarts_with( line, label );
    
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
  }//istarts_with(...)
  
  bool istarts_with( const std::string &line, const std::string &label )
  {
    const size_t len1 = line.size();
    const size_t len2 = label.size();
    
    if( len1 < len2 )
      return false;
    
    const bool answer = ::rapidxml::internal::compare( line.c_str(), len2, label.c_str(), len2, false );
    
#if(PERFORM_DEVELOPER_CHECKS)
    const bool correctAnswer = boost::algorithm::istarts_with( line, label );
    
    if( answer != correctAnswer )
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
    
    if( len1 < len2 )
      return false;
    
    const bool answer = ::rapidxml::internal::compare( line.c_str(), len2, label, len2, true );
    
#if(PERFORM_DEVELOPER_CHECKS)
    const bool correctAnswer = boost::algorithm::starts_with( line, label );
    
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
  }//istarts_with(...)
  
  
  bool iends_with( const std::string &line, const std::string &label )
  {
    const size_t len1 = line.size();
    const size_t len2 = label.size();
    
    if( len1 < len2 )
      return false;
    
    const char * const lineend = line.c_str() + (len1 - len2);
    
    const bool answer = ::rapidxml::internal::compare( lineend, len2, label.c_str(), len2, false );
    
#if(PERFORM_DEVELOPER_CHECKS)
    const bool correctAnswer = boost::algorithm::iends_with( line, label );
    
    if( answer != correctAnswer )
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
  
  
  
  
  
  
  template <class T>
  bool split_to_integral_types( const char *input, const size_t length,
                               std::vector<T> &results )
  {
    static_assert( std::is_same<T,int>::value || std::is_same<T,long long>::value,
                  "split_to_integral_types only implemented for int and long long" );
    
    if( !input || !length )
      return true;
    
    namespace qi = boost::spirit::qi;
    
    const char *begin = input;
    const char *end = begin + length;
    
#if( BOOST_VERSION < 104500 )
    errno = 0;
    const string inputstr(input, input+length);
    const char *delims = ", ";
    size_t prev_delim_end = 0;
    size_t delim_start = inputstr.find_first_of( delims, prev_delim_end );
    bool ok = true;
    while( ok && delim_start != std::string::npos )
    {
      if( (delim_start-prev_delim_end) > 0 )
      {
        T val;
        if( std::is_same<T,int>::value )
          val = strtol(inputstr.c_str()+prev_delim_end,nullptr,10);
        else
          val = strtoll(inputstr.c_str()+prev_delim_end,nullptr,10);
        
        /// \TODO: I think errno only gets set when input is out of range; should also check if string length then value is not zero
        if( errno )
          ok = false;
        results.push_back( val );
      }
      
      prev_delim_end = inputstr.find_first_not_of( delims, delim_start + 1 );
      if( prev_delim_end != std::string::npos )
        delim_start = inputstr.find_first_of( delims, prev_delim_end + 1 );
      else
        delim_start = std::string::npos;
    }//while( this_pos < input.size() )
    
    if(ok && prev_delim_end < inputstr.size() )
    {
      T val;
      if( std::is_same<T,int>::value )
        val = strtol(inputstr.c_str()+prev_delim_end,nullptr,10);
      else
        val = strtoll(inputstr.c_str()+prev_delim_end,nullptr,10);
      
      /// \TODO: I think errno only gets set when input is out of range; should also check if string length then value is not zero
      if( !errno )
        results.push_back( val );
    }
    
    return ok;
#else
    
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
    
    if( !input || !length )
      return true;
    
    results.reserve( std::min( length/2, size_t(65536) ) );
    
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
          if( fabs(a-b) > 0.000001*(std::max(fabs(a),fabs(b)) ))
          {
            char errormsg[1024];
            snprintf( errormsg, sizeof(errormsg),
                     "Out of tolerance diffence for floats %.9g using "
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
  }//bool split_to_floats(...)
  
  
  bool parse_double( const char *input, const size_t length, double &result )
  {
    namespace qi = boost::spirit::qi;
    
    const char *begin = input;
    const char *end = begin + length;
    
    result = 0.0;
    const bool ok = qi::phrase_parse( begin, end, qi::double_, qi::space, result );
    
    return ok;
  }


  bool parse_float( const char *input, const size_t length, float &result )
  {
    namespace qi = boost::spirit::qi;
    
    const char *begin = input;
    const char *end = begin + length;
    
    result = 0.0f;
    const bool ok = qi::phrase_parse( begin, end, qi::float_, qi::space, result );
    
    //  if( ok && (begin != end) )
    //    return false;
    
    return ok;
  }
  

  bool parse_int( const char *input, const size_t length, int &result )
  {
    namespace qi = boost::spirit::qi;
    
    const char *begin = input;
    const char *end = begin + length;
    
    result = 0;
    const bool ok = qi::phrase_parse( begin, end, qi::int_, qi::space, result );
    
    //  if( ok && (begin != end) )
    //    return false;
    
    return ok;
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
    
    // PARSE_CONVERT_METHOD==1 is strtod
    // PARSE_CONVERT_METHOD==2 is using boost::spirit::qi::parse
    // PARSE_CONVERT_METHOD==3 is using strtok_r and atof
    //  If option 3 is selected, then the signture of this function must be changed
    //    from a const char *.
    //  Also, methods 1 and 3 both implicitly add all the whitespaces to the
    //    delimiters.
    //  I think option 2 is the only 'correct' implementation, although the others
    //  are close enough for parsing spectrum files. Options 1 and 3 became
    //  depreciated 20151226
    
#define PARSE_CONVERT_METHOD 2
    
    const size_t input_size = contents.size();
    if( input_size )
    {
      contents.clear();
      contents.reserve( std::min( input_size/2, size_t(65536) ) );
    }//if( input_size )
    
    if( !input || !(*input) )
      return false;
    
#if( PARSE_CONVERT_METHOD == 1 )
    errno = 0;
    const char *pos = input;
    char *nextpos = input;
    pos = next_word( nextpos, delims );
    
    do
    {
      const char d = *pos;
      if( !isdigit(d) && d != '+' && d != '-' && d != '.' )
        break;
      
      float value = static_cast<float>( strtod( pos, &nextpos ) );
      //    cerr << "pos=" << (void *)pos << ", nextpos=" << (void *)nextpos << " and " << *nextpos << endl;
      
      if( errno )
      {
#if(PERFORM_DEVELOPER_CHECKS)
        char errormsg[1024];
        char strpart[128] = { 0 };
        for( int c = 0; c < 127 && pos[c]; ++c )
          strpart[c] = pos[c];
        strpart[127] = '\0';
        
        snprintf( errormsg, sizeof(errormsg),
                 "Couldnt convert string '%s' to a float using strtod(), error %i",
                 strpart, errno );
        log_developer_error( __func__, errormsg );
#endif
        return false;
      }//if( errno )
      
      if( cambio_zero_compress_fix && (value == 0.0) )
      {
        const char nextchar[2] = { *(pos+1), '\0' };
        if( !strstr(delims, nextchar) )  //If the next char is a delimeter, then we dont wantto apply the fix, otherwise apply the fix
          value = FLT_MIN;
      }//if( value == 0.0 )
      
      contents.push_back( value );
      
      pos = next_word( nextpos, delims );
    }while( pos && (*pos) && (pos != nextpos) );
    
#elif( PARSE_CONVERT_METHOD == 2 )
    
    const char *pos = input;
    
    while( *pos )
    {
      pos = next_word( pos, delims );
      if( *pos == '\0' )
        return true;
      
      const char * const start_pos = pos;
      const char *end = end_of_word( pos, delims );
      
      //Using a double here instead of a float causes about a 2.5% slow down.
      //  Using a float would be fine, but then you hit a limitation
      //  that the value before decimal point must be less than 2^32 (note, we are
      //  unlikely to ever see this in channel counts).
      double value;
      const bool ok = boost::spirit::qi::parse( pos, end, boost::spirit::qi::double_, value );
      
      
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
          snprintf( errormsg, sizeof(errormsg), "Trailing unpased string '%s'",
                   string(pos,end).c_str() );
          log_developer_error( __func__, errormsg );
        }//if( begin != end )
        
        const float a = static_cast<float>( value );
        const float b = (float) atof( string(start_pos, end).c_str() );
        if( fabs(a-b) > 0.000001f*(std::max(fabs(a),fabs(b)) ))
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
      
      //If the next char is a delimeter, then we dont wantto apply the fix, otherwise apply the fix
      if( cambio_zero_compress_fix && (value == 0.0)
         && !is_in( *(start_pos+1), delims ) )
      {
        value = FLT_MIN;
      }//if( value == 0.0 )
      
      contents.push_back( static_cast<float>(value) );
    }//while( *pos )
    
#elif( PARSE_CONVERT_METHOD == 3 )
    errno = 0;
    float value;
    char *pos_ptr = NULL;
    
    // Branches for Windows; strtok_r is on POSIX systems. strtok_s is the Windows equivalent.
#ifdef _WIN32
    char *value_str = strtok_s( input, delims, &pos_ptr );
#else
    char *value_str = strtok_r( input, delims, &pos_ptr );
#endif
    
    while( value_str != NULL )
    {
      //  XXX: using stringstream to convert the double makes split_to_floats(...)
      //       take about 40% of the time of parsing an ICD1 file (the example
      //       Passthrough) - using atof reduces this to 17.9% (and further down to
      //       14% if 'contents' is passed in with space already reserved).
      //       Running on a number of ICD1 files showed no errors using the
      //       stringstream implementation, so will continue using atof.
      //       Now approx 40% of time in this function is due to
      //       vector<float>::push_back(...), and 50% to atof(...); 11% to strtok_r
      //
      //    if( !(stringstream(value_str) >> value) )
      //      cerr << "Error converting '" << value_str << "' to float" << endl;
      //
      //Note that atof discards initial white spaces, meaning the functionality
      //  of this function is not correct if the delimiters dont include all the
      //  white space characters.
      //    errno = (sscanf(value_str, "%f", &value) != 1);
      //    value = fast_atof( value_str );
      //    errno = !naive_atof( value, value_str );
      value = (float)atof( value_str );
      //    value = (float)strtod( value_str, NULL );
      /*
       {
       //find next delim or end
       const char *end = end_of_word( value_str, delims );
       errno = !boost::spirit::qi::parse( (const char *)value_str, end, boost::spirit::qi::float_, value );
       value_str = (char *)end;
       }
       */
      if( errno )
      {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
        char errormsg[1024];
        snprintf( errormsg, sizeof(errormsg),
                 "Couldnt convert string '%s' to a float using atof(), error %i",
                 value_str, errno );
        log_developer_error( __func__, errormsg );
#endif
        return false;
      }//if( errno )
      
      
      //XXX
      //  In Cambio N42 files zeros written like "0" indicates zeroes compression,
      //  a zero written like "0.000", "-0.0", etc are all a sinle bin with
      //  content zero - so for this case well substiture in a really small number
      if( cambio_zero_compress_fix && (value == 0.0) )
      {
        if( strlen(value_str) > 1 )
          value = FLT_MIN; //std::numeric_limits<float>::min();
      }//if( value == 0.0 )
      
      contents.push_back( value );
      
      // Branches for Windows; strtok_r is on POSIX systems. strtok_s is the Windows equivalent.
#ifdef _WIN32
      value_str = strtok_s( NULL, delims, &pos_ptr );
#else
      value_str = strtok_r( NULL, delims, &pos_ptr );
#endif
    }//while( pch != NULL )
    
#else
#error Invalid parse method specified
#endif
    
    return true;
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
  

  std::string sequencesToBriefString( const std::set<int> &sample_numbers )
  {
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
    //  There was no accompaning lincense information, and since I found many
    //  similar-but-seperate implementations on the internet, I take the code in
    //  this function to be licensed under a 'do-what-you-will' public domain
    //  license. --Will Johnson 20100824
    
    
    //This function is case insensitive.
    if( !max_str_len )
      return 0;
    
    const size_t n = std::min( source.length(), max_str_len );
    const size_t m = std::min( target.length(), max_str_len );
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
        size_t cell = min( above + 1, min(left + 1, diag + cost));
        
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
