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
#include <chrono>
#include <ostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <climits>
#include <float.h>
#include <boost/algorithm/string.hpp>

#define BOOST_TEST_MODULE split_to_floats_and_ints_suite
#include <boost/test/unit_test.hpp>

//#define BOOST_TEST_DYN_LINK
// To use boost unit_test as header only (no link to boost unit test library):
//#include <boost/test/included/unit_test.hpp>


#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/ParseUtils.h"

using namespace std;
using namespace boost::unit_test;


BOOST_AUTO_TEST_SUITE( split_to_floats_and_ints_suite )


// TODO: add test case for parse_float, parse_int, parse_double, functions

/*
BOOST_AUTO_TEST_CASE( time_float_parse  )
{
  ifstream file( "float_benchmark.csv" );
	
  assert( file.is_open() );
  
  vector<string> lines;
  size_t nchars = 0;
  string line;
  while( SpecUtils::safe_get_line(file, line) )
  {
    nchars += line.size();
    lines.push_back( line );
  }
  
  cout << "Read in " << lines.size() << " lines, and " << nchars << " bytes." << endl;
  
  const size_t ntimes = 100;
  
  std::chrono::high_resolution_clock::duration total_dur{};
  for( const string &val : lines )
  {
    const auto start = std::chrono::high_resolution_clock::now();
    for( size_t i = 0; i < ntimes; ++i )
    {
			vector<float> float_vals;
      SpecUtils::split_to_floats( val.c_str(), val.size(), float_vals );
    }
    const auto end = std::chrono::high_resolution_clock::now();
    
    total_dur += end - start;
  }//for( const string &val : lines )
  
  cout << "Took: " << std::chrono::duration_cast<std::chrono::milliseconds>(total_dur).count() << " ms to parse all the input." << endl;
}//BOOST_AUTO_TEST_CASE( time_float_parse  )
*/


BOOST_AUTO_TEST_CASE( split_to_floats  )
{
	string input_string1= "5.5 4.5 3.5,34000000,1.23456,1.234567,1.2345678,1.23456789,.1,.01,.001,.0001,.00001,.000001,.0000001,.00000001";
	const vector<float> comparison_vector1{5.5f, 4.5f, 3.5f,34000000.0f,1.23456f,1.234567f,1.2345678f,1.23456789f,0.1f,0.01f,0.001f,0.0001f,0.00001f,0.000001f,0.0000001f,0.00000001f};
	vector<float> output_vector1, alt_output_vector1;
	SpecUtils::split_to_floats( input_string1, output_vector1 );
	SpecUtils::split_to_floats( &(input_string1[0]), alt_output_vector1," ,\r\n\t", false );
  
  BOOST_REQUIRE_EQUAL( output_vector1.size(), comparison_vector1.size() );
  BOOST_REQUIRE_EQUAL( alt_output_vector1.size(), comparison_vector1.size() );
  
	for( int i = 0; i < output_vector1.size(); ++i )
	{
    BOOST_CHECK_CLOSE( comparison_vector1[i], output_vector1[i], 0.000001);
	}

	for( int i = 0; i < alt_output_vector1.size(); ++i )
	{
		BOOST_CHECK_CLOSE( comparison_vector1[i], alt_output_vector1[i], 0.000001 );
  }
}


BOOST_AUTO_TEST_CASE( parse_float )
{
	const string good_input_strs[] = {
    "3.2", "-3.2", "+3.2", " 3.2 ", "\t\t\t\t3.2",
    "3.2\t ", "3.2\n", "\n3.2", "3.2 somestr", ".2",
    "2.", "+.2", "-.2", "1.23", "1.23E-6",
    "1.24E+4", "1.24E6", "1.24E06", "+1.24E+06", "+1.24E-06",
    "-1.24E-06", "1.2.3", "12. somestr", "12.somestr", "1.1E0",
    "-1.1E1", "+0.0", "-0", ".2Eeee", ".2E0eee",
    "-.22222E3", "13.2\0", "  3.22 \0\0", "3.2\08"
  };
  
	const float good_input_vals[] =  {
    3.2f, -3.2f, +3.2f, 3.2f, 3.2f,
    3.2f, 3.2f, 3.2f, 3.2f, .2f,
    2.0f, +.2f, -.2f, 1.23f, 1.23E-6f,
    1.24E+4f, 1.24E6f, 1.24E06f, +1.24E+06f, +1.24E-06f,
    -1.24E-06f, 1.2f, 12.0f, 12.0f, 1.1f,
    -11.0f, 0.0f, 0.0f, 0.2f, 0.2f,
    -222.22f, 13.2f, 3.22f, 3.2f
  };
	const string bad_input_strs[] = {"", "aa", "a2.3", "?+1.2", "somestr 3.4", "-\03.2", "- 99" };	
	
	const size_t num_good_input_strs = sizeof(good_input_strs) / sizeof(good_input_strs[0]);
	const size_t num_good_inputs_vals = sizeof(good_input_vals) / sizeof(good_input_vals[0]);
	const size_t num_bad_inputs_strs = sizeof(bad_input_strs) / sizeof(bad_input_strs[0]);
	
  BOOST_REQUIRE_EQUAL( num_good_input_strs, num_good_inputs_vals );
	
	
	for( size_t i = 0; i < num_good_input_strs; ++i )
	{
		float result;
		const string &s = good_input_strs[i];
		const bool ok = SpecUtils::parse_float( s.c_str(), s.size(), result );
		BOOST_CHECK_MESSAGE( ok, "Failed to parse '" << s << " to a float'" );
		//BOOST_CHECK_SMALL( result - good_input_vals[i], 0.00001*good_input_vals[i] )
		BOOST_CHECK_CLOSE( result, good_input_vals[i], fabs(good_input_vals[i])*0.000001 );
	}

  for( size_t i = 0; i < num_bad_inputs_strs; ++i )
	{
		float result;
		const string &s = bad_input_strs[i];
		const bool ok = SpecUtils::parse_float( s.c_str(), s.size(), result );
		BOOST_CHECK_MESSAGE( !ok, "Parsed '" << s << "' and got " << result << " when shouldnt have" );
		BOOST_CHECK_EQUAL(result,0.0f);
	}
	
	
	{ 
		float result;
		const char *txt = "3.2";
	  bool ok = SpecUtils::parse_float( txt, strlen(txt)-1, result );
	  BOOST_CHECK( ok && result == 3.0f );
		
		ok = SpecUtils::parse_float( txt, strlen(txt)-2, result );
	  BOOST_CHECK( ok && result == 3.0f );
		
		txt = "  +3.256 ";
	  ok = SpecUtils::parse_float( txt, strlen(txt)-3, result );
	  BOOST_CHECK( ok && result == 3.2f );
		
		txt = "\t0.2";
	  ok = SpecUtils::parse_float( txt, strlen(txt)-1, result );
	  BOOST_CHECK( ok && result == 0.0f );
	}
	
	{
		float result;
	  bool ok = SpecUtils::parse_float( NULL, 0, result );
	  BOOST_CHECK( !ok );
	}
	
}

BOOST_AUTO_TEST_CASE( check_trailing_characters  )
{
  bool ok;
  vector<float> results;
  const char *input;
  size_t inputlen;

  input = "9.9, 88.3, 0, 10, 0.0, 9, -1 0 0.0 0,  1 , \t\n";
  inputlen = strlen( input );
  
  vector<float> good_input_vals{ 9.9, 88.3, 0, 10, 0.0, 9, -1, 0, 0.0, 0, 1 };
  ok = SpecUtils::split_to_floats( input, inputlen, results );
  BOOST_CHECK( ok );
  BOOST_CHECK_EQUAL( results.size(), good_input_vals.size() );
  for( size_t i = 0; i < good_input_vals.size(); ++i )
    BOOST_CHECK_CLOSE( results[i], good_input_vals[i], fabs(good_input_vals[i])*0.000001 );
  

  ok = SpecUtils::split_to_floats( input, results, " ,\t\n", false );
  BOOST_CHECK( ok );
  BOOST_CHECK_EQUAL( results.size(), good_input_vals.size() );
  for( size_t i = 0; i < good_input_vals.size(); ++i )
    BOOST_CHECK_CLOSE( results[i], good_input_vals[i], fabs(good_input_vals[i])*0.000001 );


  input = "9.9 0.0 0 1 abs";
  inputlen = strlen( input );
  good_input_vals = vector<float>{ 9.9, 0.0, 0, 1 };
  ok = SpecUtils::split_to_floats( input, inputlen, results );
  BOOST_CHECK( !ok );
  BOOST_CHECK_EQUAL( results.size(), good_input_vals.size() );
  for( size_t i = 0; i < good_input_vals.size(); ++i )
    BOOST_CHECK_CLOSE( results[i], good_input_vals[i], fabs(good_input_vals[i])*0.000001 );

  ok = SpecUtils::split_to_floats( input, results, " ", false );
  BOOST_CHECK( !ok );
  BOOST_CHECK_EQUAL( results.size(), good_input_vals.size() );
  for( size_t i = 0; i < good_input_vals.size(); ++i )
    BOOST_CHECK_CLOSE( results[i], good_input_vals[i], fabs(good_input_vals[i])*0.000001 );
  
  
  input = "9.9 0.0 0 1 -";
  inputlen = strlen( input );
  good_input_vals = vector<float>{ 9.9, 0.0, 0, 1 };
  ok = SpecUtils::split_to_floats( input, inputlen, results );
  BOOST_CHECK( !ok );
  BOOST_CHECK_EQUAL( results.size(), good_input_vals.size() );
  for( size_t i = 0; i < good_input_vals.size(); ++i )
    BOOST_CHECK_CLOSE( results[i], good_input_vals[i], fabs(good_input_vals[i])*0.000001 );

  ok = SpecUtils::split_to_floats( input, results, " ", false );
  BOOST_CHECK( !ok );

  input = "9.9 0.0 0 1 +";
  inputlen = strlen( input );
  good_input_vals = vector<float>{ 9.9, 0.0, 0, 1 };
  ok = SpecUtils::split_to_floats( input, inputlen, results );
  BOOST_CHECK( !ok );
  BOOST_CHECK_EQUAL( results.size(), good_input_vals.size() );
  for( size_t i = 0; i < good_input_vals.size(); ++i )
    BOOST_CHECK_CLOSE( results[i], good_input_vals[i], fabs(good_input_vals[i])*0.000001 );

  ok = SpecUtils::split_to_floats( input, results, " ", false );
  BOOST_CHECK( !ok );
  BOOST_CHECK_EQUAL( results.size(), good_input_vals.size() );
  for( size_t i = 0; i < good_input_vals.size(); ++i )
    BOOST_CHECK_CLOSE( results[i], good_input_vals[i], fabs(good_input_vals[i])*0.000001 );

  input = "9.9 0.0 0 1 ";
  inputlen = strlen( input );
  good_input_vals = vector<float>{ 9.9, 0.0, 0, 1 };
  ok = SpecUtils::split_to_floats( input, inputlen, results );

  BOOST_CHECK( ok );
  BOOST_CHECK_EQUAL( results.size(), good_input_vals.size() );
  for( size_t i = 0; i < good_input_vals.size(); ++i )
    BOOST_CHECK_CLOSE( results[i], good_input_vals[i], fabs(good_input_vals[i])*0.000001 );
}



BOOST_AUTO_TEST_CASE( split_to_floats_cambio_fix  )
{
  const char *input = "9.9, 88.3, 0, 10, 0.0, 9, -1 0 0.0 0,0,  1";

  vector<float> with_fix, without_fix;
  const bool with_fix_ok
  = SpecUtils::split_to_floats( input, with_fix, " ,\r\n\t", true );

  const bool without_fix_ok
  = SpecUtils::split_to_floats( input, without_fix, " ,\r\n\t", false );

  BOOST_CHECK( with_fix_ok );
  BOOST_CHECK( without_fix_ok );

  BOOST_CHECK_EQUAL( with_fix.size(), 12 );
  BOOST_CHECK_EQUAL( without_fix.size(), 12 );

  BOOST_CHECK_EQUAL( with_fix[2], 0.0f );
  BOOST_CHECK_EQUAL( without_fix[2], 0.0f );

  BOOST_CHECK_EQUAL( with_fix[3], 10.0f );
  BOOST_CHECK_EQUAL( without_fix[3], 10.0f );

  BOOST_CHECK_EQUAL( with_fix[4], FLT_MIN );
  BOOST_CHECK_EQUAL( without_fix[4], 0.0f );

  BOOST_CHECK_EQUAL( with_fix[5], 9.0f );
  BOOST_CHECK_EQUAL( without_fix[5], 9.0f );

  BOOST_CHECK_EQUAL( with_fix[6], -1.0f );
  BOOST_CHECK_EQUAL( without_fix[6], -1.0f );

  BOOST_CHECK_EQUAL( with_fix[7], 0.0f );
  BOOST_CHECK_EQUAL( without_fix[7], 0.0f );

  BOOST_CHECK_EQUAL( with_fix[8], FLT_MIN );
  BOOST_CHECK_EQUAL( without_fix[8], 0.0f );

  BOOST_CHECK_EQUAL( with_fix[9], 0.0f );
  BOOST_CHECK_EQUAL( without_fix[9], 0.0f );

  BOOST_CHECK_EQUAL( with_fix[10], 0.0f );
  BOOST_CHECK_EQUAL( without_fix[10], 0.0f );

  BOOST_CHECK_EQUAL( with_fix[11], 1.0f );
  BOOST_CHECK_EQUAL( without_fix[11], 1.0f );
}


BOOST_AUTO_TEST_CASE( shouldnt_parse_any_floats  )
{
  const char *input_strings[]
   = {
       "     Energy, Data",
       "TSA,12/7/2011,53:30.3,No Slot,,NB,1,1,1,1"
   };
  const size_t nstr = sizeof(input_strings) / sizeof(input_strings[0]);

  for( size_t i = 0; i < nstr; ++i )
  {
    vector<float> results, alt_results;
    string input_string = input_strings[i];

    bool success = SpecUtils::split_to_floats( input_string, results );
//    BOOST_CHECK_EQUAL( success, false );
    BOOST_CHECK_EQUAL( results.size(), 0 );

    success = SpecUtils::split_to_floats( &(input_string[0]), alt_results, " ,\r\n\t",false);
//    BOOST_CHECK_EQUAL( success, false );
    BOOST_CHECK_EQUAL( alt_results.size(), 0 );
  }
}


BOOST_AUTO_TEST_CASE( split_to_floats2  )
{
	string input_string2= "5.5, 4.5, 3.5";
	float temp2[] = {5.5, 4.5, 3.5};
	vector<float> output_vector2,alt_output_vector2;
	vector<float> comparison_vector2(temp2,temp2+3);
	SpecUtils::split_to_floats(input_string2,output_vector2);
	SpecUtils::split_to_floats(&(input_string2[0]),alt_output_vector2," ,\r\n\t",false);

  for(int i = 0; i < output_vector2.size(); ++i )
	{
		BOOST_CHECK_EQUAL(comparison_vector2[i],output_vector2[i]);
	}

  for( int i = 0; i < alt_output_vector2.size(); ++i )
	{
		BOOST_CHECK_EQUAL(comparison_vector2[i],alt_output_vector2[i]);
	}
}


BOOST_AUTO_TEST_CASE( split_to_floats3  )
{
  bool success;
  string input_string3= "0 1 2 3 0 1 2 3 0.0 1.0 2.0 3.0 0.0 1.0 2.0 3.0";
	float temp3[] = {0,1,2,3,0.0,1.0,2.0,3.0,0,1,2,3,0.0,1.0,2.0,3.0};
  const size_t temp3_len = sizeof(temp3)/sizeof(temp3[0]);

	vector<float> output_vector3,alt_output_vector3;
	vector<float> comparison_vector3(temp3,temp3+16);
	success = SpecUtils::split_to_floats(input_string3,output_vector3);
  BOOST_CHECK_EQUAL( success, true );

	success = SpecUtils::split_to_floats(&(input_string3[0]),alt_output_vector3," ,\r\n\t",false);
  BOOST_CHECK_EQUAL( success, true );

  BOOST_CHECK_EQUAL( output_vector3.size(), temp3_len );
  BOOST_CHECK_EQUAL( alt_output_vector3.size(), temp3_len );

	for( int i = 0; i < output_vector3.size(); ++i )
	{
		BOOST_CHECK_EQUAL(comparison_vector3[i],output_vector3[i]);
	}

	for( int i = 0; i < alt_output_vector3.size(); ++i )
	{
		BOOST_CHECK_EQUAL(comparison_vector3[i],alt_output_vector3[i]);
	}


  string input_string4= "1.11 2.22 3.33 4.44 5.55";
	float temp4[] = {1.11, 2.22, 3.33, 4.44, 5.55};
  const size_t temp4_len = sizeof(temp4)/sizeof(temp4[0]);

	vector<float> output_vector4,alt_output_vector4;
	vector<float> comparison_vector4(temp4,temp4+5);
	success = SpecUtils::split_to_floats(input_string4,output_vector4);
  BOOST_CHECK_EQUAL( success, true );

	success = SpecUtils::split_to_floats(&(input_string4[0]),alt_output_vector4," ,\r\n\t",false);
  BOOST_CHECK_EQUAL( success, true );

  BOOST_CHECK_EQUAL( output_vector4.size(), temp4_len );
  BOOST_CHECK_EQUAL( alt_output_vector4.size(), temp4_len );

	for( int i = 0; i < output_vector4.size(); ++i)
	{
		BOOST_CHECK_EQUAL(comparison_vector4[i],output_vector4[i]);
	}

  for( int i=0; i < alt_output_vector4.size(); ++i )
	{
		BOOST_CHECK_EQUAL(comparison_vector4[i],alt_output_vector4[i]);
	}


  string input_string5= "5.512345 4.512345 3.512345";
	float temp5[] = {5.512345f, 4.512345f, 3.512345f};
  const size_t temp5_len = sizeof(temp5)/sizeof(temp5[0]);

	vector<float> output_vector5,alt_output_vector5;
	vector<float> comparison_vector5(temp5,temp5+3);
	success = SpecUtils::split_to_floats(input_string5,output_vector5);
  BOOST_CHECK_EQUAL( success, true );

	success = SpecUtils::split_to_floats(&(input_string5[0]),alt_output_vector5," ,\r\n\t",false);
  BOOST_CHECK_EQUAL( success, true );

  BOOST_CHECK_EQUAL( output_vector5.size(), temp5_len );
  BOOST_CHECK_EQUAL( alt_output_vector5.size(), temp5_len );


	for( int i = 0; i < output_vector5.size(); ++i )
	{
		BOOST_CHECK_EQUAL(comparison_vector5[i],output_vector5[i]);
	}

	for (int i=0;i<alt_output_vector5.size();++i)
	{
		BOOST_CHECK_EQUAL(comparison_vector5[i],alt_output_vector5[i]);
	}

  string input_string6= "5.5 1.234567";
	float temp6[] = {5.5f, 1.234567f};
  const size_t temp6_len = sizeof(temp6)/sizeof(temp6[0]);

	vector<float> output_vector6,alt_output_vector6;
	vector<float> comparison_vector6(temp6,temp6+2);
	success = SpecUtils::split_to_floats(input_string6,output_vector6);
  BOOST_CHECK_EQUAL( success, true );

	success = SpecUtils::split_to_floats(&(input_string6[0]),alt_output_vector6," ,\r\n\t",false);
  BOOST_CHECK_EQUAL( success, true );

  BOOST_CHECK_EQUAL( output_vector6.size(), temp6_len );
  BOOST_CHECK_EQUAL( alt_output_vector6.size(), temp6_len );

	for( int i = 0; i < output_vector6.size(); ++i )
	{
		BOOST_CHECK_EQUAL(comparison_vector6[i],output_vector6[i]);
	}

	for( int i = 0; i < alt_output_vector6.size(); ++i )
	{
		BOOST_CHECK_EQUAL(comparison_vector6[i],alt_output_vector6[i]);
	}

  string input_string7= "5.5       4.67";
	float temp7[] = {5.5f, 4.67f};
  const size_t temp7_len = sizeof(temp7)/sizeof(temp7[0]);

	vector<float> output_vector7,alt_output_vector7;
	vector<float> comparison_vector7(temp7,temp7+2);
	success = SpecUtils::split_to_floats(input_string7,output_vector7);
  BOOST_CHECK_EQUAL( success, true );

	success = SpecUtils::split_to_floats(&(input_string7[0]),alt_output_vector7," ,\r\n\t",false);
  BOOST_CHECK_EQUAL( success, true );

  BOOST_CHECK_EQUAL( output_vector7.size(), temp7_len );
  BOOST_CHECK_EQUAL( alt_output_vector7.size(), temp7_len );

  for( int i=0; i < output_vector7.size(); ++i )
	{
		BOOST_CHECK_EQUAL(comparison_vector7[i],output_vector7[i]);
	}

	for( int i = 0; i < alt_output_vector7.size(); ++i )
	{
		BOOST_CHECK_EQUAL(comparison_vector7[i],alt_output_vector7[i]);
	}

  string input_string8= "5.5,4.5,,3.5,,,,,2.5";
	float temp8[] = {5.5, 4.5, 3.5, 2.5};
  const size_t temp8_len = sizeof(temp8)/sizeof(temp8[0]);

	vector<float> output_vector8,alt_output_vector8;
	vector<float> comparison_vector8(temp8,temp8+4);
	success = SpecUtils::split_to_floats(input_string8,output_vector8);
  BOOST_CHECK_EQUAL( success, true );

	success = SpecUtils::split_to_floats(&(input_string8[0]),alt_output_vector8," ,\r\n\t",false);
  BOOST_CHECK_EQUAL( success, true );

  BOOST_CHECK_EQUAL( output_vector8.size(), temp8_len );
  BOOST_CHECK_EQUAL( alt_output_vector8.size(), temp8_len );

	for( int i = 0; i < output_vector8.size(); ++i )
	{
		BOOST_CHECK_EQUAL(comparison_vector8[i],output_vector8[i]);
	}

	for( int i = 0; i < alt_output_vector8.size(); ++i )
	{
		BOOST_CHECK_EQUAL(comparison_vector8[i],alt_output_vector8[i]);
	}

  //Note that the value before the decimal point must be smaller than 4294967296

  string input_string9= "-5.5 -4.5 -3.5,-3000000000,4294967000,5.0E9,-1.23456,-1.234567,-1.2345678,-1.23456789,-.1,-.01,-.001,-.0001,-.00001,-.000001,-.0000001,-.00000001,0,0.0,0.00,00.00,00.000,000.0000";
	float temp9[] = {-5.5f, -4.5f, -3.5f,-3000000000.0f,4294967000.0f,5.0E9f,-1.23456f,-1.234567f,-1.2345678f,-1.23456789f,-.1f,-.01f,-.001f,-.0001f,-.00001f,-.000001f,-.0000001f,-.00000001f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f };
  const size_t temp9_len = sizeof(temp9)/sizeof(temp9[0]);

	vector<float> output_vector9, alt_output_vector9;

	success = SpecUtils::split_to_floats( input_string9, output_vector9 );
  BOOST_CHECK_EQUAL( success, true );
	SpecUtils::split_to_floats(&(input_string9[0]),alt_output_vector9," ,\r\n\t",false);
  BOOST_CHECK_EQUAL( success, true );

  BOOST_CHECK_EQUAL( output_vector9.size(), temp9_len );
  BOOST_CHECK_EQUAL( alt_output_vector9.size(), temp9_len );

  for(int i=0; i < output_vector9.size();++i)
	{
		BOOST_CHECK_EQUAL(temp9[i],output_vector9[i]);
	}

	for (int i=0;i<alt_output_vector9.size();++i)
	{
		BOOST_CHECK_EQUAL(temp9[i],alt_output_vector9[i]);
	}

  string input_string10= "2.5\r3.5\r4.5\r\n5.5\t6.5 +123,+1.23,-123,-1.23,-100,-1000,-10000,-100000,-1000000,-10000000,-100000000,-1000000000,-0,-0.0,-0.00,-0.000,-0.0,,0,1             2, 1\n\n\n\n2,1\r\r\r\r2,1\t\t\t\t2";
	float temp10[] = {2.5f,3.5f,4.5f,5.5f,6.5f,123.0f,1.23f,-123.0f,-1.23f,-100.0f,-1000.0f,-10000.0f,-100000.0f,-1000000.0f,-10000000.0f,-100000000.0f,-1000000000.0f,-0.0f,-0.0f,-0.00f,-0.0f,0.0f,+0.0f,1.0f,2.0f,1.0f,2.0f,1.0f,2.0f,1.0f,2.0f};
  const size_t temp10_len = sizeof(temp10) / sizeof(temp10[0]);

	vector<float> output_vector10,alt_output_vector10;
	vector<float> comparison_vector10(temp10,temp10+31);
	success = SpecUtils::split_to_floats( input_string10, output_vector10 );
  BOOST_CHECK_EQUAL( success, true );

	success = SpecUtils::split_to_floats(&(input_string10[0]),alt_output_vector10," ,\r\n\t",false);
  BOOST_CHECK_EQUAL( success, true );

  BOOST_CHECK_EQUAL( output_vector10.size(), temp10_len);
  BOOST_CHECK_EQUAL( alt_output_vector10.size(), temp10_len);

	for( int i = 0; i < output_vector10.size(); ++i )
	{
		BOOST_CHECK_EQUAL(comparison_vector10[i],output_vector10[i]);
	}

	for (int i=0;i<alt_output_vector10.size();++i)
	{
		BOOST_CHECK_EQUAL(comparison_vector10[i],alt_output_vector10[i]);
	}
}


BOOST_AUTO_TEST_CASE( split_to_floats4  )
{
  string input_string11= "1 2 3";
	const float temp11[] = { 1.0f, 2.0f, 3.0f };
	vector<float> output_vector11, alt_output_vector11;
	vector<float> comparison_vector11(temp11,temp11+3);
	SpecUtils::split_to_floats(input_string11,output_vector11);

	SpecUtils::split_to_floats(&(input_string11[0]),alt_output_vector11," ,\r\n\t",false);

  for(int i = 0; i < output_vector11.size(); ++i )
	{
		BOOST_CHECK_EQUAL(comparison_vector11[i],output_vector11[i]);
	}

	for( int i = 0; i < alt_output_vector11.size(); ++i )
	{
		BOOST_CHECK_EQUAL(comparison_vector11[i],alt_output_vector11[i]);
	}
}


BOOST_AUTO_TEST_CASE( split_to_floats5  )
{
  string input_string12= "1200.25\n3556 22222222";
	float temp12[] = {1200.25,3556,22222222};
	vector<float> output_vector12,alt_output_vector12;
	vector<float> comparison_vector12(temp12,temp12+3);
	SpecUtils::split_to_floats(input_string12,output_vector12);
	SpecUtils::split_to_floats(input_string12.c_str(),
		input_string12.length(),alt_output_vector12);

	for(int i=0; i < output_vector12.size();++i)
	{
		BOOST_CHECK_EQUAL( comparison_vector12[i], output_vector12[i] );
	}

	for (int i=0;i<alt_output_vector12.size();++i)
	{
		BOOST_CHECK_EQUAL( comparison_vector12[i], alt_output_vector12[i] );
	}
}


BOOST_AUTO_TEST_CASE( split_to_floats6  )
{
	string input_string13= "1.2e3,4.5e0,4.55e1,-1.2e3,-4.5e0,-4.55e1";
	float temp13[] = {1200,4.5,45.5,-1200,-4.5,-45.5};
	vector<float> output_vector13,alt_output_vector13;
	vector<float> comparison_vector13(temp13,temp13+6);
	SpecUtils::split_to_floats(input_string13,output_vector13);
	SpecUtils::split_to_floats(&(input_string13[0]),alt_output_vector13," ,\r\n\t",false);

  for( int i = 0; i < output_vector13.size(); ++i)
	{
		BOOST_CHECK_EQUAL(comparison_vector13[i],output_vector13[i]);
	}

  for (int i=0;i<alt_output_vector13.size();++i)
	{
		BOOST_CHECK_EQUAL(comparison_vector13[i],alt_output_vector13[i]);
	}
}

/*
BOOST_AUTO_TEST_CASE( split_to_floats7  )
{
  string input_string14= "\"1.23\", 1";
	float temp14[] = {1.23f,1.0f};
	vector<float> output_vector14,alt_output_vector14;
	vector<float> comparison_vector14(temp14,temp14+2);
	SpecUtils::split_to_floats(input_string14,output_vector14);
	SpecUtils::split_to_floats(&(input_string14[0]),alt_output_vector14," ,\r\n\t",false);
	for( int i = 0; i < output_vector14.size(); ++i )
	{
		BOOST_CHECK_EQUAL(comparison_vector14[i],output_vector14[i]);
	}

	for( int i = 0; i < alt_output_vector14.size(); ++i )
	{
		BOOST_CHECK_EQUAL(comparison_vector14[i],alt_output_vector14[i]);
	}
}


BOOST_AUTO_TEST_CASE( split_to_floats8  )
{
    string input_string15= "'1.23', 1";
	float temp15[] = {(float)1.23,(float)1};
	vector<float> output_vector15,alt_output_vector15;
	vector<float> comparison_vector15(temp15,temp15+2);
	SpecUtils::split_to_floats(input_string15,output_vector15);
	SpecUtils::split_to_floats(&(input_string15[0]),alt_output_vector15," ,\r\n\t",false);
	for(int i=1; i < output_vector15.size();++i)
	{
		BOOST_CHECK_EQUAL(comparison_vector15[i],output_vector15[i]);

	}
	for (int i=0;i<alt_output_vector15.size();++i)
	{
		BOOST_CHECK_EQUAL(comparison_vector15[i],alt_output_vector15[i]);
	}
}
*/

BOOST_AUTO_TEST_CASE( split_to_ints  )
{
//start of testing split_to_ints
    //if the function encounters a decimal point "." or any letter "e" (for exponents) at any location in the string, the operation exits
    //testing if 1000e-2 works or not, need to check all forms of (num)e(exponent)
    //1e1 =10 does not work
    //(1e1)=10 does not work
    //4.0 =4 does not work
    //upper int limit for function is 2147483647
    //1+1 is seen the same as 1,+1
    //(1+1)=2 breaks the function
    //(1)=1 breaks the function
  string input_string16= "1,2 3  \t4\r5\n6,,,,,7,8,9,10";
 	int temp16[] = {1,2,3,4,5,6,7,8,9,10};
	vector<int> output_vector16;
	vector<int> comparison_vector16(temp16,temp16+10);
	SpecUtils::split_to_ints(&(input_string16[0]),input_string16.size(),output_vector16);
	for(int i=0; i < output_vector16.size();++i)
	{
		BOOST_CHECK_EQUAL(comparison_vector16[i],output_vector16[i]);

	}

	  string input_string17= "11 45 67,678,67,,1,123,400,450\t56\r45\n11,000006,2147483646,2147483647,11";
 	int temp17[] = {11,45,67,678,67,1,123,400,450,56,45,11,6,2147483646,2147483647,11};
	vector<int> output_vector17;
	vector<int> comparison_vector17(temp17,temp17+16);
	SpecUtils::split_to_ints(&(input_string17[0]),input_string17.size(),output_vector17);
	for( int i = 0; i < output_vector17.size(); ++i )
	{
		BOOST_CHECK_EQUAL(comparison_vector17[i],output_vector17[i]);
	}

  string input_string19= "1,+5,+0,-0,-1,-2,-300,0000000,1,1,11";
 	int temp19[] = {1,5,0,0,-1,-2,-300,0,1,1,11};
	vector<int> output_vector19;
	vector<int> comparison_vector19(temp19,temp19+11);
	SpecUtils::split_to_ints(&(input_string19[0]),input_string19.size(),output_vector19);

  for( int i = 1; i < output_vector19.size(); ++i )
	{
		BOOST_CHECK_EQUAL(comparison_vector19[i],output_vector19[i]);
	}

}

BOOST_AUTO_TEST_SUITE_END()
