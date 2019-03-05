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

// Block out some warnings occurring in xutility.
// warning C4996: function call with parameters that may be unsafe -
#pragma warning(disable:4996)

#include <ctime>
#include <string>
#include <vector>
#include <locale>
#include <limits>
#include <time.h>
#include <ctype.h>
#include <stdio.h>
#include <fstream>
#include <errno.h>
#include <string.h>
#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <algorithm>

#if(PERFORM_DEVELOPER_CHECKS)
#include <mutex>
#endif

#include "rapidxml/rapidxml.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <Lmcons.h>
#include <direct.h>
#include <io.h>
#elif __APPLE__
#include <sys/time.h>
#include <sys/sysctl.h>
#include <dirent.h>
#else
#include <sys/time.h>
#include <unistd.h>
#include <dirent.h>
#endif

#include <boost/version.hpp>

#if( SpecUtils_NO_BOOST_LIB )
#ifndef _WIN32
#include <libgen.h>
#else
#include <shlwapi.h>
//We will need to link to Shlawapi.lib, which I think uncommenting the next line would do... untested
//#pragma comment(lib, "Shlwapi.lib")
#endif
#endif

//#if( SpecUtils_NO_BOOST_LIB )
#include <sys/stat.h>
//#endif

#if( !SpecUtils_NO_BOOST_LIB )
#include <boost/filesystem.hpp>
#endif


#if( BOOST_VERSION < 104500 )
#include <boost/config/warning_disable.hpp>
#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/phoenix_core.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/spirit/include/phoenix_stl.hpp>
#endif

#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <boost/fusion/adapted.hpp>
#include <boost/spirit/include/qi.hpp>

#include "SpecUtils/UtilityFunctions.h"

#if( ANDROID )
#include <jni.h>
#include <sys/param.h>
#endif

#if( USE_HH_DATE_LIB )
#include "3rdparty/date_2cb4c34/include/date/date.h"
#endif

#if( defined(_MSC_VER) && _MSC_VER <= 1700 )
#define strtoll _strtoi64
#endif

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
  
  const char *month_number_to_Str( const int month )
  {
    switch( month )
    {
      case 1: return "Jan";
      case 2: return "Feb";
      case 3: return "Mar";
      case 4: return "Apr";
      case 5: return "May";
      case 6: return "Jun";
      case 7: return "Jul";
      case 8: return "Aug";
      case 9: return "Sep";
      case 10: return "Oct";
      case 11: return "Nov";
      case 12: return "Dec";
    }
    return "";
  }//const char *month_number_to_Str( const int month )
}//namespace


namespace UtilityFunctions
{
  //Templated boost functions used multiple times tend to take up a ton of space
  //  in the final executable, so to keep it so that only one instaniation of
  //  each comonly used boost function is made, we'll force this with the below.
  //It also makes it a bit easier to eliminate boost from the dependancies later
  //  on if this is desired.
  //This results in a reduction of the generated code size for this file by
  // 71.1 kb on Win74 MinSizeRel
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
    log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
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
    log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
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
  const bool correctAnswer = boost::algorithm::istarts_with( line, label );
  
  if( answer != correctAnswer )
  {
    char errormsg[1024];
    snprintf( errormsg, sizeof(errormsg),
             "Got %i when should have got %i for label '%s' and string '%s'",
             int(answer), int(correctAnswer), label, line.c_str() );
    log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
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
    log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
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
    log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
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
    log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
  }//if( answer != correctAnswer )
#endif
    
  return answer;
}//contains(...)
  
bool iequals( const char *str, const char *test )
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
    log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
  }//if( answer != correctAnswer )
#endif
    
  return answer;
}//bool iequals
  
bool iequals( const std::string &str, const char *test )
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
    log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
  }//if( answer != correctAnswer )
#endif
  
  return answer;
}
  
bool iequals( const std::string &str, const std::string &test )
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
    log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
  }//if( answer != correctAnswer )
#endif
  
  return answer;
}
  
#if( defined(WIN32) && _DEBUG )
//I get the MSVC debug runtime asserts on for character values less than -1, which since chars are signed by default
//  happens on unicode characters.  So I'll make a crappy workaround
//  https://social.msdn.microsoft.com/Forums/vstudio/en-US/d57d4078-1fab-44e3-b821-40763b119be0/assertion-in-isctypec?forum=vcgeneral
bool not_whitespace( char c )
{
  return !rapidxml::internal::lookup_tables<0>::lookup_whitespace[static_cast<unsigned char>(c)];
}
#endif


  // trim from start
static inline std::string &ltrim(std::string &s)
{
#if( defined(WIN32) && _DEBUG )
  s.erase( s.begin(), std::find_if( s.begin(), s.end(), &not_whitespace ) );
#else
  s.erase( s.begin(), std::find_if( s.begin(), s.end(), std::not1( std::ptr_fun<int, int>( std::isspace ) ) ) );
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
#if( defined(WIN32) && _DEBUG )
  s.erase( std::find_if( s.rbegin(), s.rend(), &not_whitespace ).base(), s.end() );
#else
  s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
#endif  
  //remove null terminating characters.  Boost doesnt do this, but is
  //  necassary when reading fixed width binary data.
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
#if(PERFORM_DEVELOPER_CHECKS)
  string copystr = s;
  boost::algorithm::trim( copystr );
#endif
  
  ltrim( rtrim(s) );
  
#if(PERFORM_DEVELOPER_CHECKS)
  size_t pos = copystr.find_first_not_of( '\0' );
  if( pos != 0 && pos != string::npos )
    copystr.erase( copystr.begin(), copystr.begin() + pos );
  else if( pos == string::npos )
    copystr.clear();
  pos = copystr.find_last_not_of( '\0' );
  if( pos != string::npos && (pos+1) < s.size() )
    copystr.erase( copystr.begin() + pos + 1, copystr.end() );
  
  if( copystr != s )
  {
    char errormsg[1024];
    snprintf( errormsg, sizeof(errormsg),
             "Trimmed strings not equal expect: '%s' (len %i), got: '%s' (len %i, from boost)",
             s.c_str(), int(s.size()), copystr.c_str(), int(copystr.size()) );
    log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
  }
#endif
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
      
      log_developer_error( BOOST_CURRENT_FUNCTION, errormsg.str().c_str() );
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
      
      log_developer_error( BOOST_CURRENT_FUNCTION, errormsg.str().c_str() );
    }//if( resutls != coorectResults )
#endif
  }//void split_no_delim_compress(...)
  
  
  void to_lower( string &input )
  {
#if(PERFORM_DEVELOPER_CHECKS)
    string strcopy = input;
    boost::algorithm::to_lower( strcopy );
#endif
    
    for( size_t i = 0; i < input.size(); ++i )
      input[i] = static_cast<char>( tolower(input[i]) );
    
#if(PERFORM_DEVELOPER_CHECKS)
    if( strcopy != input )
    {
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg),
               "Failed to lowercase string.  Expected: '%s', got: '%s'",
               strcopy.c_str(), input.c_str() );
      log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
    }
#endif
  }//to_lower(...)
  
  std::string to_lower_copy( std::string input )
  {
    to_lower( input );
    return input;
  }
  
  void to_upper( string &input )
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
      log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
    }
#endif
  }//void to_upper( string &input )
  
  void ireplace_all( std::string &input, const char *pattern, const char *replacement )
  {
    //This function does not handle UTF8!
    if( input.empty() )
      return;

#if(PERFORM_DEVELOPER_CHECKS)
    string strcopy = input, original = input;
    size_t reslen = strcopy.size() + 1;
    while( reslen != strcopy.size() )
    {
      reslen = strcopy.size();
      boost::algorithm::ireplace_all( strcopy, pattern, replacement );
    }
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

#if(PERFORM_DEVELOPER_CHECKS)
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
      
      log_developer_error( BOOST_CURRENT_FUNCTION, msg.str().c_str() );
    }
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
      
      log_developer_error( BOOST_CURRENT_FUNCTION, msg.str().c_str() );
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
      c = *it;
      
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
      // 0x80 --> 10000000
      // 0xC0 --> 11000000
      if( !(c & 0x80) || ((c & 0xC0) == 0xC0))
        break;
    }
    
    return res;
  }//size_t utf8_str_iterator( IterType& it, const IterType& last )
  
  
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
  }//size_t utf8_str_iterator( IterType& it, const IterType& last )
  
  
  size_t utf8_str_len( const char * const str, size_t str_size_bytes )
  {
    size_t len = 0;
    
    if( !str_size_bytes )
    {
      for( const char *ptr = str; *ptr; utf8_iterate(ptr) )
        ++len;
    }else
    {
      for( const char *ptr = str, * const end = str + str_size_bytes;
           ptr != end; utf8_iterate(ptr, end) )
        ++len;
    }
    
    return len;
  }//size_t utf8_str_len( const char * const str, size_t str_size_bytes )
  
  
  void utf8_limit_str_size( std::string &str, const size_t max_bytes )
  {
    const size_t pos = utf8_str_size_limit( str.c_str(), str.size(), max_bytes );
    str = str.substr( 0, pos );
  }
  
  
  size_t utf8_str_size_limit( const char *str,
                              size_t len, const size_t max_bytes )
  {
    if( !len )
      len = strlen( str );
    
    if( !len || !max_bytes )
      return 0;
  
    if( len < max_bytes )
      return len;
    
    const char *iter = str + max_bytes - 1;
    for( ; iter != str; --iter )
    {
      if( ((*iter) & 0xC0) == 0xC0 )
        break;
    }
    
    return iter - str;
  }
  
  
  

  std::string to_common_string( const boost::posix_time::ptime &t, const bool twenty_four_hour )
  {
    if( t.is_special() )
      return "not-a-date-time";
    
    const int year = static_cast<int>( t.date().year() );
    const int day = static_cast<int>( t.date().day() );
    int hour = static_cast<int>( t.time_of_day().hours() );
    const int mins = static_cast<int>( t.time_of_day().minutes() );
    const int secs = static_cast<int>( t.time_of_day().seconds() );
    
    bool is_pm = (hour >= 12);
    
    if( !twenty_four_hour )
    {
      if( is_pm )
        hour -= 12;
      if( hour == 0 )
        hour = 12;
    }
    
    const char * const month = month_number_to_Str( t.date().month() );
    
    char buffer[64];
    snprintf( buffer, sizeof(buffer), "%i-%s-%04i %02i:%02i:%02i%s",
             day, month, year, hour, mins, secs, (twenty_four_hour ? "" : (is_pm ? " PM" : " AM")) );
    
    return buffer;
  }//to_common_string

  
  std::string to_vax_string( const boost::posix_time::ptime &t )
  {
    //Ex. "2014-Sep-19 14:12:01.62"
    if( t.is_special() )
      return "";
    
    const int year = static_cast<int>( t.date().year() );
    const char * const month = month_number_to_Str( t.date().month() );
    const int day = static_cast<int>( t.date().day() );
    const int hour = static_cast<int>( t.time_of_day().hours() );
    const int mins = static_cast<int>( t.time_of_day().minutes() );
    const int secs = static_cast<int>( t.time_of_day().seconds() );
    const int hundreth = static_cast<int>( 0.5 + ((t.time_of_day().total_milliseconds() % 1000) / 10.0) ); //round to neares hundreth of a second
    
    char buffer[32];
    snprintf( buffer, sizeof(buffer), "%02i-%s-%04i %02i:%02i:%02i.%02i",
             day, month, year, hour, mins, secs, hundreth );
    
#if(PERFORM_DEVELOPER_CHECKS)
    if( strlen(buffer) != 23 )
    {
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg),
               "Vax format of '%s' is '%s' which is not the expected length of 23, but %i",
                to_extended_iso_string(t).c_str(), buffer, static_cast<int>(strlen(buffer)) );
      log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
    }
#endif
    
    return buffer;
  }//std::string to_vax_string( const boost::posix_time::ptime &t );
  
  
  std::string print_to_iso_str( const boost::posix_time::ptime &t,
                               const bool extended )
  {
    //#if( SpecUtils_NO_BOOST_LIB )
    if( t.is_special() )
      return "not-a-date-time";
    //should try +-inf as well
    
    const int year = static_cast<int>( t.date().year() );
    const int month = static_cast<int>( t.date().month() );
    const int day = static_cast<int>( t.date().day() );
    const int hour = static_cast<int>( t.time_of_day().hours() );
    const int mins = static_cast<int>( t.time_of_day().minutes() );
    const int secs = static_cast<int>( t.time_of_day().seconds() );
    double frac = t.time_of_day().fractional_seconds()
    / double(boost::posix_time::time_duration::ticks_per_second());
    
    char buffer[256];
    if( extended ) //"2014-04-14T14:12:01.621543"
      snprintf( buffer, sizeof(buffer),
               "%i-%.2i-%.2iT%.2i:%.2i:%09.6f",
               year, month, day, hour, mins, (secs+frac) );
    else           //"20140414T141201.621543"
      snprintf( buffer, sizeof(buffer),
               "%i%.2i%.2iT%.2i%.2i%09.6f",
               year, month, day, hour, mins, (secs+frac) );

#if(PERFORM_DEVELOPER_CHECKS)
    string tmpval = buffer;
    if( tmpval.find(".") == string::npos )
    {
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg),
               "Expected there to be a '.' in iso date time: '%s'", buffer );
      log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
    }
#endif

    const char point = '.';
//    const char point = std::use_facet< std::numpunct<char> >(std::cout.getloc()).decimal_point();
    
    //Get rid of trailing zeros
    size_t result_len = strlen(buffer) - 1;
    while( result_len > 1 && buffer[result_len]=='0' )
      buffer[result_len--] = '\0';
    
    if( result_len > 1 && buffer[result_len]==point )
      buffer[result_len--] = '\0';
    
#if(PERFORM_DEVELOPER_CHECKS)
    string correctAnswer = extended
                            ? boost::posix_time::to_iso_extended_string( t )
                            : boost::posix_time::to_iso_string( t );
    
    if( correctAnswer.find(".") != string::npos )
    {
      result_len = correctAnswer.size() - 1;
      while( result_len > 1 && correctAnswer[result_len]=='0' )
        correctAnswer = correctAnswer.substr( 0, result_len-- );
    
      if( result_len > 1 && buffer[result_len]==point )
        correctAnswer = correctAnswer.substr( 0, result_len-- );
    }//if( correctAnswer.find(".") != string::npos )
    
    if( correctAnswer != buffer )
    {
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg),
               "Failed to format date correctly for %sextended iso format. Expected: '%s', got: '%s'",
               (extended ? "" : "non-"),
               correctAnswer.c_str(), buffer );
      log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
    }
#endif
    
    
    return buffer;
  }//std::string to_extended_iso_string( const boost::posix_time::ptime &t )
  
std::string to_extended_iso_string( const boost::posix_time::ptime &t )
{
  return print_to_iso_str( t, true );
}
  
std::string to_iso_string( const boost::posix_time::ptime &t )
{
  return print_to_iso_str( t, false );
}//std::string to_iso_string( const boost::posix_time::ptime &t )
  

boost::posix_time::ptime time_from_string( const char *time_string )
{
#define CREATE_datetimes_TEST_FILE 0
  
#if( CREATE_datetimes_TEST_FILE )
  static std::mutex datetimes_file_mutex;
  static int ntimes_called = 0;
  string inputstr = time_string;
  UtilityFunctions::ireplace_all(inputstr, ",", "");
  if( inputstr.size() && inputstr[0]=='#' )
    inputstr = inputstr.substr(1);
  
  //#ifndef _WIN32
  auto result = time_from_string_strptime( time_string, MiddleEndianFirst );
  //#else
  //    auto result = time_from_string_boost( time_string );
  //#endif
  
  if( inputstr.empty() )
    return result;
  
  std::lock_guard<std::mutex> lock( datetimes_file_mutex );
  ++ntimes_called;
  if( (ntimes_called % 1000) == 0 )
    cerr << "Warning, time_from_string() is creating datetimes.txt test file" << endl;
  
  ofstream output( "datetimes.txt", ios::out | ios::app );
  
  if( output )
    output << inputstr << "," << UtilityFunctions::to_extended_iso_string(result) << "\r\n";
  else
    cerr << "Failed to open datetimes.txt for appending" << endl;
  
  return result;
#else  //CREATE_datetimes_TEST_FILE
  
  return time_from_string_strptime( time_string, MiddleEndianFirst );
#endif
}//boost::posix_time::ptime time_from_string( const char *time_string )
  

  
#if( USE_HH_DATE_LIB )
template <typename Clock, typename Duration>
std::tm to_calendar_time(std::chrono::time_point<Clock, Duration> tp)
{
  using namespace date;
  auto date = floor<days>(tp);
  auto ymd = year_month_day(date);
  //auto weekday = year_month_weekday(date).weekday_indexed().weekday();
  auto weekday = year_month_weekday(date).weekday_indexed().index();
  auto tod = make_time(tp - date);
  days daysSinceJan1 = date - sys_days(ymd.year()/1/1);
  
  std::tm result;
  std::memset(&result, 0, sizeof(result));
  result.tm_sec   = tod.seconds().count();
  result.tm_min   = tod.minutes().count();
  result.tm_hour  = tod.hours().count();
  result.tm_mday  = unsigned(ymd.day());
  result.tm_mon   = unsigned(ymd.month()) - 1u; // Zero-based!
  result.tm_year  = int(ymd.year()) - 1900;
  result.tm_wday  = unsigned(weekday);
  result.tm_yday  = daysSinceJan1.count();
  result.tm_isdst = -1; // Information not available
  return result;
}
  
#else //USE_HH_DATE_LIB
  
#if( defined(WIN32) )
#define timegm _mkgmtime
#endif
  
bool strptime_wrapper( const char *s, const char *f, struct tm *t )
{
  //For the testTimeFromString unit test on my mac, the native take strptime
  //  takes 302188us to run, vs the c++11 version taking 4113835us.
  //  ~10 times slower, so preffer native strptime where available.
//#if( defined(_MSC_VER) && _MSC_VER < 1800 )
#if( defined(_MSC_VER) )  //Doesnt look like MSVS 2017 has strptime.
  #define HAS_NATIVE_STRPTIME 0
#else
  #define HAS_NATIVE_STRPTIME 1
#endif
  
#if( HAS_NATIVE_STRPTIME )
  return (strptime(s,f,t) != nullptr);
#else
  //*t = std::tm();  //should already be done
  //memset( t, 0, sizeof(*t) );  //Without this some tm fields dont make sense for some formats...


#if(defined(WIN32))
  //see https://developercommunity.visualstudio.com/content/problem/18311/stdget-time-asserts-with-istreambuf-iterator-is-no.html
  //if( strlen( f ) < strlen( s ) )
  //  return false;
#endif

  //Arg! Windows VS2017 get_time(...) doesnt say it fails when it should!

  std::istringstream input( s );
  input.imbue( std::locale( setlocale( LC_ALL, nullptr ) ) );
  input >> std::get_time( t, f );
  if( input.fail() )
    return false;

  //cout << "Format '" << f << "' parsed '" << s << "' to " << std::put_time(t, "%c") << endl;
  /*
   cout
   << "seconds after the minute [0-60]: " << t.tm_sec << endl
   << "minutes after the hour [0-59]: " << t.tm_min << endl
   << "hours since midnight [0-23]: " << t.tm_hour << endl
   << "day of the month [1-31]: " << t.tm_mday << endl
   << "months since January [0-11]: " << t.tm_mon << endl
   << "years since 1900: " << t.tm_year << endl
   << "days since Sunday [0-6]: " << t.tm_wday << endl
   << "days since January 1 [0-365]: " << t.tm_yday << endl
   << "Daylight Savings Time flag: " << t.tm_isdst << endl
   << "offset from UTC in seconds: " << t.tm_gmtoff << endl
   //<< "timezone abbreviation: " << (t.tm_zone ? (const char *)t.tm_zone : "null")
   << endl;
   */
  
  return true;
#endif
}//char *strptime_wrapper(...)
#endif //USE_HH_DATE_LIB
  
  boost::posix_time::ptime time_from_string_strptime( std::string time_string,
                                                     const DateParseEndianType endian )
  {
#if(PERFORM_DEVELOPER_CHECKS)
	  const string develop_orig_str = time_string;
#endif
    UtilityFunctions::to_upper( time_string );  //make sure strings like "2009-11-10t14:47:12" will work (some file parsers convert to lower case)
    UtilityFunctions::ireplace_all( time_string, "  ", " " );
    UtilityFunctions::ireplace_all( time_string, "_T", "T" );  //Smiths NaI HPRDS: 2009-11-10_T14:47:12Z
    UtilityFunctions::trim( time_string );
    
    //Replace 'T' with a space to almost cut the number of formats to try in
    //  half.  We could probably do a straight replacement of 'T', btu we'll be
    //  conservative and make it have a number on both sides of it (e.g.,
    //  seperating the date from the time).
    auto tpos = time_string.find( 'T' );
    while( tpos != string::npos )
    {
      if( (tpos > 0) && ((tpos + 1) < time_string.size())
         && isdigit( time_string[tpos - 1] ) && isdigit( time_string[tpos + 1] ) )
        time_string[tpos] = ' ';
      tpos = time_string.find( 'T', tpos + 1 );
    }
    
    
    //strptime(....) cant handle GMT offset (ex '2014-10-24T08:05:43-04:00')
    //  so we will do this manually.
    boost::posix_time::time_duration gmtoffset(0,0,0);
    const size_t offsetcolon = time_string.find_first_of( ':' );
    if( offsetcolon != string::npos )
    {
      const size_t signpos = time_string.find_first_of( "-+", offsetcolon+1 );
      if( signpos != string::npos )
      {
        //Note: the + and - symbols are in the below for dates like:
        //  '3-07-31T14:25:30-03:-59'
        const size_t endoffset = time_string.find_first_not_of( ":0123456789+-", signpos+1 );
        const string offset = endoffset==string::npos ? time_string.substr(signpos)
        : time_string.substr(signpos,endoffset-signpos-1);
        string normal = time_string.substr(0,signpos);
        if( endoffset != string::npos )
          normal += time_string.substr( endoffset );
        
        //normal will look like "2014-10-24T08:05:43"
        //offset will look like "-04:00"
        try
        {
          gmtoffset = boost::posix_time::duration_from_string(offset);
        }catch( std::exception &e )
        {
          cerr << "Failed to convert '" << offset << "' to time duration, from string '" << time_string << "'" << endl;
        }
        
        
        //Should also make sure gmtoffset is a reasonalbe value.
        
        //      cout << "offset for '" << time_string << "' is '" << offset << "' with normal time '" <<  normal<< "'" << endl;
        time_string = normal;
      }//if( signpos != string::npos )
    }//if( offsetcolon != string::npos )
    
#if ( defined(WIN32) || defined(UNDER_CE) || defined(_WIN32) || defined(WIN64) )
	//Right now everything is uppercase; for at least MSVC 2012, abreviated months, such as
	//  "Jan", "Feb", etc must start with a capital, and be followed by lowercase
	//  We could probably use some nice regex, but whatever for now
	auto letter_start = time_string.find_first_of( "JFMASOND" );
	if( (letter_start != string::npos) && ((letter_start+2) < time_string.length()) )
	{
	  if( time_string[letter_start+1] != 'M' )
	  {
	    for( size_t i = letter_start+1; i < time_string.length() && isalpha(time_string[i]); ++i )
		  time_string[i] = static_cast<char>( tolower(time_string[i]) );
	  }
	}
#endif

    //strptime(....) cant handle fractions of second (because tm doesnt either)
    //  so we will manually convert fractions of seconds.
    boost::posix_time::time_duration fraction(0,0,0);
    //int fraction_nano_sec = 0;
    
    const size_t fraccolon = time_string.find_last_of( ':' );
    if( fraccolon != string::npos )
    {
      const size_t period = time_string.find( '.', fraccolon+1 );
      if( period != string::npos )
      {
        const size_t last = time_string.find_first_not_of( "0123456789", period+1 );
        string fracstr = ((last!=string::npos)
                          ? time_string.substr(period+1,last-period-1)
                          : time_string.substr(period+1));
        
        //Assume microsecond resolution at the best (note
        //  boost::posix_time::nanosecond isnt available on my OS X install)
        const size_t ndigits = 9;
        const size_t invfrac = 1E9;
        const size_t nticks = boost::posix_time::time_duration::ticks_per_second();
        
        if( fracstr.size() < ndigits )
          fracstr.resize( ndigits, '0' );
        else if( fracstr.size() > ndigits )
          fracstr.insert( ndigits, "." );
        
        int numres = 0;  //using int will get rounding wrong
        if( (stringstream(fracstr) >> numres) )
        {
          //fraction_nano_sec = numres;
          fraction = boost::posix_time::time_duration(0,0,0, numres*nticks/invfrac);
        }else
          cerr << "Failed to convert fraction '" << fracstr << "' to double" << endl;
        
        string normal = time_string.substr(0,period);
        if( last != string::npos )
          normal += time_string.substr(last);
        time_string = normal;
      }//if( period != string::npos )
    }//if( fraccolon != string::npos )
    
    //With years like 2070, 2096, and such, strptime seems to fail badly, so we
    //  will fix them up a bit.
    //Assumes the first time you'll get four numbers in a row, it will be the year
    bool add100Years = false;
    if( time_string.size() > 5 )
    {
      for( size_t i = 0; i < time_string.size()-4; ++i )
      {
        if( isdigit(time_string[i]) && isdigit(time_string[i+1])
           && isdigit(time_string[i+2]) && isdigit(time_string[i+3]) )
        {
          int value;
          if( stringstream(time_string.substr(i,4)) >> value )
          {
            //XXX - I havent yet determined what year this issue starts at
            if( value > 2030 && value < 2100 )
            {
              char buffer[8];
              snprintf( buffer, sizeof(buffer), "%i", value - 100 );
              time_string[i] = buffer[0];
              time_string[i+1] = buffer[1];
              time_string[i+2] = buffer[2];
              time_string[i+3] = buffer[3];
              add100Years = true;
            }
          }//if( stringstream(time_string.substr(i,4)) >> value )
          
          break;
        }//if( four numbers in a row )
      }//for( size_t i = 0; i < time_string.size(); ++i )
    }//if( time_string.size() > 5 )

    //middle: whether to try a middle endian (date start with month number)
    //  date decoding first, or alternetavely little endian (date start with day
    //  number).  Both endians will be tried, this just selects which one first.
    const bool middle = (endian == MiddleEndianFirst);
    const bool only = (endian == MiddleEndianOnly || endian == LittleEndianOnly);
    

    //  Should go through list of formats listed in http://www.partow.net/programming/datetime/index.html
    //  and make sure they all parse.
    const char * const formats[] =
    {
      "%d-%b-%y%n%r", //'15-MAY-14 08:30:44 PM'  (disambiguos: May 15 2014)
      "%Y-%m-%d%n%H:%M:%SZ", //2010-01-15T23:21:15Z
      "%Y-%m-%d%n%H:%M:%S", //2010-01-15 23:21:15
      "%d-%b-%Y%n%r",       //1-Oct-2004 12:34:42 AM  //"%r" ~ "%I:%M:%S %p"
      (middle ? "%m/%d/%Y%n%r" : "%d/%m/%Y%n%r"), //1/18/2008 2:54:44 PM
      (only ? "" : (middle ? "%d/%m/%Y%n%r" : "%m/%d/%Y%n%r")),
      (middle ? "%m/%d/%Y%n%H:%M:%S" : "%d/%m/%Y%n%H:%M:%S"), //08/05/2014 14:51:09
      (only ? "" : (middle ? "%d/%m/%Y%n%H:%M:%S" : "%m/%d/%Y%n%H:%M:%S")),
      (middle ? "%m-%d-%Y%n%H:%M:%S" : "%d-%m-%Y%n%H:%M:%S"), //14-10-2014 16:15:52
      (only ? "" : (middle ? "%d-%m-%Y%n%H:%M:%S" : "%m-%d-%Y%n%H:%M:%S" )),
#if ( defined(WIN32) || defined(UNDER_CE) || defined(_WIN32) || defined(WIN64) )
      (middle ? "%m %d %Y %H:%M:%S" : "%d %m %Y %H:%M:%S"), //14 10 2014 16:15:52
      (only ? "" : (middle ? "%d %m %Y %H:%M:%S" : "%m %d %Y %H:%M:%S")),
#else
	    (middle ? "%m%n%d%n%Y%n%H:%M:%S" : "%d%n%m%n%Y%n%H:%M:%S"), //14 10 2014 16:15:52
      (only ? "" : (middle ? "%d%n%m%n%Y%n%H:%M:%S" : "%m%n%d%n%Y%n%H:%M:%S" )),
#endif
      "%d-%b-%y%n%H:%M:%S", //16-MAR-06 13:31:02, or "12-SEP-12 11:23:30"
      "%d-%b-%Y%n%H:%M:%S", //31-Aug-2005 12:38:04,
      "%d %b %Y%n%H:%M:%S", //31 Aug 2005 12:38:04
      "%d-%b-%Y%n%H:%M:%S%nZ",//9-Sep-2014T20:29:21 Z
      (middle ? "%m-%m-%Y%n%H:%M:%S" : "%d-%m-%Y%n%H:%M:%S" ), //"10-21-2015 17:20:04" or "21-10-2015 17:20:04"
      (only ? "" : (middle ? "%d-%m-%Y%n%H:%M:%S" : "%m-%m-%Y%n%H:%M:%S" )),
      "%d.%m.%Y%n%H:%M:%S",
//      (middle ? "%m.%d.%Y%n%H:%M:%S" : "%d.%m.%Y%n%H:%M:%S"), //26.05.2010 02:53:49
//      (only ? "" : (middle ? "%d.%m.%Y%n%H:%M:%S" : "%m.%d.%Y%n%H:%M:%S")),
      "%b. %d %Y%n%H:%M:%S",//May. 21 2013  07:06:42
      "%d.%m.%y%n%H:%M:%S",
//      (middle ? "%m.%d.%y%n%H:%M:%S" : "%d.%m.%y%n%H:%M:%S"),  //'28.02.13 13:42:47'
      (middle ? "%m.%d.%y%n%H:%M:%S" : "%d.%m.%y%n%H:%M:%S"), //'3.14.06 10:19:36' or '28.02.13 13:42:47'
      (only ? "" : (middle ? "%d.%m.%y%n%H:%M:%S" : "%m.%d.%y%n%H:%M:%S" )),
      (middle ? "%m.%d.%Y%n%H:%M:%S" : "%d.%m.%Y%n%H:%M:%S"), //'3.14.2006 10:19:36' or '28.02.2013 13:42:47'
      (only ? "" : (middle ? "%d.%m.%Y%n%H:%M:%S" : "%m.%d.%Y%n%H:%M:%S" )),
      "%d.%m.%y%n%H:%M:%S",
//      (only ? "" : (middle ? "%d.%m.%y%n%H:%M:%S" : "%m.%d.%y%n%H:%M:%S")),
      "%Y.%m.%d%n%H:%M:%S", //2012.07.28 16:48:02
      "%d.%b.%Y%n%H:%M:%S",//01.Nov.2010 21:43:35
      "%Y%m%d%n%H:%M:%S",  //20100115 23:21:15
      "%Y-%b-%d%n%H:%M:%S", //2017-Jul-07 09:16:37
      "%Y%m%d%n%H%M%S",  //20100115T232115
      (middle ? "%m/%d/%Y%n%I:%M %p" : "%d/%m/%Y%n%I:%M %p"), //11/18/2018 10:04 AM
      (only ? "" : (middle ? "%d/%m/%Y%n%I:%M %p" : "%m/%d/%Y%n%I:%M %p")),
      "%Y-%m-%d%n%H-%M-%S", //2018-10-09T19-34-31_27 (not sure what the "_27" exactly means)
      "%d-%b-%Y",           //"00-Jan-2000 "
      "%Y/%m/%d", //"2010/01/18"
      "%Y-%m-%d" //"2010-01-18"
    };
   

    const size_t nformats = sizeof(formats) / sizeof(formats[0]);
    
    const char *timestr = time_string.c_str();
    
    for( size_t i = 0; i < nformats; ++i )
    {
      struct tm t = std::tm();
      
#if( USE_HH_DATE_LIB )
      if( !formats[i][0] )
        continue;
      
      std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> tm_object{}; /* std::chrono::milliseconds */
      std::istringstream input(timestr);
      input >> date::parse(formats[i], tm_object);
      if( !input.fail() )
      {
        t = to_calendar_time( tm_object );
        return boost::posix_time::from_time_t( timegm(&t) ) + fraction + boost::gregorian::years( add100Years ? 100 : 0 );
      }
#else
      if( formats[i][0] && strptime_wrapper( timestr, formats[i], &t ) )
      {

        //cout << "Format='" << formats[i] << "' worked to give: " 
        //  << print_to_iso_str( boost::posix_time::from_time_t(timegm( &t )) + fraction + boost::gregorian::years( add100Years ? 100 : 0 ), false )
        //  << " time_t=" << timegm(&t)
        //  << endl;


        //if( add100Years )
          //t.tm_year += 100;
        //std::chrono::time_point tp = system_clock::from_time_t( std::mktime(&t) ) + std::chrono::nanoseconds(fraction_nano_sec);
        
        return boost::posix_time::from_time_t( timegm(&t) )
        + fraction
        + boost::gregorian::years( add100Years ? 100 : 0 )
        /*+ gmtoffset*/;  //ignore offset since we want time in local persons zone
      }
      //      return boost::posix_time::from_time_t( mktime(&tm) - timezone + 3600*daylight ) + fraction + gmtoffset;
#endif //USE_HH_DATE_LIB
    }//for( size_t i = 0; i < nformats; ++i )
    
    //cout << "Couldnt parse" << endl;

#if(PERFORM_DEVELOPER_CHECKS)
    if( develop_orig_str.size() > 5 //5 is arbitrary
       && develop_orig_str.find("NA")==string::npos
       && std::count( begin(develop_orig_str), end(develop_orig_str), '0') < 8 )
      log_developer_error( BOOST_CURRENT_FUNCTION, ("Failed to parse dat/time from: '" + develop_orig_str  + "' which was massaged into '" + time_string + "'").c_str() );
#endif
    return boost::posix_time::ptime();
  }//boost::posix_time::ptime time_from_string_strptime( std::string time_string )
//#endif  //#ifndef _WIN32
  

  
std::string temp_dir()
{
#if( ANDROID )
  {
    const char *val = std::getenv("TMPDIR");
    if( !val )
    {
      cerr << "Warning, unable to get \"TMPDIR\" environment variable;"
           << "returning: \"/data/local/tmp/\"" << endl;
      return "/data/local/tmp/";
    }
  
    return val;
  }
#endif
  
#if( !SpecUtils_NO_BOOST_LIB && BOOST_VERSION >= 104700)
  try
  {
    return boost::filesystem::temp_directory_path().generic_string();
  }catch( std::exception &e )
  {
    cerr << "Warning, unable to get a temporary directory...: " << e.what()
         << "\nReturning '/tmp'" << endl;
  }//try/catch
#endif

#if ( defined(WIN32) || defined(UNDER_CE) || defined(_WIN32) || defined(WIN64) )
  //Completely un-tested
  const DWORD len = GetTempPathW( 0, NULL );
  vector<TCHAR> buf( len );
  
  if( !len || !GetTempPath( len, &buf[0] ) )
  {
    const char *val = 0;
    (val = std::getenv("temp" )) ||
    (val = std::getenv("TEMP"));
    
    if( val )
      return val;
    
#if(PERFORM_DEVELOPER_CHECKS)
    log_developer_error( BOOST_CURRENT_FUNCTION, "Couldnt find temp path on Windows" );
#endif
    return "C:\\Temp";
  }
  
#if( defined(UNICODE) || defined(_UNICODE) )
  // TCHAR type is wchar_t
  const size_t utf8len = wcstombs( NULL, &buf[0], len );
  if( utf8len == 0 || static_cast<size_t>( int(-1) ) == utf8len )
  {
#if(PERFORM_DEVELOPER_CHECKS)
    log_developer_error( BOOST_CURRENT_FUNCTION, "Error converting temp path to UTF8 on Windows" );
#endif
    return "C:\\Temp";
  }
  vector<char> thepath( utf8len );
  wcstombs( &thepath[0], &buf[0], len );
  return string( thepath.begin(), thepath.end() );
#else
  // TCHAR type is char
  return string(buf.begin(), buf.end());
#endif
  
#else
  
  const char *val = NULL;
  (val = std::getenv("TMPDIR" )) ||
  (val = std::getenv("TMP"    )) ||
  (val = std::getenv("TEMP"   )) ||
  (val = std::getenv("TEMPDIR"));

  if( val && UtilityFunctions::is_directory(val) )
    return val;

  return "/tmp";
#endif
}//std::string temp_dir()

  
bool remove_file( const std::string &name )
{
#if( SpecUtils_NO_BOOST_LIB )
  return (0 == unlink(name.c_str()) );
#else
  try{ boost::filesystem::remove( name ); } catch(...){ return false; }
  return true;
#endif
}//bool remove_file( const std::string &name )

bool rename_file( const std::string &source, const std::string &destination )
{
  if( !is_file(source) || is_file(destination) || is_directory(destination) )
    return false;
#if( SpecUtils_NO_BOOST_LIB )
  //from Windows: BOOL MoveFile( LPCTSTR lpExistingFileName, LPCTSTR lpNewFileName );
  const int result = rename( source.c_str(), destination.c_str() );
  return (result==0);
#else
  boost::system::error_code ec;
  boost::filesystem::rename( source, destination, ec );
  return !ec;
#endif
}//bool rename_file( const std::string &from, const std::string &destination )
  
  
bool is_file( const std::string &name )
{
#if( SpecUtils_NO_BOOST_LIB )
//  struct stat fileInfo;
//  const int status = stat( name.c_str(), &fileInfo );
//  if( status != 0 )
//    return false;
//  return S_ISREG(fileinfo.st_mode);
  ifstream file( name.c_str() );
  return file.good();
#else
  bool isfile = false;
  try
  {
    isfile = (boost::filesystem::exists( name )
              && !boost::filesystem::is_directory( name ));
  }catch(...){}
  
  return isfile;
#endif
}//bool is_file( const std::string &name )
  

bool is_directory( const std::string &name )
{
#if( SpecUtils_NO_BOOST_LIB )
  struct stat statbuf;
  stat( name.c_str(), &statbuf);
  return S_ISDIR(statbuf.st_mode);
#else
  try{ return boost::filesystem::is_directory( name ); }catch(...){}
  return false;
#endif
}//bool is_directory( const std::string &name )

  
int create_directory( const std::string &name )
{
  if( is_directory(name) )
    return -1;
  
  int nError = 0;
#if ( defined(WIN32) || defined(UNDER_CE) || defined(_WIN32) || defined(WIN64) )
  nError = _mkdir(name.c_str()); // can be used on Windows
#else
  mode_t nMode = 0733; // UNIX style permissions
  nError = mkdir(name.c_str(),nMode); // can be used on non-Windows
#endif
  if (nError != 0) {
    // handle your error here
    return 0;
  }
  
  return 1;
}
  
bool can_rw_in_directory( const std::string &name )
{
  if( !is_directory(name) )
    return false;
    
#if ( defined(WIN32) || defined(UNDER_CE) || defined(_WIN32) || defined(WIN64) )
  const int can_access = _access( name.c_str(), 06 );
#else
  const int can_access = access( name.c_str(), R_OK | W_OK | X_OK );  
#endif
    
  return (can_access == 0);
}//bool can_rw_in_directory( const std::string &name )
  
  
std::string append_path( const std::string &base, const std::string &name )
{
#if( SpecUtils_NO_BOOST_LIB )
#if( defined(WIN32) || defined(UNDER_CE) || defined(_WIN32) || defined(WIN64) )
  if( base.size() && (base[base.size()-1]=='\\'||base[base.size()-1]=='/') )
    return base + name;
  if( name.size() && (name[0]=='\\'||name[0]=='/') )
    return base + name;
  return base + '\\' + name;
#else
  if( base.size() && base[base.size()-1]=='/' )
    return base + name;
  if( name.size() && name[0]=='/' )
    return base + name;
  return base + '/' + name;
#endif
#else
  boost::filesystem::path p(base);
  p /= name;
#if( BOOST_VERSION < 106501 )
  return p.make_preferred().string<string>();
#else
  return p.make_preferred().lexically_normal().string<string>();
#endif
#endif
}//std::string append_path( const std::string &base, const std::string &name )
  

std::string filename( const std::string &path_and_name )
{
#if( SpecUtils_NO_BOOST_LIB )
#ifdef _WIN32
  #error "UtilityFunctions::parent_path not not tested for SpecUtils_NO_BOOST_LIB on Win32!  Like not even tested once"
  char path_buffer[_MAX_PATH];
  char drive[_MAX_DRIVE];
  char dir[_MAX_DIR];
  char fname[_MAX_FNAME];
  char ext[_MAX_EXT];
  
  errno_t err = _splitpath_s( path.c_str(), drive, dir, fname,  ext );
  
  if( err != 0 )
    throw runtime_error( "Failed to _splitpath_s in filename()" );
  
  return string(fname) + ext;
#else  // _WIN32
  // "/usr/lib" -> "lib"
  // "/usr/"    -> "usr"
  // "usr"      -> "usr"
  // "/"        -> "/"
  // "."        -> "."
  // ".."       -> ".."
  #error "UtilityFunctions::filename not tested for SpecUtils_NO_BOOST_LIB *NIX - not even one time"
  vector<char> pathvec( path_and_name.size() + 1 );
  memcpy( &(pathvec[0]), path_and_name.c_str(), path_and_name.size() + 1 );
  
  //basename is supposedly thread safe, and also you arent supposed to free what
  //  it returns
  char *bname = basename( &(pathvec[0]) );
  if( !bname ) //shouldnt ever happen!
    throw runtime_error( "Error with basename in filename()" );
  
  return bname;
#endif
#else
  return boost::filesystem::path(path_and_name).filename().string<string>();
#endif
}//std::string filename( const std::string &path_and_name )
  
  
std::string parent_path( const std::string &path )
{
#if( SpecUtils_NO_BOOST_LIB )
#ifdef _WIN32
  #error "UtilityFunctions::parent_path not not tested for SpecUtils_NO_BOOST_LIB on Win32!  Like not even tested once"
  char path_buffer[_MAX_PATH];
  char drive[_MAX_DRIVE];
  char dir[_MAX_DIR];
  char fname[_MAX_FNAME];
  char ext[_MAX_EXT];
  
  errno_t err = _splitpath_s( path.c_str(), drive, dir, fname, ext );
  
  if( err != 0 )
    throw runtime_error( "Failed to get parent-path" );
  
  err = _makepath_s( path_buffer, _MAX_PATH, drive, dir, nullptr, nullptr );
  
  if( err != 0 )
    throw runtime_error( "Failed to makepathin parent_path" );
  
  return path_buffer;
#else
  //dirname from libgen.h
  //"/usr/lib" -> "/usr"
  //"/usr/" -> "/"
  //"usr" -> "."
  //"." -> "."
  vector<char> pathvec( path.size() + 1 );
  memcpy( &(pathvec[0]), path.c_str(), path.size() + 1 );
  
  //dirname is supposedly thread safe, and also you arent supposed to free what
  //  it returns
  char *parname = dirname( &(pathvec[0]) );
  if( !parname )  //shouldnt ever happen!
    throw runtime_error( "Error with dirname in parent_path()" );
  
  #error "UtilityFunctions::parent_path not not tested for SpecUtils_NO_BOOST_LIB"
  return parname;
#endif
#else
  return boost::filesystem::path(path).parent_path().string<string>();
#endif
}//std::string parent_path( const std::string &path )

  
std::string file_extension( const std::string &path )
{
  const string fn = filename( path );
  const size_t pos = fn.find_last_of( '.' );
  if( pos == string::npos )
    return "";
  return fn.substr(pos);
}
  
#if defined(WIN32) || defined(WIN64)
  // Copied from linux libc sys/stat.h:
  #define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
  #define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
  
size_t file_size( const std::string &path )
{
  struct stat st;
  if( stat(path.c_str(), &st) < 0 )
    return 0;
  
  if( S_ISDIR(st.st_mode) )
    return 0;
  
  return st.st_size;
}
  
  
#if( SpecUtils_NO_BOOST_LIB )
std::string temp_file_name( std::string base, std::string directory )
{
#error "temp_file_name for SpecUtils_NO_BOOST_LIB=1 not tested (but is in principle reasonably solid)"
  
  //For alternative implementations (this one is probably by no means
  //trustworthy) see http://msdn.microsoft.com/en-us/library/aa363875%28VS.85%29.aspx
  // for windows
  // Or just grab from unique_path.cpp in boost.
  
  if( directory.empty() || !is_directory(directory) )
    directory = UtilityFunctions::temp_dir();
 
  size_t numplaceholders = 0;
  for( const char c : base )
    numplaceholders += (c=='%');
  
  if( numplaceholders < 8 )
  {
    if( !base.empty() )
      base += "_";
    base += "%%%%-%%%%-%%%%-%%%%";
  }
  
  std::random_device randdev;  //This is the system (true) random number generator
  std::uniform_int_distribution<int> distribution(0,15);
  
  const char hex[] = "0123456789abcdef";
  static_assert( sizeof(hex) == 16, "" );
  
  for( size_t i = 0; i < base.size(); ++i )
  {
    if( base[i] != '%' )
      continue;
    const int randhexval = distribution(randdev);
    base[i] = hex[randhexval];
  }
  
  const string answer = append_path( directory, base );
  
  //We could test that this file doesnt exist, but the overwhelming probability
  //  says it doesnt, so we'll live large and skip the check.
  
  return append_path( directory, base );
}
#else
std::string temp_file_name( std::string bases, std::string temppaths )
{
  using boost::filesystem::path;
  using boost::filesystem::unique_path;

  boost::filesystem::path temppath = temppaths;
  if( temppath.empty() || !boost::filesystem::is_directory(temppath) )
    temppath = UtilityFunctions::temp_dir();
  
  size_t numplaceholders = 0;
  for( const char c : bases )
    numplaceholders += (c=='%');
  
  if( numplaceholders < 8 )
  {
    if( !bases.empty() )
      bases += "_";
    bases += "%%%%-%%%%-%%%%-%%%%";
  }
  
  
  temppath /= bases;

  return unique_path( temppath ).string<std::string>();
}//path temp_file_name( path base )


bool is_absolute_path( const std::string &path )
{
  cerr << "Warning, is_absolute_path() untested" << endl;
  
#if( SpecUtils_NO_BOOST_LIB )
#ifdef WIN32
  return !PathIsRelativeA( path.c_str() );
#else
  return (path.size() && (path[0]=='/'));
#endif
#else //SpecUtils_NO_BOOST_LIB
  return boost::filesystem::path(path).is_absolute();
#endif
}
  
std::string get_working_path()
{
  cerr << "Warning, get_working_path() untested" << endl;
#ifdef WIN32
  char *buffer = _getcwd(nullptr, 0);
  if( !buffer )
    return "";
  const std::string cwdtemp = buffer;
  free( buffer );
  return cwdtemp;
#else
  char buffer[PATH_MAX];
  return (getcwd(buffer, sizeof(buffer)) ? std::string(buffer) : std::string(""));
#endif
}//std::string get_working_path();
  
  
#if( SpecUtils_NO_BOOST_LIB || BOOST_VERSION >= 104500 )
bool make_canonical_path( std::string &path, const std::string &cwd )
{
cerr << "Warning, make_canonical_path() untested" << endl;
  
  //filesystem::canonical was added some time after boost 1.44, so if we want canonicla
  //(BOOST_VERSION < 104500)
  
#if( SpecUtils_NO_BOOST_LIB ) //I dont know when filesystem::canonical was added
  if( !is_absolute_path(path) )
  {
    if( cwd.empty() )
    {
      const std::string cwdtmp = get_working_path();
      if( cwdtmp.empty() )
        return false;
      path = append_path( cwdtmp, path );
    }else
    {
      path = append_path( cwd, path );
    }
  }//if( !is_absolute_path(path) )
  
#ifdef WIN32
  //char full[_MAX_PATH];
  //if( _fullpath( full, partialPath, _MAX_PATH ) != NULL )
  //{
  //}
  char buffer[MAX_PATH];
  if( PathCanonicalizeA( buffer, path.c_str() ) )
  {
    path = buffer;
    return true;
  }
  return false;
#else //WIN32
  vector<char> resolved_name(PATH_MAX + 1, '\0');
  char *linkpath = realpath( path.c_str(), &(resolved_name[0]) );
  if( !linkpath )
    return false;
  path = linkpath;
  return true;
#endif
#else //SpecUtils_NO_BOOST_LIB
  boost::system::error_code ec;
  const auto result = boost::filesystem::canonical(path, cwd, ec);
  if( !ec )
    path = result.string<string>();
  return !ec;
#endif
}//bool make_canonical_path( std::string &path )
#endif //#if( SpecUtils_NO_BOOST_LIB || BOOST_VERSION >= 104500 )
  
  
  /*
   Could replace recursive_ls_internal() with the following to help get rid of linking to boost
#ifdef _WIN32
   //https://stackoverflow.com/questions/2314542/listing-directory-contents-using-c-and-windows
   
  bool ListDirectoryContents(const char *sDir)
  {
    WIN32_FIND_DATA fdFile;
    HANDLE hFind = NULL;
    
    char sPath[2048];
    
    //Specify a file mask. *.* = We want everything!
    sprintf(sPath, "%s\\*.*", sDir);
    
    if((hFind = FindFirstFile(sPath, &fdFile)) == INVALID_HANDLE_VALUE)
    {
      printf("Path not found: [%s]\n", sDir);
      return false;
    }
    
    do
    {
      //Find first file will always return "."
      //    and ".." as the first two directories.
      if(strcmp(fdFile.cFileName, ".") != 0
         && strcmp(fdFile.cFileName, "..") != 0)
      {
        //Build up our file path using the passed in
        //  [sDir] and the file/foldername we just found:
        sprintf(sPath, "%s\\%s", sDir, fdFile.cFileName);
        
        //Is the entity a File or Folder?
        if(fdFile.dwFileAttributes &FILE_ATTRIBUTE_DIRECTORY)
        {
          printf("Directory: %s\n", sPath);
          ListDirectoryContents(sPath); //Recursion, I love it!
        }
        else{
          printf("File: %s\n", sPath);
        }
      }
    }
    while(FindNextFile(hFind, &fdFile)); //Find the next file.
    
    FindClose(hFind); //Always, Always, clean things up!
    
    return true;
  }
#else
  //see recursive_ls_internal_unix(...)
#endif
*/
  

  
bool filter_ending( const std::string &path, void *user_match_data )
{
  const std::string *ending = (const std::string *)user_match_data;
  return iends_with(path, *ending);
}
  
#if( defined(_WIN32) || PERFORM_DEVELOPER_CHECKS )
vector<std::string> recursive_ls_internal_boost( const std::string &sourcedir,
                                            file_match_function_t match_fcn,
                                            void *user_match_data,
                                            const size_t depth,
                                            const size_t numfiles )
{
  using namespace boost::filesystem;
  
  vector<string> files;
  
  /*
   //A shorter untested implementation, that might be better.
   for( recursive_directory_iterator iter(sourcedir), end; iter != end; ++iter )
   {
   const std::string name = iter->path().filename().string();
   const bool isdir = UtilityFunctions::is_directory( name );
   
   if( !isdir && (!match_fcn || match_fcn(name,user_match_data)) )
   files.push_back( name );
   
   if( files.size() >= sm_ls_max_results )
   break;
   }
   return files;
   */
  
  
  if( depth >= sm_recursive_ls_max_depth )
    return files;
  
  if ( !UtilityFunctions::is_directory( sourcedir ) )
    return files;
  
  directory_iterator end_itr; // default construction yields past-the-end
  
  directory_iterator itr;
  try
  {
    itr = directory_iterator( sourcedir );
  }catch( std::exception & )
  {
    //ex: boost::filesystem::filesystem_error: boost::filesystem::directory_iterator::construct: Permission denied: "..."
    return files;
  }
  
  for( ; (itr != end_itr) && ((files.size()+numfiles) < sm_ls_max_results); ++itr )
  {
    const boost::filesystem::path &p = itr->path();
    const string pstr = p.string<string>();
    
    const bool isdir = UtilityFunctions::is_directory( pstr );
    
    if( isdir )
    {
		//I dont think windows supports symbolic links, so we dont have to worry about cyclical links, maybe
#if( !defined(WIN32) )
      if( boost::filesystem::is_symlink(pstr) )
      {
        //Make sure to avoid cyclical references to our parent directory
        try
        {
          auto resvedpath = boost::filesystem::read_symlink( p );
          if( resvedpath.is_relative() )
            resvedpath = p.parent_path() / resvedpath;
          resvedpath = boost::filesystem::canonical( resvedpath );
          auto pcanon = boost::filesystem::canonical( p.parent_path() );
          if( UtilityFunctions::starts_with( pcanon.string<string>(), resvedpath.string<string>().c_str() ) )
            continue;
        }catch(...)
        {
        }
      }//if( boost::filesystem::is_symlink(pstr) )
#endif

      const vector<string> r = recursive_ls_internal_boost( pstr, match_fcn, user_match_data, depth + 1, files.size() );
      files.insert( files.end(), r.begin(), r.end() );
    }else if( UtilityFunctions::is_file( pstr ) ) //if ( itr->leaf() == patern ) // see below
    {
      if( !match_fcn || match_fcn(pstr,user_match_data) )
        files.push_back( pstr );
    }//
  }//for( loop over
  
  return files;
}//recursive_ls(...)
#endif //#if( defined(_WIN32) || PERFORM_DEVELOPER_CHECKS )
  
#ifndef _WIN32
/** Returns -1 if there is some error, like couldnt access a file.
    Returns 0 if symlink is not to parent.
    Returns 1 if symlink is a parent directory
 */
int check_if_symlink_is_to_parent( const string &filename )
{
  //Need to make sure symbolic link doesnt point to somethign below the
  //  current directory to avoid goign in a circle
  struct stat sb;
  
  if( lstat(filename.c_str(), &sb) == -1 )
  {
#if(PERFORM_DEVELOPER_CHECKS)
    char buff[1024], errormsg[1024];
    strerror_r( errno, buff, sizeof(buff)-1 );
    snprintf( errormsg, sizeof(errormsg), "Warning: couldnt lstat '%s' error msg: %s", filename.c_str(), buff );
    log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
#endif
    
    return -1;
  }//if( lstat(filename.c_str(), &sb) == -1 )
  
  vector<char> linkname( sb.st_size + 1 );
  const ssize_t r = readlink( filename.c_str(), &(linkname[0]), sb.st_size + 1 );
  linkname[linkname.size()-1] = '\0';  //JIC
  if( r > sb.st_size )
  {
#if(PERFORM_DEVELOPER_CHECKS)
    char errormsg[1024];
    snprintf( errormsg, sizeof(errormsg), "Warning: For file '%s' the symlink contents changed during operations", filename.c_str() );
    log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
#endif
    return -1;
  }//if( r > sb.st_size )
  
  //Check if symlink is relative or absolute, and if relative
  string linkfull;
  if( linkname[0] == '.' )
    linkfull = UtilityFunctions::append_path( UtilityFunctions::parent_path(filename), &(linkname[0]) );
  else
    linkfull = &(linkname[0]);
      
  vector<char> resolved_link_name, resolved_parent_name;
  
  resolved_link_name.resize(PATH_MAX + 1);
  resolved_parent_name.resize(PATH_MAX + 1);
  
  char *linkpath = realpath( linkfull.c_str(), &(resolved_link_name[0]) );
  char *parentpath = realpath( UtilityFunctions::parent_path(filename).c_str(), &(resolved_parent_name[0]) );
      
  resolved_link_name[resolved_link_name.size()-1] = '\0';     //JIC
  resolved_parent_name[resolved_parent_name.size()-1] = '\0'; //JIC
  
  if( !linkpath || !parentpath )
  {
#if(PERFORM_DEVELOPER_CHECKS)
    char errormsg[1024];
    snprintf( errormsg, sizeof(errormsg), "Warning: Couldnt resolve real path for '%s' or %s ", linkfull.c_str(), filename.c_str() );
    log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
#endif
    return -1;
  }
  
  if( UtilityFunctions::starts_with(parentpath, linkpath) )
    return 1;

  return 0;
}//int check_if_symlink_is_to_parent( const string &filename )
  
  
vector<std::string> recursive_ls_internal_unix( const std::string &sourcedir,
                                            file_match_function_t match_fcn,
                                            void *user_match_data,
                                            const size_t depth,
                                            const size_t numfiles )
{
  vector<string> files;
  
  if( depth >= sm_recursive_ls_max_depth )
    return files;
  
  errno = 0;
  DIR *dir = opendir( sourcedir.c_str() );
  if( !dir )
  {
#if(PERFORM_DEVELOPER_CHECKS)
    char buff[1024], errormsg[1024];
    strerror_r( errno, buff, sizeof(buff)-1 );
    snprintf( errormsg, sizeof(errormsg), "Failed to open directory '%s' with error: %s", sourcedir.c_str(), buff );
    log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
#endif
    return files;
  }//if( couldnt open directory )
  
  errno = 0;
  struct dirent *dent = nullptr;
  
  while( (dent = readdir(dir)) && ((numfiles + files.size()) < sm_ls_max_results) )
  {
    if( !strcmp(dent->d_name, ".") || !strcmp(dent->d_name, "..") )
      continue;
    
    const string filename = UtilityFunctions::append_path( sourcedir, dent->d_name );
    
    //handling (dent->d_type == DT_UNKNOWN) is probably unecassary, but we'll
    //  do it anyway since the cost is cheap.
    //We dont want to bother calling is_diretory() or is_file() (or stat in
    //  general) since these operations are kinda expensive, so we will only
    //  do it if necassary.
    
    const bool follow_sym_links = true;
    bool is_dir = (dent->d_type == DT_DIR) || ((dent->d_type == DT_UNKNOWN) && UtilityFunctions::is_directory(filename));
    if( !is_dir && follow_sym_links && (dent->d_type == DT_LNK) && UtilityFunctions::is_directory(filename) )
    {
      is_dir = (0==check_if_symlink_is_to_parent(filename));
    }//if( a symbolic link that doesnt resolve to a file )
    
    const bool is_file = !is_dir
                         && ((dent->d_type == DT_REG)
                         || (follow_sym_links && (dent->d_type == DT_LNK) && UtilityFunctions::is_file(filename))  //is_file() checks for broken link
                         || ((dent->d_type == DT_UNKNOWN) && UtilityFunctions::is_file(filename)));
    
    if( is_dir )
    {
      //Note that we are leaving the directory open here - there is a limit on
      //  the number of directories we can have open (like a couple hundred)
      //  (we could refactor things to avoid this, but whatever for now)
      const vector<string> r = recursive_ls_internal_unix( filename, match_fcn, user_match_data, depth + 1, files.size() );
      files.insert( files.end(), r.begin(), r.end() );
    }else if( is_file )
    {
      if( !match_fcn || match_fcn(filename,user_match_data) )
        files.push_back( filename );
    }else
    {
      //broken symbolic links will end up here, I dont think much else
    }
  }//while( dent )
  
  closedir( dir ); //Should we bother checking/handling errors
  
  
#if(PERFORM_DEVELOPER_CHECKS)
  auto from_boost = recursive_ls_internal_boost( sourcedir, match_fcn, user_match_data, depth, numfiles );
  if( from_boost != files )  //It looks like things are always oredered the same
  {
    auto from_native = files;
    std::sort( begin(from_native), end(from_native) );
    std::sort( begin(from_boost), end(from_boost) );
    
    if( from_native != from_boost )
    {
      vector<bool> boost_has( from_native.size(), false );
      
      for( const auto &b : from_boost )
      {
        auto iter = lower_bound( std::begin(from_native),std::end(from_native), b );
        if( iter == std::end(from_native) || ((*iter) != b) )
          cout << "Native didnt find: '" << b << "'" << endl;
        else
          boost_has[iter - std::begin(from_native)] = true;
      }
      
      for( size_t i = 0; i < from_native.size(); ++i )
      {
        if( boost_has[i] )
          continue;
        auto pos = lower_bound( std::begin(from_boost), std::end(from_boost), from_native[i] );
        if( pos == std::end(from_boost) || ((*pos) != from_native[i]) )
          cout << "Boost didnt find: '" << from_native[i] << "'" << endl;
      }
      
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg), "Didnt get same files from UNIX vs Boost recursive search; nUnix=%i, nBoost=%i",
                static_cast<int>(from_native.size()), static_cast<int>(from_boost.size())  );
      log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
    }
  }
#endif //#if(PERFORM_DEVELOPER_CHECKS)
  
  return files;
}//recursive_ls_internal_unix(...)
#endif //#ifndef _WIN32
  

  
 
vector<std::string> recursive_ls( const std::string &sourcedir,
                                   const std::string &ending  )
{
#ifdef _WIN32
  if( ending.empty() )
    return recursive_ls_internal_boost( sourcedir, (file_match_function_t)0, 0, 0, 0 );
  return recursive_ls_internal_boost( sourcedir, &filter_ending, (void *)&ending, 0, 0 );
#else
  if( ending.empty() )
    return recursive_ls_internal_unix( sourcedir, (file_match_function_t)0, 0, 0, 0 );
  return recursive_ls_internal_unix( sourcedir, &filter_ending, (void *)&ending, 0, 0 );
#endif
}

  
std::vector<std::string> recursive_ls( const std::string &sourcedir,
                                        file_match_function_t match_fcn,
                                        void *match_data )
{
#ifndef _WIN32
  return recursive_ls_internal_unix( sourcedir, match_fcn, match_data, 0, 0 );
#else
  return recursive_ls_internal_boost( sourcedir, match_fcn, match_data, 0, 0 );
#endif
}
  

vector<string> ls_files_in_directory( const std::string &sourcedir, const std::string &ending )
{
  if( ending.empty() )
    return ls_files_in_directory( sourcedir, (file_match_function_t)0, 0 );
  return ls_files_in_directory( sourcedir, &filter_ending, (void *)&ending );
}
  
std::vector<std::string> ls_files_in_directory( const std::string &sourcedir,
                                                 file_match_function_t match_fcn,
                                                 void *user_data )
{
#ifndef _WIN32
  return recursive_ls_internal_unix( sourcedir, match_fcn, user_data, sm_recursive_ls_max_depth-1, 0 );
#else
  using namespace boost::filesystem;
  
  vector<string> files;
  if ( !UtilityFunctions::is_directory( sourcedir ) )
    return files;
  
  directory_iterator end_itr; // default construction yields past-the-end
  
  directory_iterator itr;
  try
  {
    itr = directory_iterator( sourcedir );
  }catch( std::exception & )
  {
    //ex: boost::filesystem::filesystem_error: boost::filesystem::directory_iterator::construct: Permission denied: "..."
    return files;
  }
  
  for( ; itr != end_itr; ++itr )
  {
    const boost::filesystem::path &p = itr->path();
    const string pstr = p.string<string>();
    const bool isfile = UtilityFunctions::is_file( pstr );
    
    if( isfile )
      if( !match_fcn || match_fcn(pstr,user_data) )
        files.push_back( pstr );
  }//for( loop over
  
  return files;
#endif
}//ls_files_in_directory(...)
  
  
  
#if( BOOST_VERSION < 106501 )
namespace
{
  namespace fs = boost::filesystem;
  
  //Get a relative path from 'from_path' to 'to_path'
  //  assert( make_relative( "/a/b/c/d", "/a/b/foo/bar" ) == "../../foo/bar" );
  // Return path when appended to from_path will resolve to same as to_path
  fs::path make_relative( fs::path from_path, fs::path to_path )
  {
    fs::path answer;
    
    //Make the paths absolute. Ex. turn 'some/path.txt' to '/current/working/directory/some/path.txt'
    from_path = fs::absolute( from_path );
    to_path = fs::absolute( to_path );
    
    fs::path::const_iterator from_iter = from_path.begin();
    fs::path::const_iterator to_iter = to_path.begin();
    
    //Loop through each each path until we have a component that doesnt match
    for( fs::path::const_iterator to_end = to_path.end(), from_end = from_path.end();
         from_iter != from_end && to_iter != to_end && *from_iter == *to_iter;
         ++from_iter, ++to_iter )
    {
    }
    
    //Add '..' to get from 'from_path' to our the base path we found
    for( ; from_iter != from_path.end(); ++from_iter )
    {
      if( (*from_iter) != "." )
        answer /= "..";
    }
    
    // Now navigate down the directory branch
    while( to_iter != to_path.end() )
    {
      answer /= *to_iter;
      ++to_iter;
    }
    
    return answer;
  }
}//namespace
#endif
  
std::string fs_relative( const std::string &from_path, const std::string &to_path )
{
#if( BOOST_VERSION < 106501 )
  return make_relative( from_path, to_path ).string<std::string>();
#else
  return boost::filesystem::relative( to_path, from_path ).string<std::string>();
#endif
}//std::string fs_relative( const std::string &target, const std::string &base )
  
#endif //if( !SpecUtils_NO_BOOST_LIB )
  
//  Windows
#ifdef _WIN32
double get_wall_time()
{
  LARGE_INTEGER time,freq;
  if( !QueryPerformanceFrequency(&freq) )
    return -std::numeric_limits<double>::max();

  if( !QueryPerformanceCounter(&time) )
    return -std::numeric_limits<double>::max();
  
  return static_cast<double>(time.QuadPart) / freq.QuadPart;
}//double get_wall_time()
#else //  Posix/Linux
double get_wall_time()
{
  //\todo Test std::chrono implementation and then get rid of Windows specialization
  //return std::chrono::time_point_cast<std::chrono::microseconds>( std::chrono::system_clock::now() ).time_since_epoch().count() / 1.0E6;
  struct timeval time;
  if( gettimeofday(&time,NULL) )
    return -std::numeric_limits<double>::max();
  return static_cast<double>(time.tv_sec) + (0.000001 * time.tv_usec);
}
#endif

double get_cpu_time()
{
  return static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
}
  

  
std::istream& safe_get_line(std::istream& is, std::string& t)
{
  return safe_get_line( is, t, 0 );
}
  
  
std::istream &safe_get_line( std::istream &is, std::string &t, const size_t maxlength )
{
  //from  http://stackoverflow.com/questions/6089231/getting-std-ifstream-to-handle-lf-cr-and-crlf
  //  adapted by wcjohns
  t.clear();

  // The characters in the stream are read one-by-one using a std::streambuf.
  // That is faster than reading them one-by-one using the std::istream.
  // Code that uses streambuf this way must be guarded by a sentry object.
  // The sentry object performs various tasks,
  // such as thread synchronization and updating the stream state.
  std::istream::sentry se( is, true );
  std::streambuf *sb = is.rdbuf();

  for( ; !maxlength || (t.length() < maxlength); )
  {
    int c = sb->sbumpc(); //advances pointer to current location by one
    switch( c )
    {
      case '\r':
        c = sb->sgetc();  //does not advance pointer to current location
        if(c == '\n')
          sb->sbumpc();   //advances pointer to one current location by one
         return is;
      case '\n':
        return is;
      case EOF:
        is.setstate( ios::eofbit );
        return is;
      default:
        t += (char)c;
    }//switch( c )
  }//for(;;)

  return is;
}//safe_get_line(...)

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
  
bool split_to_floats( const char *input, const size_t length,
                      vector<float> &results )
{
  results.clear();
  
  if( !input || !length )
    return true;
  
  results.reserve( length/2 );
  
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
#if(PERFORM_DEVELOPER_CHECKS)
  if( !ok )
  {
    if( *input && isdigit(*input) )
    {
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg), "Parsing failed: '%s'",
               string(begin,end).c_str() );
      log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
    }//if( *input && isdigit(*input) )
  }else
  {
    if( begin != end && results.size() )
    {
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg), "Trailing unpased string '%s'",
               string(begin,end).c_str() );
      log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
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
        if( fabs(a-b) > 0.000001*std::max(fabs(a),fabs(b)) )
        {
          char errormsg[1024];
          snprintf( errormsg, sizeof(errormsg),
                   "Out of tolerance diffence for floats %.9g using "
                   "boost::spirit vs %.9g using alternative split_to_float on float %i",
                    a, b, int(i) );
          log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
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
      log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
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
  
  
bool parse_float( const char *input, const size_t length, float &result )
{
  namespace qi = boost::spirit::qi;
  
  const char *begin = input;
  const char *end = begin + length;
  
  result = 0.0f;
  
  bool ok = qi::phrase_parse( begin, end, qi::float_, qi::space, result );
  
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
    log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
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
    contents.reserve( input_size / 2 );
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
      log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
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
    
    
#if(PERFORM_DEVELOPER_CHECKS)
    if( !ok )
    {
      if( input && isdigit(*input) )
      {
        char errormsg[1024];
        snprintf( errormsg, sizeof(errormsg), "Parsing failed: '%s'",
                  string(start_pos, end).c_str() );
        log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
      }
    }else
    {
      if( pos != end )
      {
        char errormsg[1024];
        snprintf( errormsg, sizeof(errormsg), "Trailing unpased string '%s'",
                 string(pos,end).c_str() );
        log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
      }//if( begin != end )
      
      const float a = static_cast<float>( value );
      const float b = (float) atof( string(start_pos, end).c_str() );
      if( fabs(a-b) > 0.000001f*std::max(fabs(a),fabs(b)) )
      {
        char errormsg[1024];
        snprintf( errormsg, sizeof(errormsg),
                 "Out of tolerance diffence for floats %.9g using "
                 "boost::spirit vs %.9g using alternative split_to_float on float %i",
                 a, b, int(contents.size()) );
        log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
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
#if ( defined(WIN32) || defined(UNDER_CE) || defined(_WIN32) || defined(WIN64) )
  char *value_str = strtok_s( input, delims, &pos_ptr );
#else
  char *value_str = strtok_r( input, delims, &pos_ptr );
#endif // #if ( defined(WIN32) || defined(UNDER_CE) || defined(_WIN32) || defined(WIN64) )
    
  while( value_str != NULL )
  {
    //  XXX: using stringstream to convert the double makes split_to_floats(...)
    //       take about 40% of the time of parsing an ICD1 file (the example Anthony
    //       NM Passthrough) - using atof reduces this to 17.9% (and further down to
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
#if(PERFORM_DEVELOPER_CHECKS)
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg),
                "Couldnt convert string '%s' to a float using atof(), error %i",
                value_str, errno );
      log_developer_error( BOOST_CURRENT_FUNCTION, errormsg );
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
#if ( defined(WIN32) || defined(UNDER_CE) || defined(_WIN32) || defined(WIN64) )
    value_str = strtok_s( NULL, delims, &pos_ptr );
#else
    value_str = strtok_r( NULL, delims, &pos_ptr );
#endif // #if ( defined(WIN32) || defined(UNDER_CE) || defined(_WIN32) || defined(WIN64) )
  }//while( pch != NULL )
  
#else
#error Invalid parse method specified
#endif
  
  return true;
}//vector<float> split_to_floats( ... )

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
  
  
unsigned int levenshtein_distance( const string &source, const string &target )
{
  //This function largely derived from code found at:
  //  http://www.merriampark.com/ldcpp.htm  (by Anders Johnasen).
  //  There was no accompaning lincense information, and since I found many
  //  similar-but-seperate implementations on the internet, I take the code in
  //  this function to be licensed under a 'do-what-you-will' public domain
  //  license. --Will Johnson 20100824
  //This function is case insensitive.
  const size_t n = source.length();
  const size_t m = target.length();
  if( !n )
    return static_cast<unsigned int>(m);
  
  if( !m )
    return static_cast<unsigned int>(n);
  
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

}//namespace UtilityFunctions
