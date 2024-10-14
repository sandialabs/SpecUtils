#ifndef SpecUtils_StringAlgo_h
#define SpecUtils_StringAlgo_h
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



/** String-based functions used while parsing, creating, or dealing with
 spectrum files.
 
 Contains not just functions for altering or manipulating strings, but functions
 to split, combine, parse float/int/etc from string, CSV, dealing with UTF-8,
 etc.
 */
namespace  SpecUtils
{
  /** \brief Removes leading and trailing whitespaces (ex, " \f\n\r\t\v") according to std::isspace. */
  SpecUtils_DLLEXPORT
  void trim( std::string &str );
  
  /** \brief Removes leading and trailing whitespaces (" \f\n\r\t\v"). */
  SpecUtils_DLLEXPORT
  std::string trim_copy( std::string str );
  
  /** \brief Converts each ascii letter to lower case, not UTF8 safe/aware. */
  SpecUtils_DLLEXPORT
  void to_lower_ascii( std::string &input );
  
  /** \brief Converts each ascii letter to lower case, not UTF8 safe/aware. */
  SpecUtils_DLLEXPORT
  std::string to_lower_ascii_copy( std::string input );
  
  /** \brief Converts each ascii letter to upper case, not UTF8 safe/aware. */
  SpecUtils_DLLEXPORT
  void to_upper_ascii( std::string &input );
  
  /** \brief Case independent string comparison. Not UTF8 or locale aware. */
  SpecUtils_DLLEXPORT
  bool iequals_ascii( const char *str, const char *test );
  
  /** \brief Case independent string comparison. Not UTF8 or locale aware. */
  SpecUtils_DLLEXPORT
  bool iequals_ascii( const std::string &str, const char *test );
  
  /** \brief Case independent string comparison. Not UTF8 or locale aware. */
  SpecUtils_DLLEXPORT
  bool iequals_ascii( const std::string &str, const std::string &test );
  
  /** \brief Returns if the substring is contained within the input string. */
  SpecUtils_DLLEXPORT
  bool contains( const std::string &input, const char *substr );
  
  /** \brief Returns if the substring is contained within the input string,
   independent of case; not UTF8 or locale aware.
   
   if `substr` is empty, will return false, which may be different than behavior of
   `boost::algorithm::icontains(...)`.
   */
  SpecUtils_DLLEXPORT
  bool icontains( const std::string &input, const char *substr );
  
  /** \brief Returns if the substring is contained within the input string,
   independent of case; not UTF8 or locale aware.
   
   if `substr` is empty, will return false, which may be different than behavior of
   `boost::algorithm::icontains(...)`.
   */
  SpecUtils_DLLEXPORT
  bool icontains( const std::string &input, const std::string &substr );
  
  /** \brief Returns if the substring is contained within the input string,
   independent of case; not UTF8 or locale aware.
   
   if `substr` is empty, will return false, which may be different than behavior of
   `boost::algorithm::icontains(...)`.
   */
  SpecUtils_DLLEXPORT
  bool icontains( const char *input, const size_t input_len,
                 const char *substr, const size_t substr_len );
  
  /** \brief Returns if the input starts with the specified substr. */
  SpecUtils_DLLEXPORT
  bool starts_with( const std::string &input, const char *substr );
  
  /** \brief Returns if the input starts with the specified substr, case
   independent; is not UTF8 or locale aware.
   */
  SpecUtils_DLLEXPORT
  bool istarts_with( const std::string &line, const char *label );
  
  /** \brief Returns if the input starts with the specified substr, case
   independent; is not UTF8 or locale aware.
   */
  SpecUtils_DLLEXPORT
  bool istarts_with( const std::string &line, const std::string &label );
  
  /** \brief Returns if the input ends with the specified substr, case
   independent; is not UTF8 or locale aware.
   */
  SpecUtils_DLLEXPORT
  bool iends_with( const std::string &line, const std::string &label );
  
  /** \brief Case-insensitively finds the substring in the input.
   
   @returns The index of the first occurrence of `substr` in `input`.
            If `substr` is not in `input`, then returns `std::string::npos`.
   */
  SpecUtils_DLLEXPORT
  size_t ifind_substr_ascii( const std::string &input, const char * const substr );
  
  /** \brief Removes any character in chars_to_remove from line; is not UTF8 or
   locale aware.
   */
  SpecUtils_DLLEXPORT
  void erase_any_character( std::string &line, const char *chars_to_remove );
  
  /** \brief Splits an input string according to specified delimiters.
   
   Leading and trailing delimiters are ignored, and multiple delimiters in a
   row are treated as equivalent to a single delimiter.
   Note that this function is not equivalent to boost::split.
   
   \param results Where results of splitting are placed.  Will be cleared of
   any previous contents first.
   \param input input string to split.
   \param delims Null terminated list of delimiters to split at; note that the
   input string will be split when ever any of the characters are
   encountered. '\0' cannot be specified as a delimiter. An empty string
   will return 0 results if #input is empty, and one result otherwise.
   
   Example results:
     {",,,hello how are,,", ", "}  -> {"hello", "how", "are"}
     {",,,hello how are,,", ","}  ->  {"hello how are"}
   */
  SpecUtils_DLLEXPORT
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
  SpecUtils_DLLEXPORT
  void split_no_delim_compress( std::vector<std::string> &results,
                               const std::string &input, const char *delims );
  
  /** \brief Replaces all (case insensitive) instances of <i>pattern</i> with
   <i>replacement</i> in <i>input</i>.  Not UTF8 or locale aware.
   */
  SpecUtils_DLLEXPORT
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
              Note that a null character will not terminate counting; it will
              count until \p str_size_bytes.
              Invalid UTF-8 characters (ex, '\0', not properly ended, etc)
              will be counted as characters.
   \param str_size_bytes Specifies how many bytes the string is
   (ex: `str + str_size_bytes` will typically point to a '\0' character, but
   doesnt have to).
   */
  SpecUtils_DLLEXPORT
  size_t utf8_str_len( const char * const str, const size_t str_size_bytes );


  /** \brief Counts the number of UTF8 encoded characters of the string,
   not the number of bytes the string length is.
 
   \param str input UTF8 encoded string that MUST be null terminated.
   */
  SpecUtils_DLLEXPORT
  size_t utf8_str_len( const char * const str );
  
  /** \brief Reduces string size to the specified number of bytes, or the
   nearest valid smaller size, where the last character is a valid UTF8
   character.  Does not include the null terminating byte (e.g., will take
   max_bytes+1 bytes to represent in a c-string).
   */
  SpecUtils_DLLEXPORT
  void utf8_limit_str_size( std::string &str, const size_t max_bytes );
  
  /** Gives the index to place a null terminating character that is less
   than or equal to the specified lenght, while making sure the last byte is a
   valid UTF8 character.
   
   \param str The UTF8 encoded input string.
   \param num_in_bytes The actual length of the input string, in bytes,
   including the null-terminating byte.  If zero <em>str</em> must be null
   terminated.
   \param max_bytes The maximum desired length, in bytes, of the string,
   including the null terminating byte.  If zero or one, will return zero.
   \returns The location (index), in bytes, to place the new terminating '\0'
   character.
   */
  SpecUtils_DLLEXPORT
  size_t utf8_str_size_limit( const char * const str,
                             size_t num_in_bytes, const size_t max_bytes );
  
  
  
  
  /** \brief parses a string of ascii characters to their floating point value.
   
   The ascii float may have preceding whitespaces, and any text afterwards;
   both of which are ignored.
   
   If number is larger than can be represented, this function may either return true with
   result +inf (`SpecUtils_USE_FROM_CHARS`, `SpecUtils_USE_FAST_FLOAT`,
   and `SpecUtils_USE_BOOST_SPIRIT`, ), or it could return false
   (`SpecUtils_USE_STRTOD`).
   
   \param input Pointer to start of ascii string.  May be null only if length
   is zero.
   \param length Number of bytes long the string to be parsed is.  A length
   of zero will always result in failed parsing.
   \param result The result of parsing.  If parsing failed, will be 0.0f.
   \returns True if was able to parse a number, false otherwise.
   */
  SpecUtils_DLLEXPORT
  bool parse_float( const char *input, const size_t length, float &result );

  /** Same as #parse_float, but for doubles. */
  SpecUtils_DLLEXPORT
  bool parse_double( const char *input, const size_t length, double &result );
  

  /** \brief Parses a string of ascii characters to their integer representation.
   
   The ascii int may have preceding whitespaces, and any text afterwards;
   both of which are ignored.
   
   \param input Pointer to start of ascii string.  May be null only if length
   is zero.
   \param length Number of bytes long the string to be parsed is.  A length
   of zero will always result in failed parsing.
   \param result The result of parsing.  If parsing failed, will be 0.
   \returns True if was able to parse a number, false otherwise.
   */
  SpecUtils_DLLEXPORT
  bool parse_int( const char *input, const size_t length, int &result );
  
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
   cambio_zero_compress_fix this function substitutes a really small number
   (FLT_MIN) in place of zero so zero-decompression wont decompress that
   channel.
   \returns True if all characters in the string were either delimiters or were
   interpreted as part of a float, and the entire string was consumed (eg
   there were no extra non-delimiter characters hanging on the end of the
   string).  A false return value indicates parsing failed one of these
   conditions. Strings like "3,-0.1.7,9.3" will yield 3 values, and return false.
   
   \code{.cpp}
   std::vector<float> contents;
   if( split_to_floats( "7.990,3,5 7 8", contents, ", ", false ) )
   cout << "Succeeded in parsing" << endl;
   assert( contents.size() == 5 );
   assert( contents[0] == 7.99f );
   assert( contents[4] == 8.0f );
   \endcode
   
   \TODO: change this to be range based, instead of zero-terminated string input
   */
  SpecUtils_DLLEXPORT
  bool split_to_floats( const char *input,
                       std::vector<float> &contents,
                       const char * const delims, // = " ,\r\n\t",
                       const bool cambio_zero_compress_fix );
  
  
  /** \brief Parses a string of ascii floats separated by a fixed set of
   delimiters into a std::vector<float>.
   
   This implementation is approximately 20% faster than
   the other variant of this function, and does not require a null terminated
   input string, but the delimiters used are fixed, and this version does not
   consider the cambio zero compress issue.
   
   \param input Input ascii string of floats separated by spaces, tabs, returns,
   newlines or commas.  Does not have to be a null terminated string.
   \param length Length of input string to be parsed.
   \returns True of the entire specified length of the input could be
   interpreted as delimiter separated floats.
   
   Note: the leading numbers before the decimal point must have a value less
   than 4294967296 (2^32) or else parsing will fail; values larger than this
   will still parse fine as long as they are written in engineering notation,
   such as 5.0E9.  The other implementation of this function is not effected
   by this potential issue (not this has not been seen to happen on channel
   counts of gamma data by wcjohns).
   
   Note that currently (as of 20240307) that strings like "661.7,-0.1.7,9.3", will parse
   into 4 numbers {661.6,-0.1,0.7,9.3}, and this function will return true.
   
   \TODO: Investigate performance of using:
     - https://github.com/fastfloat/fast_float
     - https://github.com/abseil/abseil-cpp/blob/master/absl/strings/charconv.h
     - https://github.com/simdjson/simdjson/blob/master/src/generic/stage2/numberparsing.h
   */
  SpecUtils_DLLEXPORT
  bool split_to_floats( const char *input, const size_t length,
                       std::vector<float> &results );
  
  /* \brief A convenience function. */
  SpecUtils_DLLEXPORT
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
  SpecUtils_DLLEXPORT
  bool split_to_ints( const char *input, const size_t length,
                     std::vector<int> &results );
  
  /** Same as split_to_ints, but for long longs.
   */
  SpecUtils_DLLEXPORT
  bool split_to_long_longs( const char *input, const size_t length,
                           std::vector<long long> &results );
  
  
  /** Convert between UTF-16 and UTF-8 strings.  This is used on Windows as
    all the file path functions encode everything as UTF-8, however on Windows
    you must call the "wide" version of functions related to file paths to work
    correctly (the narrow versions use the current code range, not UTF-8).
   
   If input is improperly encoded, or other error, will return empty string.
    */
  SpecUtils_DLLEXPORT
  std::string convert_from_utf16_to_utf8( const std::wstring &wstr );
 
  /** Converts from UTF-8 to UTF-16 strings; primarily useful on Windows.
  
  All filesystem functions in this library take in, and give out UTF-8 paths,
  so if you want to open a file path given by one of the functions in this
  library on Windows, you should first convert it to UTF-16 before calling into
  the C or C++ standard library functions, or Windows provided functions.
   
  If input is improperly encoded, or other error, will return empty string.
  */
  SpecUtils_DLLEXPORT
  std::wstring convert_from_utf8_to_utf16( const std::string &str );
 

  /** \brief Prints the floating point value into its most compact form, for the specified
   number of significant figures.
   
   Prints the most compact string representation of the value, with at least the specified
   number of significant figures; if more significant figures can be included without increasing
   result length, they will be included.
   
   Uses the "round to nearest and ties to even" convention.
   
   Note: this function is hand-rolled, and extremely slow, and likely missing some edge cases or
         something.  There is likely a much more elegant and correct implementation for this.
   */
  SpecUtils_DLLEXPORT
  std::string printCompact( const double value, const size_t sig_figs );
  
  
  /** \brief Turns a set of numbers with possible many sub-sequences with values
   that are adjacent into a convenient human-readable string.
   
   For sequences such as {0,1,2,3,4,5,10,99,100,101,102,200}, will return a
   human readable string similar to "1-5,10,99-102,200"
   */
  SpecUtils_DLLEXPORT
  std::string sequencesToBriefString( const std::set<int> &sequence );
  
  //see CompactFileManager::handleUserChangeSampleNum(...) for code that
  //  goes from a sequence string to a set of numbers... Not implemented here
  //  so we dont have to link to regex lib..
  
  
  /** \brief Gives the case-insensitive distance between two strings.
   
   \param source Input string one
   \param source Input string two
   \param max_str_len The maximum number of characters to consider from
          \p source or \target.  This function would take
          (source.size()+1)*(target.size()+1) time and memory, so feeding
          in large strings on accident could cause excessive memory use, so
          this parameter keeps that from happening, unless it is explicitly
          wanted.  Passing a \p max_str_len of zero will cause this function
          to return 0.  The default value of 128 is arbitrary, but still
          larger than any of the expected use cases in SpecUtils/InterSpec/Cambio.
   */
  SpecUtils_DLLEXPORT
  unsigned int levenshtein_distance( const std::string &source,
                                    const std::string &target,
                                    const size_t max_str_len = 128 );


  
}//namespace SpecUtils

#endif //SpecUtils_StringAlgo_h

