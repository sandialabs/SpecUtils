#ifndef UtilityFunctions_h
#define UtilityFunctions_h
/* SpecUtils: a library to parse, save, and manipulate gamma spectrum data files.
 
 Copyright 2018 National Technology & Engineering Solutions of Sandia, LLC
 (NTESS). Under the terms of Contract DE-NA0003525 with NTESS, the U.S.
 Government retains certain rights in this software.
 For questions contact William Johnson via email at wcjohns@sandia.gov, or
 alternative emails of interspec@sandia.gov.
 
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

#include <set>
#include <string>
#include <vector>
#include <istream>
#include <exception>

#if( !SpecUtils_NO_BOOST_LIB )
#include <boost/version.hpp>
#endif

namespace boost
{
  namespace posix_time
  {
    class ptime;
  }
}

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#ifdef SRC_LOCATION
#undef SRC_LOCATION
#endif

#if( defined(_MSC_VER) && _MSC_VER <= 1700 )
#define SRC_LOCATION (std::string("File " TOSTRING(__FILE__) ": Function '") \
                     +  std::string(__FUNCTION__) \
                     + std::string( "': Line " TOSTRING(__LINE__)))
#else
#define SRC_LOCATION (std::string("File " TOSTRING(__FILE__) ": Function '") \
                     +  std::string(__func__) \
                     + std::string( "': Line " TOSTRING(__LINE__)))
#endif

//The below YEAR MONTH DAY macros are taken from
//http://bytes.com/topic/c/answers/215378-convert-__date__-unsigned-int
//  and I believe to be public domain code
#define YEAR ((((__DATE__ [7] - '0') * 10 + (__DATE__ [8] - '0')) * 10 \
         + (__DATE__ [9] - '0')) * 10 + (__DATE__ [10] - '0'))
#define MONTH (__DATE__ [2] == 'n' && __DATE__ [1] == 'a' ? 0 \
         : __DATE__ [2] == 'b' ? 1 \
         : __DATE__ [2] == 'r' ? (__DATE__ [0] == 'M' ? 2 : 3) \
         : __DATE__ [2] == 'y' ? 4 \
         : __DATE__ [2] == 'n' ? 5 \
         : __DATE__ [2] == 'l' ? 6 \
         : __DATE__ [2] == 'g' ? 7 \
         : __DATE__ [2] == 'p' ? 8 \
         : __DATE__ [2] == 't' ? 9 \
         : __DATE__ [2] == 'v' ? 10 : 11)
#define DAY ((__DATE__ [4] == ' ' ? 0 : __DATE__ [4] - '0') * 10 \
        + (__DATE__ [5] - '0'))

/** \brief Macro to embed compile date (as an decimal int) in ISO format into 
  the program
 
 example: Will evaluate to 20120122 for jan 22nd, 2012.
 */
#define COMPILE_DATE_AS_INT (YEAR*10000 + (MONTH+1)*100 + DAY)

#if( defined(WIN32) )
#undef min
#undef max
#endif 

namespace UtilityFunctions
{
  //Below string functions are implmented here to reduce binary size over using
  //  the equivalent boost::algorithm functions everywhere; also, I am trying
  //  to (slowly) remove all boost dependancies from SpectrumDataStructs.h so
  //  it will be easier on (Windows) folks to use this code in their projects.
  
  /** \brief Removes leading and trailing whitespaces (" \f\n\r\t\v"). */
  void trim( std::string &str );
  
  /** \brief Removes leading and trailing whitespaces (" \f\n\r\t\v"). */
  std::string trim_copy( std::string str );
  
  /** \brief Converts each ascii letter to lower case, not UTF8 safe/aware. */
  void to_lower( std::string &input );
  
  /** \brief Converts each ascii letter to lower case, not UTF8 safe/aware. */
  std::string to_lower_copy( std::string input );
  
  /** \brief Converts each ascii letter to upper case, not UTF8 safe/aware. */
  void to_upper( std::string &input );
  
  /** \brief Case independant string comparison. Not UTF8 or locale aware. */
  bool iequals( const char *str, const char *test );
  
  /** \brief Case independant string comparison. Not UTF8 or locale aware. */
  bool iequals( const std::string &str, const char *test );
  
  /** \brief Case independant string comparison. Not UTF8 or locale aware. */
  bool iequals( const std::string &str, const std::string &test );
  
  /** \brief Returns if the substring is contained within the input string. */
  bool contains( const std::string &input, const char *substr );
  
  /** \brief Returns if the substring is contained within the input string, 
     independant of case; not UTF8 or locale aware. 
   */
  bool icontains( const std::string &input, const char *substr );
  
  /** \brief Returns if the substring is contained within the input string,
   independant of case; not UTF8 or locale aware.
   */
  bool icontains( const std::string &input, const std::string &substr );
  
  /** \brief Returns if the substring is contained within the input string,
   independant of case; not UTF8 or locale aware.
   */
  bool icontains( const char *input, const size_t input_len,
                  const char *substr, const size_t substr_len );
  
  /** \brief Returns if the input starts with the specified substr. */
  bool starts_with( const std::string &input, const char *substr );
  
  /** \brief Returns if the input starts with the specified substr, case 
    independant; is not UTF8 or locale aware.
   */
  bool istarts_with( const std::string &line, const char *label );
  
  /** \brief Returns if the input starts with the specified substr, case
   independant; is not UTF8 or locale aware.
   */
  bool istarts_with( const std::string &line, const std::string &label );
  
  /** \brief Returns if the input ends with the specified substr, case
   independant; is not UTF8 or locale aware.
   */
  bool iends_with( const std::string &line, const std::string &label );

  /** \brief Removes any character in chars_to_remove from line; is not UTF8 or
  locale aware.
   */
  void erase_any_character( std::string &line, const char *chars_to_remove );
  
  /** \brief Splits an input string according to specified delimiters.  
   
   Leading and trailing delimiters are ignored, and mutliple delimiters in a
   row are treated as eqivalent to a single delimiter.
   Note that this function is not equivalent to boost::split.
   
   \param results Where results of splitting are placed.  Will be cleared of 
          any previous contents first.
   \param input input string to split.
   \param delims Null terminated list of delimiters to split at; note that the 
          input string will be split when ever any of the characters are
          encountered. '\0' cannot be specified as a delimiter. An empty string
          will return 0 results if #input is empty, and one result otherwise.
   */
  void split( std::vector<std::string> &results,
              const std::string &input, const char *delims );
  
  /** \brief Splits an input string according to specified delimiters.
   
   Similar to #split, but each delimiters ends the field, even if the field is
   empty (i.e., no delimiter compression)
   
   \param results Where results of splitting are placed.  Will be cleared of
   any previous contents first.
   \param input input string to split.
   \param delims Null terminated list of delimiters to split at; note that the
   input string will be split when ever any of the characters are
   encountered. '\0' cannot be specified as a delimiter, and an empty string
   will return 0 results if input is empty, and one result otherwise.
   */
  void split_no_delim_compress( std::vector<std::string> &results,
             const std::string &input, const char *delims );
  
  /** \brief Replaces all (case insensitive) instances of <i>pattern</i> with
     <i>replacement</i> in <i>input</i>.  Not UTF8 or locale aware.
   */
  void ireplace_all( std::string &input,
                     const char *pattern, const char *replacement );
  
  /** \brief  Replaces all (case insensitive) instances of <i>pattern</i> with
      <i>replacement</i> in <i>input</i>, returning a copy.  Much more efficient
      for longer strings and/or many matches.
      Not UTF8 or locale aware.
      Not well tested - so commented out.
   */
//  std::string ireplace_all_copy( const std::string &input,
  //                  const char *pattern, const char *replacement );
  
  //XXX The following UTF8 functions have not been tested at all
  //
  // TODO: consider using code at http://bjoern.hoehrmann.de/utf-8/decoder/dfa/
  //       be the UTF-8 "workhorse"
  //
  /** \brief Counts the number of UTF8 encoded characters of the string,
    not the number of bytes the string length is.
   
   \param str input UTF8 encoded string.
   \param str_size_bytes Specifies how many bytes the string is 
     (ex: str+str_size_bytes will typically point to a '\0' character, but 
      doesnt have to).  If <em>str_size_bytes</em> is 0, then
     'str' must must be null-terminated, and length will be determined from
     that.
  */
  size_t utf8_str_len( const char * const str, size_t str_size_bytes );
  
  /** \brief Reduces string size to the specified number of bytes, or the
    nearest valid smaller size, where the last character is a valid UTF8 
    character.
   */
  void utf8_limit_str_size( std::string &str, const size_t max_bytes );
  
  /** Gives the location to place a null terminating character that is less
    than or equal to the specified lenght, while making sure the last byte is a
    valid UTF8 character.
   
   \param str The UTF8 encoded input string.
   \param strlen The actual length of the input string, in bytes.  If zero
     <em>str</em> must be null terminated.
   \param max_bytes The maximum desired length, in bytes, of the string.
   \returns The location, in bytes, to place the new terminating '\0' character.
   */
  size_t utf8_str_size_limit( const char *str,
                              size_t strlen, const size_t max_bytes );
  
  /* Convert between UTF-16 and UTF-8 strings.  This is used on Windows as
     all the file path functions encode everything as UTF-8, however on Windows
     you must call the "wide" version of functions related to file paths to work
     correctly (the narrow versions use the current code range, not UTF-8).
   
     (all of this wide/narrow stuff is a work on progress 20190717)
   */
  std::string convert_from_utf16_to_utf8(const std::wstring &wstr);
  std::wstring convert_from_utf8_to_utf16( const std::string &str );
  
  //to_iso_string(...) and to_extended_iso_string(...) are implemented here
  //  to avoid having to link to the boost datetime library
  
  /** Converts the input time to a iso formated string.
      Ex. "20140414T141201.621543"
   */
  std::string to_iso_string( const boost::posix_time::ptime &t );
  
  /** Converts the input time to a extended iso formated string.
      Ex. "2014-04-14T14:12:01.621543"
   */
  std::string to_extended_iso_string( const boost::posix_time::ptime &t );

  /** Converts the input to string in format d-mmm-YYYY HH:MM:SS AM,
      where mmm is 3 char month name; d is day number with no leading zeros.
      Returns "not-a-date-time" if input is not valid.
      Ex. 24hr format: "2014-Sep-9 13:02:15", AMP/PM: "2014-Sep-9 03:02:15 PM"
   */
  std::string to_common_string( const boost::posix_time::ptime &t, const bool twenty_four_hour );
  
  /** Converts input to the 23 character VAX format "DD-MMM-YYYY HH:MM:SS.SS".
      Returns empty string if input is not valid.
      Ex. "2014-Sep-19 14:12:01.62"
   */
  std::string to_vax_string( const boost::posix_time::ptime &t );
  
  /* Using Howard Hinnant's date library https://github.com/HowardHinnant/date
     is nearly a drop-in, header only solution for cross platform date parsing
     that appears to work well (all date/time unit tests pass on macOS at
     least).  If we use, we could probably totally get rid a ton of the
     "pre-proccessing" string wrangling complexity too!  ...next release...,
     when we can also switch to using std::chrono::time_point instead of
     boost::posix_time::ptime
   */
#define USE_HH_DATE_LIB 0
  
  //time_from_string(...):  Currently is a convience function for
  //  time_from_string_strptime(str,MiddleEndianFirst)
  boost::posix_time::ptime time_from_string( const char *str );
  
  
  /** \brief Describes how to attempt to parse date/times when it is ambigous,
    and you might have some prior information based on the source.
   */
  enum DateParseEndianType
  {
    /** Parse date time trying middle endian (month first) before trying little 
     endian, for ambiguous formats.  */
    MiddleEndianFirst,
    
    /** Parse date time trying little endian (day first) before trying middle 
      endian, for ambiguous formats. */
    LittleEndianFirst,
    
    /** Only try middle endian parsing on ambiguous formats. */
    MiddleEndianOnly,
    
    /* Only try little endian parsing on ambiguous formats. */
    LittleEndianOnly
  };//enum DateParseEndianType
  
  /** Converts the input string to a ptime.
   
   Modifies 'time_string' to be in a compatible format with strptime, and then 
   tries a number of common date formats to parse the date.
   Does not throw.
   
   Not tested on iOS, Android, Linux (I think just did on macOS and Windows).
   
   \param time_string Input string
   \param endian How to parse abigous dates.
   \returns If successful returns a valid ptime, if datetime couldnt be parsed
     returns invalid datetime.
   */
  boost::posix_time::ptime time_from_string_strptime( std::string time_string,
                                                     const DateParseEndianType endian );
  
  
  //The below uses home-spun methods when SpecUtils_NO_BOOST_LIB is
  //  true (not well tested as of 20140404, may have issues with symbolic links),
  //  and calls the equivalent boost funcitons otherwise.
  /** \brief Removes file from the filesystem, returning true if succesful. */
  bool remove_file( const std::string &name );
  
  /** \brief Returns if the specified name cooresponds to a file that can be 
    read. */
  bool is_file( const std::string &name );
  
  /** Renames a file from source to destination, returning if operation was
    sucessfull.
   
   Will fail if source is not a file.
   Will fail if destination already exists.
   Will fail is destination is a directory.
   */
  bool rename_file( const std::string &source, const std::string &destination );
  
  /** \brief Returns if the specified name is a directory that can be accessed 
    on the filesystem. */
  bool is_directory( const std::string &name );
  
  /** Creates specified directory.
      \returns
        - 0 if error making directory
        - -1 if directory existed
        - 1 if directory successfully made.
   */
  int create_directory( const std::string &name );
  
  /** Checks that path passed in is a directory, and the current process can
      list directory contents, as well as change them.
      On Unix cooresponds to +rwx (note, while on Windows it checks you
      can access the directory and read/write it).
   */
  bool can_rw_in_directory( const std::string &name );
  
  //Need file_extension(), current_path(), is_regular_file(),
  //  boost::filesystem::absolute, boost::filesystem::equivalent,
  //  boost::filesystem::path::make_preferred()
  //in order to ditch boost::filesystem throughout InterSpec
  
  /** \brief Concatinates parts of a filesystem name according to the operating 
    system.
   
   ex. append_path("path/to","file.txt") return "path/to/file.txt" on UNIX
   or "path/to\file.txt" on Windows. 
   May, or may not convert seperators in the rest of the path to the OS 
   preffered (depending if SpecUtils_NO_BOOST_LIB is defined).
   */
  std::string append_path( const std::string &base, const std::string &name );
  
  /** \brief Returns just the filename of a path passed in

   ex. "/path/to/some/file.txt" --> "file.txt"
       "/path/to/some"          --> "some" );
       "/path/to/some/"         --> "." );
   */
  std::string filename( const std::string &path_and_name );
  
  /** \brief Returns the parent path of the passed in path
   
   ex. "/path/to/some/file.txt" --> "/path/to/some"
       "/path/to/some/path"     --> "/path/to/some"
       "/path/to/some/path/"    -->  "/path/to/some" );
   
       Note that currently:
         "/path/to/some/path/.."  will return  "/path/to/some/path"
       (but may change in the future)
   
   This function operated in the passed in path string, not the absolute
   path on the filesystem.
   */
  std::string parent_path( const std::string &path );

  /** \brief Returns the extension of a filename, if there is one.
   
   Returns last '.' and any trailing characters from input.
   
   ex. "/path/to/some/file.txt" --> ".txt"
       "/path/to/filename"      --> ""
       ".profile"               --> ".profile"
   */
  std::string file_extension( const std::string &path );

  /** \brief Gives the size of the file in bytes.  Returns 0 if not a file.
   */
  size_t file_size( const std::string &path );
  
  /** \brief Returns temporary directory as designated by the operating system
   (or /tmp on unix if not specified).
   
   Note that if you are deployed in as a FCGI app, the system environment
   wont specify the temporary directory, in that case you should consult the
   CGI values (this function will just return /tmp in that case).
   */
  std::string temp_dir();
  
  /** Determines is the path passed in is absolute or not.
   */
  bool is_absolute_path( const std::string &path );
  
  /** Get the current working directory.
   
   Becareful if using multiple threads, and another thread could change the
   current working directory;
   
   Returns empty string on error.
   */
  std::string get_working_path();

  /** \brief Gives a unique file name.
   
   \param filebasename If not empty, then the returned file name will
    have '_%%%%-%%%%-%%%%-%%%%' appended to it (unless the string already
    contains at least 8 '%' characters), where the % characters will be randomly
    generated.
    
   
    \param directory Specifies the location where the temporary file should be
     located; if blank, or not a valid directory, the directory
     returned by #UtilityFunctions::temp_dir will be used.
*/
#if( SpecUtils_NO_BOOST_LIB )
  //temp_file_name: not implemetned well for SpecUtils_NO_BOOST_LIB==1
  std::string temp_file_name( std::string filebasename, std::string directory );
#else
  std::string temp_file_name( std::string filebasename, std::string directory );
  
  //Boost 1.44 doesnt support boost::filesystem::canonical, so we'll leave this
  //  function out because nothing I compile with the ancient Boosts use it; see
  //  source code for workaround if need-be
#if( SpecUtils_NO_BOOST_LIB || BOOST_VERSION >= 104500 )
  /** Converts path to a canonical absolute path (no dot, dot-dot or symbolic
      links). If the path is not an absolute path, it is first made absolute.
      The path must exist.
      If the current working directory is not specified, then the directory
      returned by cwd() will be used, if the path is not already absolute.
   
      Returns true if sucessful, and false if it fails for any reason.
   */
  bool make_canonical_path( std::string &path, const std::string &cwd = "" );
#endif
  
  /** Limit of how far down any of the recursive 'ls' functions can recurse down.
      I.e., how many directories deep can you go.
   */
   static const size_t sm_recursive_ls_max_depth = 25;
  
  /** The appriximate maximum number of file names any of the 'ls' functions can
      return.
   */
  static const size_t sm_ls_max_results = 100000;
  
  /** \brief Signature for a function to help filter if a file
   and a pointer to user data.
   */
  typedef bool(*file_match_function_t)( const std::string &filename, void *userdata );
  
  
  /** \brief Recursively searches through specified source directory and returns
    full path to each file found.  Directories are not returned (only files).
    Symbolic links are followed if they wont result in infinite cycles (e.g.
    symlinks pointing to one of their parent directories is skipped).
   
    Searches a maximum depth of #sm_recursive_ls_max_depth directories, and will
     return a maximum of about #sm_ls_max_results files.  If these limits are
     reached no indications are given.
   
   \param sourcedir Directory to recursively search through.  If not a directory
          results will be returned empty.
   \param ending If not empty, only files ending with the specified string will
    be returned; ending is not case sensistive.
   */
  std::vector<std::string> recursive_ls( const std::string &sourcedir,
                                         const std::string &ending = "" );
  
  
  /** \brief Recursively searches through specified source directory and returns
       full path to each file found, that also satisfies the 
       file_match_function_t function.  
       Directories are not returned (only files).
   
       Searches max depth of #sm_recursive_ls_max_depth directories; returns a
       maximum of #sm_ls_max_results files.
   
       \param sourcedir Directory to recursively search through.
       \param match_fcn function supplied by caller, that should return true
              if the file should be included in the results, false otherwise.  
              If not supplied, all files are matched.  This function does not
              help filter what directories are searched.
       \param user_data argument passed to match_fcn to help it make the 
              decision.  May be null if match_fcn allows for it to be null.
   */
  std::vector<std::string> recursive_ls( const std::string &sourcedir,
                                         file_match_function_t match_fcn,
                                         void *user_data );
  
  
  /** \brief Lists files in the specified source directory, returning the full
      path to each file found.  Directories are not returned (only files).
   
      Returns a maximum of #sm_ls_max_results files.
   
   \param sourcedir Directory to list through.
   \param ending If not empty, only files ending with the specified string will
   be returned; ending is not case sensistive.
   */
  std::vector<std::string> ls_files_in_directory( const std::string &sourcedir,
                                        const std::string &ending = "" );
  
  /** \brief Lists files only (not directories) in the specified source directory
       that match match_fcn criteria, returning the full path to each file 
       found.
   
       Returns a maximum of #sm_ls_max_results files.
    
      \param sourcedir Directory to list through.
      \param match_fcn Function that determines if a result should be included.
      \param match_data Data that will be passed to match_fcn to help decide if
             a file should be included in the results; this is optional to use
             according to requirments of your match_fcn.
   */
  std::vector<std::string> ls_files_in_directory( const std::string &sourcedir,
                                                  file_match_function_t match_fcn,
                                                  void *match_data );
  
  /** \brief List the directories inside a specific directory.  Not recursive.
   
       \param src The base directory to look in.
   
       \returns directories in src directory.  the "." and ".." directories are
                not included in results.  If src is not a directory, an empty
                answer is returned.  Returned answers do not include 'src' in
                them (i.e. if src contains directors "a", "b", "c", you will
                get back {"a", "b", "c"}.
   
      As of 20190625 only briefly checked that this function sorta-kinda seemed okay.
   */
  std::vector<std::string> ls_directories_in_directory( const std::string &src );
  
  
  /** Get a relative path from 'from_path' to 'to_path'
      Note: resolves files, so may throw exception
   
      assert( make_relative( "/a/b/c/d", "/a/b/foo/bar" ) == "../../foo/bar" );
      assert( make_relative( "a", "a/b/c") == "b/c");
      assert( make_relative( "a/b/c/x/y", "a/b/c") == "../..");
   */
  std::string fs_relative( const std::string &from_path, const std::string &to_path );
  
  //ToDo: add in path comparisons (which dont resolve files, just use strings)
  //std::string lexically_normalize_path( std::string &input );
  //std::string fs_lexically_relative( const std::string &source, const std::string &target );
  //assert( fs_lexically_normal("foo/./bar/..") == "foo/");
  //assert( fs_lexically_normal("foo/.///bar/../") == "foo/");
  //assert( fs_lexically_relative("/a/b/c","/a/d") == "../../d");
  //assert( fs_lexically_relative("/a/d","/a/b/c") == "../b/c");
  //assert( fs_lexically_relative("a","a/b/c") == "b/c");
  //assert( fs_lexically_relative("a/b/c/x/y","a/b/c") == "../..");
  //assert( fs_lexically_relative("a/b/c","a/b/c") == ".");
  //assert( fs_lexically_relative("c/d","a/b") == "../../a/b");
  
#endif //#if( !SpecUtils_NO_BOOST_LIB )


  /** \brief Gives the CPU time in seconds.
   
   Useful for timing things when you dont want to use boost.
   Does not count CPU time of sub-proccesses.
   
   \returns The CPU time in seconds, or on error -DBL_MAX.
   */
  double get_cpu_time();
  
  
  /** \brief Gives the current wall time in seconds.
   
    Useful for timing things when you dont want to use boost.
      
    \returns The wall time in seconds, or on error -DBL_MAX.

    Note May have an occational jump of a few seconds on Windows due to a
    hardware issue (fixed on newer windows/hardware?)
  */
  double get_wall_time();
  
  /** \brief Gets a line from the input stream that may be terminated with
    either UNIX or Windows EOL characters.
   
    See code for code source.
    Note that this function is probably very slow, and could be upgraded.
   */
  std::istream &safe_get_line( std::istream &is, std::string &t );

  /** Same as other variant of #safe_get_line, except allows specifying the
      maximum number of bytes to read; specifying zero means no limit.
   */
  std::istream &safe_get_line( std::istream &is, std::string &t, const size_t maxlength );
  
  /** \brief parses a string of ascii characters to their floating point value.
      
      The ascii float may have preceding whitespaces, and any text afterwards; 
      both of which are ignored.
   
      \param input Pointer to start of ascii string.  May be null only if length 
             is zero.
      \param length Number of bytes long the string to be parsed is.  A length
             of zero will always result in failed parsing.
      \param result The result of parsing.  If parsing failed, will be 0.0f.
      \returns True if was able to parse a number, false otherwise.
   */
  bool parse_float( const char *input, const size_t length, float &result );
  
  
  /** \brief Parses a string of ascii floats seperated by user specified 
    delimters into a std::vector<float>.  
   
    If there is more than one delimiter between floats,
    the extra delimiters are ignored (e.g. same result as if only a single 
    delimiter).
   
   \param input Null terminated input text string.
   \param contents Where the resulting floats are placed
   \param delims The possible delimiters that can seperate floats, specified as
     a null terminated c-string.  Note that any one character in this string
     acts as a delimieter. The delimters specified should not be '\0', numbers,
     '+', '-', '.', 'e', or 'E'.
   \param cambio_zero_compress_fix In Cambio N42 files zeros written like "0"
     indicates zeroes compression, a zero written like "0.000", "-0.0", etc are
     all a single bin with content zero - so for this case, if you specify 
     cambio_zero_compress_fix this function substitures a really small number 
     (FLT_MIN) in place of zero so zero-decompression wont decompress that 
     channel.
   \returns True if all characters in the string were either delimiters or were
     interpreted as part of a float, and the entire string was consumed (eg 
     there were no extra non-delimeter characters hanging on the end of the 
     string).  A false return value indicates parsing failed one of these 
     conditions.
   
   \code{.cpp}
    std::vector<float> contents;
    if( split_to_floats( "7.990,3,5 7 8", contents, ", ", false ) )
        cout << "Succesed in parsing" <<endl;
    assert( contents.size() == 5 );
    assert( contents[0] == 7.99f );
    assert( contents[4] == 8.0f );
    \endcode
  */
  bool split_to_floats( const char *input,
                       std::vector<float> &contents,
                       const char * const delims, // = " ,\r\n\t",
                       const bool cambio_zero_compress_fix );
 
  
  /** \brief Parses a string of ascii floats seperated by a fixed set of 
   delimters into a std::vector<float>.  
   
   This implementation is approximately 20% faster than
   the other variant of this function, and does not require a null terminated 
   input string, but the delimiters used are fixed, and this version does not 
   consider the cambio zero compress issue.
   
   \param input Input ascii string of floats seperated by spaces, tabs, returns, 
     newlines or commas.  Does not have to be a null terminated string.
   \param length Length of input string to be parsed.
   \returns True of the entire specified length of the input could be 
    interpreted as delimeter seperated floats.
   
   Note: the leading numbers before the decimal point must have a value less
    than 4294967296 (2^32) or else parsing will fail; values larger than this 
    will still parse fine as long as they are written in engineering notation, 
    such as 5.0E9.  The other implementation of this function is not effected
    by this potential issue (not this has not been seen to happen on channel
    counts of gamma data by wcjohns).
   */
  bool split_to_floats( const char *input, const size_t length,
                        std::vector<float> &results );
  
  /* \brief A convienience function. */
  bool split_to_floats( const std::string &input, std::vector<float> &results );
  
  
  /** \brief Parses a string of ascii integers into a std::vector<int>.  
   
   The ascii ints must be seperated by spaces, tabs, returns, newlines or 
   commas, and multiple delimiters in a row are treated as a single deliter.
   \param input Input text (does not have to be null terminated).
   \param length The lenght (number of bytes) of the input text to be parsed.
   \param results Where the results are placed.
   \returns True if input text only contained delimiters and numbers that could
     be interpreted as ints, and the entire string was consumed during parsing 
     (no trailing text besides delimiters).
  */
  bool split_to_ints( const char *input, const size_t length,
                       std::vector<int> &results );
  
  /** Same as split_to_ints, but for long longs.
   */
  bool split_to_long_longs( const char *input, const size_t length,
                            std::vector<long long> &results );
  
  
  /** \brief Turns a set of numbers with possible many sub-sequences with values
       that are adjacent into a convient human-readable string.
   
      For sequences such as {0,1,2,3,4,5,10,99,100,101,102,200}, will return a
      human readable string similar to "1-5,10,99-102,200"
   */
  std::string sequencesToBriefString( const std::set<int> &sequence );
  
  //see CompactFileManager::handleUserChangeSampleNum(...) for code that
  //  goes from a sequence string to a set of numbers... Not implemented here
  //  so we dont have to link to regex lib..
  
  
  /** \brief Gives the case-insensitive distance between two strings.
   */
  unsigned int levenshtein_distance( const std::string &source,
                                     const std::string &target );
  
}//namespace UtilityFunctions


#endif //#ifndef UtilityFunctions_h
