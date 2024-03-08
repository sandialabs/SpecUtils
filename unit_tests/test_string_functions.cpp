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

#include <string>
#include <vector>
#include <random>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdlib.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include <boost/algorithm/string.hpp>

#define BOOST_TEST_MODULE testUtilityStringFunctions
#include <boost/test/unit_test.hpp>

//#define BOOST_TEST_DYN_LINK
// To use boost unit_test as header only (no link to boost unit test library):
//#include <boost/test/included/unit_test.hpp>


#include "SpecUtils/Filesystem.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/ParseUtils.h"

using namespace std;
using namespace boost::unit_test;


BOOST_AUTO_TEST_CASE( testUtilityStringFunctions ) {

  string indir;
  const int argc = framework::master_test_suite().argc;
  for( int i = 1; i < argc-1; ++i )
  {
    if( framework::master_test_suite().argv[i] == string("--indir") )
      indir = framework::master_test_suite().argv[i+1];
  }
  
  indir = "/Users/wcjohns/rad_ana/InterSpec_master/external_libs/SpecUtils/unit_tests/";
  
  string test_in_file, test_out_file;
  const string potential_input_paths[] = { ".", indir, "../testing/", "../../testing/", "../../../testing/" };
  for( const string dir : potential_input_paths )
  {
    const string potential = SpecUtils::append_path( dir, "test_data/txt/test_string_functions_input.txt" );
    if( SpecUtils::is_file(potential) )
    {
      test_in_file = potential;
      test_out_file = SpecUtils::append_path( dir, "test_data/txt/test_string_functions_output.txt" );
    }
  }
  
  if( test_in_file.empty() )
    throw runtime_error( "Could not find 'test_string_functions_input.txt'"
                         " - you may need to specify the '--indir' command line argument" );
    
  // read in file containing utf8 encoded inputs
  ifstream utf8_input;
  utf8_input.open( test_in_file.c_str(), ios::in | ios::binary );
  if( !utf8_input.is_open() )
    throw std::runtime_error( "Can not open file " + test_in_file );
  
  string line;
  vector<string> tests, correctOutput;
  while( SpecUtils::safe_get_line(utf8_input, line)  )
    tests.emplace_back( std::move(line) );
  

  // read in file containing expected outputs
  ifstream utf8_output;
  utf8_output.open( test_out_file.c_str(), ios::in | ios::binary );
  if( !utf8_input.is_open() )
    throw std::runtime_error( "Can not open file " + test_out_file );
  
  while( SpecUtils::safe_get_line(utf8_output, line)  )
    correctOutput.emplace_back( std::move(line) );
  
  BOOST_REQUIRE_GT( tests.size(), 0 );
  BOOST_REQUIRE_GT( correctOutput.size(), 0 );
  
  
  //At the beggining of every line of test_string_functions_input.txt and
  //  test_string_functions_output.txt there is a number followed by a space
  //  indicating which function to test.
  
  size_t index1 = 0; // index of tests vector
  size_t index2 = 0; // index of correctOutput vector

  do
  {
    BOOST_REQUIRE_LT( index1, tests.size() );
    BOOST_REQUIRE_LT( index2, correctOutput.size() );
    BOOST_REQUIRE_EQUAL( tests[index1].substr(0,1), correctOutput[index2].substr(0,1) );
    BOOST_REQUIRE_EQUAL( tests[index1].substr(0,1), "1" );
    
    string test = tests[index1].substr(2);
    SpecUtils::trim(test);
    BOOST_CHECK_EQUAL(test, correctOutput[index2].substr(2));
    index1++; index2++;
  }while( (index1 < tests.size()) && (index2 < correctOutput.size()) && tests[index1].substr(0, 1) == "1" );
  
  // test empty string
  string s = "";
  SpecUtils::trim(s);
  BOOST_CHECK_EQUAL(s, "");
  
  // test string with only whitespace
  s = "   ";
  SpecUtils::trim(s);
  BOOST_CHECK_EQUAL(s, "");


  // tests for SpecUtils::to_lower(string &input) - 2 in text files
  do
  {
    BOOST_REQUIRE_LT( index1, tests.size() );
    BOOST_REQUIRE_LT( index2, correctOutput.size() );
    BOOST_REQUIRE_EQUAL( tests[index1].substr(0,1), correctOutput[index2].substr(0,1) );
    BOOST_REQUIRE_EQUAL( tests[index1].substr(0,1), "2" );
    
    string test = tests[index1].substr(2);
    
    //Currently there are non-ascii strings in the test data - skip them for now
    if( SpecUtils::utf8_str_len(test.c_str(),test.size()) == test.size() )
    {
      SpecUtils::to_lower_ascii(test);
      BOOST_CHECK_EQUAL(test, correctOutput[index2].substr(2));
    }
    
    index1++; index2++;
  }while( (index1 < tests.size()) && (index2 < correctOutput.size()) && tests[index1].substr(0, 1) == "2" );
  
  
  // ASCII tests
  s = "     ";
  SpecUtils::to_lower_ascii(s);
  BOOST_CHECK_EQUAL(s, "     ");
  s = "";
  SpecUtils::to_lower_ascii(s);
  BOOST_CHECK_EQUAL(s, "");
  
  // tests for all printable ASCII characters
  string allASCII;
  for( int i = 32; i < 126; ++i )
    allASCII += (char)i;
  
  string correctAllASCII;
  for( int i = 32; i < 126; ++i)
  {
    if(i>64 && i<91)
      correctAllASCII += (char)(i+32);
    else
      correctAllASCII += (char)i;
  }
  
  SpecUtils::to_lower_ascii(allASCII);
  BOOST_CHECK_EQUAL(allASCII, correctAllASCII);
  
  // tests for ASCII escape characters
  s = '\t';
  string correctS;
  correctS = '\t';
  SpecUtils::to_lower_ascii(s);
  BOOST_CHECK_EQUAL(s, correctS);
  
  s = '\n';
  correctS = '\n';
  SpecUtils::to_lower_ascii(s);
  BOOST_CHECK_EQUAL(s, correctS);
  
  s = '\r';
  correctS = '\r';
  SpecUtils::to_lower_ascii(s);
  BOOST_CHECK_EQUAL(s, correctS);
  
  // test of ASCII string of random lenth and random characters
  random_device r;
  mt19937 m1(r());
  uniform_real_distribution<double> dist1(1, 100);
  mt19937 m2(r());
  uniform_real_distribution<double> dist2(1, 255);
  int random_length = (int)dist1(m1);
  
  string random_ASCII;
  string correct_random_ASCII;
  for(int i = 0; i < random_length; ++i )
  {
    int random = dist2(m2);
    random_ASCII += (char)(random);
    if(random>64 && random<91)
      correct_random_ASCII += (char)(random+32);
    else
      correct_random_ASCII += (char)(random);
  }
  
  SpecUtils::to_lower_ascii(random_ASCII);
  BOOST_CHECK_EQUAL(random_ASCII, correct_random_ASCII);
  
  
  // test for SpecUtils::to_upper_ascii(string &input)
  // 3 in text files
  // string &input is found in testUtilityStringFunctions.txt
  // expected output is found in testUtilityStringFunctionsOutput.txt
  do
  {
    BOOST_REQUIRE_LT( index1, tests.size() );
    BOOST_REQUIRE_LT( index2, correctOutput.size() );
    BOOST_REQUIRE_EQUAL( tests[index1].substr(0,1), correctOutput[index2].substr(0,1) );
    BOOST_REQUIRE_EQUAL( tests[index1].substr(0,1), "3" );
    
    string test = tests[index1].substr(2);
    
    //Currently there are non-ascii strings in the test data - skip them for now
    if( SpecUtils::utf8_str_len(test.c_str(),test.size()) == test.size() )
    {
      SpecUtils::to_upper_ascii(test);
      BOOST_CHECK_EQUAL(test, correctOutput[index2].substr(2));
    }
    
    index1++; index2++;
  } while( (index1 < tests.size()) && (index2 < correctOutput.size()) && tests[index1].substr(0, 1) == "3");
  
  
  s = "     ";
  SpecUtils::to_upper_ascii(s);
  BOOST_CHECK_EQUAL(s, "     ");
  
  s= "";
  SpecUtils::to_upper_ascii(s);
  BOOST_CHECK_EQUAL(s, "");
  
  // test of all printable ASCII characters
  allASCII = "";
  for( int i = 32; i < 126; ++i )
    allASCII+= (char)i;
  
  correctAllASCII = "";
  for(int i = 32; i < 126; ++i)
  {
    if( i > 96 && i < 123)
      correctAllASCII += (char)(i-32);
    else
      correctAllASCII += (char)i;
  }
  SpecUtils::to_upper_ascii(allASCII);
  BOOST_CHECK_EQUAL(allASCII, correctAllASCII);
  
  // tests for ASCII escape characters
  s = '\t';
  correctS = '\t';
  SpecUtils::to_upper_ascii(s);
  BOOST_CHECK_EQUAL(s, correctS);
  
  s = '\n';
  correctS = '\n';
  SpecUtils::to_upper_ascii(s);
  BOOST_CHECK_EQUAL(s, correctS);
  
  s = '\r';
  correctS = '\r';
  SpecUtils::to_upper_ascii(s);
  BOOST_CHECK_EQUAL(s, correctS);
  
  // test for ASCII string of random length with random characters
  random_length = (int)dist1(m1);

  random_ASCII = "";
  correct_random_ASCII = "";
  for( int i = 0; i < random_length; ++i)
  {
    int random = dist2(m2);
    
    random_ASCII += (char)(random);
    if(random>96 && random<123) {
      correct_random_ASCII += (char)(random-32);
    }
    else
      correct_random_ASCII += (char)(random);
  }//
  SpecUtils::to_upper_ascii(random_ASCII);
  BOOST_CHECK_EQUAL(random_ASCII, correct_random_ASCII);
  
  // tests for SpecUtils::iequals_ascii(const char *str, const char *test)
  // 4 in text file
  // each *str is tested with two *test with the first expected to pass and second to fail
  do
  {
    BOOST_REQUIRE_LT( index1, tests.size() );
    BOOST_REQUIRE_LT( index2, correctOutput.size() );
    BOOST_REQUIRE_EQUAL( tests[index1].substr(0,1), correctOutput[index2].substr(0,1) );
    BOOST_REQUIRE_EQUAL( tests[index1].substr(0,1), "4" );
    
    string message;
    string test = tests[index1].substr(2);
    
    if( SpecUtils::utf8_str_len(test.c_str(),test.size()) == test.size() )
    {
      message = test + "  " + correctOutput[index2].substr(2);
      BOOST_CHECK_MESSAGE(SpecUtils::iequals_ascii(test.c_str(), correctOutput[index2].substr(2).c_str()), message);
    }
    
    index2++;
    
    if( SpecUtils::utf8_str_len(test.c_str(),test.size()) == test.size() )
    {
      message = test + "  " + correctOutput[index2].substr(2);
      BOOST_CHECK_MESSAGE(!SpecUtils::iequals_ascii(test.c_str(), correctOutput[index2].substr(2).c_str()), message);
    }
    
    index1++;
    index2++;
  } while( (index1 < tests.size()) && (index2 < correctOutput.size()) && tests[index1].substr(0, 1) == "4");
  
  s = "    ";
  string q;
  BOOST_CHECK( !SpecUtils::iequals_ascii(s.c_str(), q.c_str()) );
  
  // tests for SpecUtils::contains(const string &line, const char *label)
  // 5 in text file
  // each &line is tested with two *label with the first expected to pass and second to fail
  do
  {
    BOOST_REQUIRE_LT( index1, tests.size() );
    BOOST_REQUIRE_LT( index2, correctOutput.size() );
    BOOST_REQUIRE_EQUAL( tests[index1].substr(0,1), correctOutput[index2].substr(0,1) );
    BOOST_REQUIRE_EQUAL( tests[index1].substr(0,1), "5" );
    
    string test = tests[index1].substr(2);
    string message = test + "  " + correctOutput[index2];
    BOOST_CHECK_MESSAGE(SpecUtils::contains(test, correctOutput[index2].substr(2).c_str()), message);
    index2++;
    message = test + "  " + correctOutput[index2];
    BOOST_CHECK_MESSAGE(!SpecUtils::contains(test, correctOutput[index2].substr(2).c_str()), message);
    index2++; index1++;
  } while( (index1 < tests.size()) && (index2 < correctOutput.size()) && tests[index1].substr(0,1) == "5" );
  
  const char *e = "";
  BOOST_CHECK( SpecUtils::contains(q, e) );


  // tests for SpecUtils::icontains(string &line, const char *label)
  // 6 in text file
  // each &line is tested with two *label with the first expected to pass and second to fail
  do
  {
    BOOST_REQUIRE_LT( index1, tests.size() );
    BOOST_REQUIRE_LT( index2, correctOutput.size() );
    BOOST_REQUIRE_EQUAL( tests[index1].substr(0,1), correctOutput[index2].substr(0,1) );
    BOOST_REQUIRE_EQUAL( tests[index1].substr(0,1), "6" );
    
    string test = tests[index1].substr(2);
    string message = test + "  " + correctOutput[index2];
    BOOST_CHECK_MESSAGE(SpecUtils::icontains(test, correctOutput[index2].substr(2).c_str()), message);
    index2++;
    message = test + "  " + correctOutput[index2];
    BOOST_CHECK_MESSAGE(!SpecUtils::icontains(test, correctOutput[index2].substr(2).c_str()), message);
    index2++; index1++;
  } while( (index1 < tests.size()) && (index2 < correctOutput.size()) && tests[index1].substr(0,1) == "6");
  

  // tests for SpecUtils::starts_with(string &line, const char* label)
  // 7 in text file
  // each &line is test with two *label with the first expected to pass and second to fail
  do
  {
    BOOST_REQUIRE_LT( index1, tests.size() );
    BOOST_REQUIRE_LT( index2, correctOutput.size() );
    BOOST_REQUIRE_EQUAL( tests[index1].substr(0,1), correctOutput[index2].substr(0,1) );
    BOOST_REQUIRE_EQUAL( tests[index1].substr(0,1), "7" );
    
    string test = tests[index1].substr(2);
    string message = test + "  " + correctOutput[index2];
    BOOST_CHECK_MESSAGE(SpecUtils::starts_with(test, correctOutput[index2].substr(2).c_str()), message);
    index2++;
    message = test + "  " + correctOutput[index2];
    BOOST_CHECK_MESSAGE(!SpecUtils::starts_with(test, correctOutput[index2].substr(2).c_str()), message);
    index1++; index2++;
  }while( (index1 < tests.size()) && (index2 < correctOutput.size()) && tests[index1].substr(0,1) == "7");


  // tests for SpecUtils::split(vector<string> &resutls, string &input, const char *delims)
  // 8 in text file
  do
  {
    BOOST_REQUIRE_LT( index1, tests.size() );
    BOOST_REQUIRE_LT( index2, correctOutput.size() );
    BOOST_REQUIRE_EQUAL( tests[index1].substr(0,1), correctOutput[index2].substr(0,1) );
    BOOST_REQUIRE_EQUAL( tests[index1].substr(0,1), "8" );
    
    string input = tests[index1].substr(2);
    index1++;
    string delims = tests[index1];
    index1++;
    
    vector<string> results;
    SpecUtils::split(results, input, delims.c_str());
    int expected_length = atoi(correctOutput[index2].substr(2).c_str());
    int length = (int)(results.size());
    BOOST_CHECK_EQUAL(expected_length, length);
    index2++;
    for(int i=0; i<length; i++) {
      BOOST_CHECK_EQUAL(results[i], correctOutput[index2].substr(2));
      index2++;
    }
    results.clear();
  } while( (index1 < tests.size()) && (index2 < correctOutput.size()) && tests[index1].substr(0,1) == "8");
  
  
  string input = "hello how are you doing 543 342 ";
  string originalInput = input;
  const char* delims = "";
  vector<string> results;
  SpecUtils::split(results, input, delims);
  BOOST_CHECK(results.size() == 1);
  BOOST_CHECK_EQUAL(results[0], input);

  SpecUtils::split( results, ",,,hello how are,,", ", " );
  BOOST_CHECK_EQUAL(results.size(), 3);
  BOOST_CHECK_EQUAL(results[0], "hello");
  BOOST_CHECK_EQUAL(results[1], "how");
  BOOST_CHECK_EQUAL(results[2], "are");
  
  SpecUtils::split( results, ",,,hello how are,,", "," );
  BOOST_CHECK_EQUAL(results.size(), 1);
  BOOST_CHECK_EQUAL(results[0], "hello how are");
  
  SpecUtils::split( results, ",hello,,  how     are  ", ", " );
  BOOST_CHECK_EQUAL(results.size(), 3);
  BOOST_CHECK_EQUAL(results[0], "hello");
  BOOST_CHECK_EQUAL(results[1], "how");
  BOOST_CHECK_EQUAL(results[2], "are");
  
  SpecUtils::split( results, ", hello,,  how     are  ", " ;" );
  BOOST_CHECK_EQUAL(results.size(), 4);
  BOOST_CHECK_EQUAL(results[0], ",");
  BOOST_CHECK_EQUAL(results[1], "hello,,");
  BOOST_CHECK_EQUAL(results[2], "how");
  BOOST_CHECK_EQUAL(results[3], "are");
  
  
  SpecUtils::split( results, "hello, how, are,", "," );
  BOOST_CHECK_EQUAL(results.size(), 3);
  BOOST_CHECK_EQUAL(results[0], "hello");
  BOOST_CHECK_EQUAL(results[1], " how");
  BOOST_CHECK_EQUAL(results[2], " are");
  
  input = "hello how are you doing 543 342 ";
  SpecUtils::split_no_delim_compress( results, input, "" );
  BOOST_CHECK(results.size() == 1);
  BOOST_CHECK_EQUAL(results[0], input);

  input = "hello how are you doing 543 342 ";
  SpecUtils::split_no_delim_compress( results, input, "," );
  BOOST_CHECK(results.size() == 1);
  BOOST_CHECK_EQUAL(results[0], input);

  input = ",,,hello how are you doing 543 342 ,,";
  SpecUtils::split_no_delim_compress( results, input, "," );
  BOOST_CHECK(results.size() == 6);
  BOOST_CHECK(results[0].empty());
  BOOST_CHECK(results[1].empty());
  BOOST_CHECK(results[2].empty());
  BOOST_CHECK_EQUAL(results[3], "hello how are you doing 543 342 ");
  BOOST_CHECK(results[4].empty());
  BOOST_CHECK(results[5].empty());


  input = ",A, AAA";
  SpecUtils::split_no_delim_compress( results, input, ", " );
  BOOST_CHECK(results.size() == 4);
  BOOST_CHECK(results[0].empty());
  BOOST_CHECK_EQUAL(results[1], "A");
  BOOST_CHECK(results[2].empty());
  BOOST_CHECK_EQUAL(results[3], "AAA");

  input = ",A, AAA ";
  SpecUtils::split_no_delim_compress( results, input, ", " );
  BOOST_CHECK( results.size() == 5 );
  BOOST_CHECK_EQUAL( results[3], "AAA" );
  BOOST_CHECK( results[4].empty() );
    

  // tests for SpecUtils::ireplace_all(string &input, const char *pattern, const char *replacement)
  // 9 in text file
  do
  {
    //Failed to replace '\x5f\x54' with '\x54' in '\x20\xffffffff\x5f\x5f\x25\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x25\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\xffffff9b\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x54\x54\x3c\x50\x4d\x54'.
	  //Expected: '\x20\xffffffff\x5f\x5f\x25\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x25\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\xffffff9b\x54\x54\x3c\x50\x4d\x54' (length 21)
	  //Got:      '\x20\xffffffff\x5f\x5f\x25\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x25\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\xffffff9b\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x5f\x54\x54\x3c\x50\x4d\x54' (length 3e).

  
    BOOST_REQUIRE_LT( index1, tests.size() );
    BOOST_REQUIRE_LT( index2, correctOutput.size() );
    BOOST_REQUIRE_EQUAL( tests[index1].substr(0,1), correctOutput[index2].substr(0,1) );
    BOOST_REQUIRE_EQUAL( tests[index1].substr(0,1), "9" );
    
    string input = tests[index1].substr(2);
    index1++;
    string pattern = tests[index1].substr(2);
    index1++;
    string replacement = tests[index1].substr(2);
    index1++;
    SpecUtils::ireplace_all(input, pattern.c_str(), replacement.c_str());
    BOOST_CHECK_EQUAL(input, correctOutput[index2].substr(2));
    index2++;
  } while( (index1 < tests.size()) && (index2 < correctOutput.size()) && tests[index1].substr(0,1) == "9" );
  
  
  //istarts_with
  //iends_with
  //starts_with
  //erase_any_character

  
  
}

BOOST_AUTO_TEST_CASE( checkIFind )
{
  string corpus = "Hello Dude";
  const char *substr = "dude";
  size_t correct_substr_pos = 6;
  size_t substr_pos = SpecUtils::ifind_substr_ascii(corpus, substr);
  BOOST_CHECK_EQUAL(substr_pos, correct_substr_pos);
  
  corpus = "Dude";
  substr = "Dude";
  correct_substr_pos = 0;
  substr_pos = SpecUtils::ifind_substr_ascii(corpus, substr);
  BOOST_CHECK_EQUAL(substr_pos, correct_substr_pos);
  
  
  corpus = "Dude what";
  substr = "Dude";
  correct_substr_pos = 0;
  substr_pos = SpecUtils::ifind_substr_ascii(corpus, substr);
  BOOST_CHECK_EQUAL(substr_pos, correct_substr_pos);
  
  corpus = "Dude  what";
  substr = "  ";
  correct_substr_pos = 4;
  substr_pos = SpecUtils::ifind_substr_ascii(corpus, substr);
  BOOST_CHECK_EQUAL(substr_pos, correct_substr_pos);
  
  
  corpus = "Dude what";
  substr = "--";
  correct_substr_pos = std::string::npos;
  substr_pos = SpecUtils::ifind_substr_ascii(corpus, substr);
  BOOST_CHECK_EQUAL(substr_pos, correct_substr_pos);
  
  
  corpus = "--";
  substr = "---";
  correct_substr_pos = std::string::npos;
  substr_pos = SpecUtils::ifind_substr_ascii(corpus, substr);
  BOOST_CHECK_EQUAL(substr_pos, correct_substr_pos);

  corpus = "-a--";
  substr = "---";
  correct_substr_pos = std::string::npos;
  substr_pos = SpecUtils::ifind_substr_ascii(corpus, substr);
  BOOST_CHECK_EQUAL(substr_pos, correct_substr_pos);

  corpus = "-a--";
  substr = "-";
  correct_substr_pos = 0;
  substr_pos = SpecUtils::ifind_substr_ascii(corpus, substr);
  BOOST_CHECK_EQUAL(substr_pos, correct_substr_pos);
  
  corpus = "-a--";
  substr = "--";
  correct_substr_pos = 2;
  substr_pos = SpecUtils::ifind_substr_ascii(corpus, substr);
  BOOST_CHECK_EQUAL(substr_pos, correct_substr_pos);
  
  
  corpus = "A";
  substr = "a";
  correct_substr_pos = 0;
  substr_pos = SpecUtils::ifind_substr_ascii(corpus, substr);
  BOOST_CHECK_EQUAL(substr_pos, correct_substr_pos);
  
  corpus = "Aa";
  substr = "a";
  correct_substr_pos = 0;
  substr_pos = SpecUtils::ifind_substr_ascii(corpus, substr);
  BOOST_CHECK_EQUAL(substr_pos, correct_substr_pos);
  
  corpus = "A - BEACh";
  substr = "bEACH";
  correct_substr_pos = 4;
  substr_pos = SpecUtils::ifind_substr_ascii(corpus, substr);
  BOOST_CHECK_EQUAL(substr_pos, correct_substr_pos);
  
  corpus = "shor";
  substr = "LongerString";
  correct_substr_pos = string::npos;
  substr_pos = SpecUtils::ifind_substr_ascii(corpus, substr);
  BOOST_CHECK_EQUAL(substr_pos, correct_substr_pos);
  
  corpus = "12345";
  substr = "23";
  correct_substr_pos = 1;
  substr_pos = SpecUtils::ifind_substr_ascii(corpus, substr);
  BOOST_CHECK_EQUAL(substr_pos, correct_substr_pos);
}//BOOST_AUTO_TEST_CASE( checkIFind )



BOOST_AUTO_TEST_CASE( testPrintCompact )
{
  using namespace std;
  
//   auto printtest = [](float energy, size_t prec ){
//   cout << "assert( SpecUtils::printCompact(" << std::setprecision(7) << energy << ","
//   << prec << ") == \"" << SpecUtils::printCompact( energy, prec ) << "\" );" << endl;
//   };
//
//   cout << endl << endl;
//
//   printtest( 0.00000001, 2 );
//   printtest( 0.00001, 7 );
//   printtest( 0.00001, 1 );
//   printtest( 1.0001, 3 );
//   printtest( 1.0001, 4 );
//   printtest( 1.0001, 5 );
//   printtest( 1.0001, 6 );
//   printtest( 100000, 2 );
//   printtest( 80999, 2 );
//   printtest( 89999, 2 );
//   printtest( 99999, 2 );
//   printtest( 100000, 8 );
//   printtest( 100000000, 2 );
//   printtest( 1.2345, 1 );
//   printtest( 1.2345, 2 );
//   printtest( 1.2345, 3 );
//   printtest( 1.2345, 4 );
//   printtest( 1.2345, 5 );
//   printtest( 1.2345, 6 );
//   printtest( 1.2345, 7 );
//   printtest( 1234.5, 4 );
//   printtest( 1234.5, 5 );
//   printtest( 1235.5, 4 );
//   printtest( 1235.5, 5 );
//   printtest( -1234.5, 5 );
//
//   printtest( 999.9, 2 );
//   printtest( 999.9, 3 );
//   printtest( 999.9, 4 );
//   printtest( 999.9, 5 );
//
//   printtest( 0.9999, 1 );
//   printtest( 0.9999, 2 );
//   printtest( 0.9999, 3 );
//   printtest( 0.9999, 4 );
//
//   printtest( 0.998, 3 );
//   printtest( 0.998, 2 );
//   printtest( 0.998, 1 );
//
//   printtest( -0.998, 3 );
//   printtest( -0.998, 2 );
//   printtest( -0.998, 1 );
//
//   printtest( 1.998, 1 );
//   printtest( 1.998, 2 );
//   printtest( 1.998, 3 );
//   printtest( 1.998, 4 );
//
//   printtest( -1.998, 1 );
//   printtest( -1.998, 2 );
//   printtest( -1.998, 3 );
//   printtest( -1.998, 4 );
//
//
//   printtest( 0.00998, 1 );
//   printtest( 0.00998, 2 );
//   printtest( 0.00998, 3 );
//   printtest( 0.00998, 4 );
//   printtest( 0.00998, 5 );
//   printtest( 0.00998, 6 );
//
//   cout << endl << endl;
//
  BOOST_CHECK( SpecUtils::printCompact(1e-08,2) == "1E-8" );
  BOOST_CHECK( SpecUtils::printCompact(1e-05,7) == "1E-5" );
  BOOST_CHECK( SpecUtils::printCompact(1e-05,1) == "1E-5" );
  BOOST_CHECK( SpecUtils::printCompact(1.0001,3) == "1" );
  BOOST_CHECK( SpecUtils::printCompact(1.0001,4) == "1" );
  BOOST_CHECK( SpecUtils::printCompact(1.0001,5) == "1.0001" );
  BOOST_CHECK( SpecUtils::printCompact(1.0001,6) == "1.0001" );
  BOOST_CHECK( SpecUtils::printCompact(100000,2) == "1E5" );
  BOOST_CHECK( SpecUtils::printCompact(80999,2) == "80999" );
  BOOST_CHECK( SpecUtils::printCompact(89999,2) == "9E4" );
  BOOST_CHECK( SpecUtils::printCompact(99999,2) == "1E5" );
  BOOST_CHECK( SpecUtils::printCompact(100000,8) == "1E5" );
  BOOST_CHECK( SpecUtils::printCompact(1e+08,2) == "1E8" );
  BOOST_CHECK( SpecUtils::printCompact(1.2345,1) == "1" );
  BOOST_CHECK( SpecUtils::printCompact(1.2345,2) == "1.2" );
  BOOST_CHECK( SpecUtils::printCompact(1.2345,3) == "1.23" );
  BOOST_CHECK( SpecUtils::printCompact(1.2345,4) == "1.234" );
  BOOST_CHECK( SpecUtils::printCompact(1.2345,5) == "1.2345" );
  BOOST_CHECK( SpecUtils::printCompact(1.2345,6) == "1.2345" );
  BOOST_CHECK( SpecUtils::printCompact(1.2345,7) == "1.2345" );
  BOOST_CHECK( SpecUtils::printCompact(1234.5,4) == "1234" );
  BOOST_CHECK( SpecUtils::printCompact(1234.5,5) == "1234.5" );
  BOOST_CHECK( SpecUtils::printCompact(1235.5,4) == "1236" );
  BOOST_CHECK( SpecUtils::printCompact(1235.5,5) == "1235.5" );
  BOOST_CHECK( SpecUtils::printCompact(-1234.5,5) == "-1234.5" );
  BOOST_CHECK( SpecUtils::printCompact(999.9,2) == "1E3" );
  BOOST_CHECK( SpecUtils::printCompact(999.9,3) == "1E3" );
  BOOST_CHECK( SpecUtils::printCompact(999.9,4) == "999.9" );
  BOOST_CHECK( SpecUtils::printCompact(999.9,5) == "999.9" );
  BOOST_CHECK( SpecUtils::printCompact(0.9999,1) == "1" );
  BOOST_CHECK( SpecUtils::printCompact(0.9999,2) == "1" );
  BOOST_CHECK( SpecUtils::printCompact(0.9999,3) == "1" );
  BOOST_CHECK( SpecUtils::printCompact(0.9999,4) == "0.9999" );
  BOOST_CHECK( SpecUtils::printCompact(0.998,3) == "0.998" );
  BOOST_CHECK( SpecUtils::printCompact(0.998,2) == "1" );
  BOOST_CHECK( SpecUtils::printCompact(0.998,1) == "1" );
  BOOST_CHECK( SpecUtils::printCompact(-0.998,3) == "-0.998" );
  BOOST_CHECK( SpecUtils::printCompact(-0.998,2) == "-1" );
  BOOST_CHECK( SpecUtils::printCompact(-0.998,1) == "-1" );
  BOOST_CHECK( SpecUtils::printCompact(1.998,1) == "2" );
  BOOST_CHECK( SpecUtils::printCompact(1.998,2) == "2" );
  BOOST_CHECK( SpecUtils::printCompact(1.998,3) == "2" );
  BOOST_CHECK( SpecUtils::printCompact(1.998,4) == "1.998" );
  BOOST_CHECK( SpecUtils::printCompact(-1.998,1) == "-2" );
  BOOST_CHECK( SpecUtils::printCompact(-1.998,2) == "-2" );
  BOOST_CHECK( SpecUtils::printCompact(-1.998,3) == "-2" );
  BOOST_CHECK( SpecUtils::printCompact(-1.998,4) == "-1.998" );
  BOOST_CHECK( SpecUtils::printCompact(0.00998,1) == "0.01" );
  BOOST_CHECK( SpecUtils::printCompact(0.00998,2) == "0.01" );
  BOOST_CHECK( SpecUtils::printCompact(0.00998,3) == "0.00998" );
  BOOST_CHECK( SpecUtils::printCompact(0.00998,4) == "0.00998" );
  BOOST_CHECK( SpecUtils::printCompact(0.00998,5) == "0.00998" );
  BOOST_CHECK( SpecUtils::printCompact(0.00998,6) == "0.00998" );
  BOOST_CHECK( SpecUtils::printCompact(std::numeric_limits<double>::infinity(),6) == "inf" );
  BOOST_CHECK( SpecUtils::printCompact(std::numeric_limits<double>::quiet_NaN(),6) == "nan" );
  
  auto check_range = []( const double lower, const double upper ){
    const size_t nchecks = 100;
    std::default_random_engine generator;
    std::uniform_real_distribution<double> flt_distribution(lower,upper);
    std::uniform_int_distribution<size_t> int_distribution(1,9);
    
    for( size_t i = 0; i < nchecks; ++i )
    {
      const double number = flt_distribution(generator);
      const size_t nsig = int_distribution(generator);
      const string strval = SpecUtils::printCompact( number, nsig );
      
      double readinval;
      const int nread = sscanf( strval.c_str(), "%lf", &readinval );
      BOOST_CHECK( nread == 1 );
      
      const double eps = 0.5 * std::pow(10.0, -(nsig - 1)) * fabs(number);

     //Example: if number is 1.2345E12 --> 1234.5E9, printed to 4 sig figs, then eps is 0.5*1.2345E9

      BOOST_CHECK( fabs( fabs(number) - fabs(readinval) ) <= eps );
      BOOST_CHECK( number==0.0 || readinval==0.0 || (std::signbit(number) == std::signbit(readinval)) );
    }
  };//check_range(...)
  
  check_range(-1.0,1.0);
  check_range(-2.1,2.1);
  check_range(-1000000.0,10000000.0);
  check_range(-1.0E32,1.0E32);
}//void testPrintCompact()
