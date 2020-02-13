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
#include <iostream>
#include <fstream>
#include <boost/algorithm/string.hpp>
#include <vector>
#include <cstdlib>
#include <stdlib.h>
#include <unistd.h>
#include <cstdlib>

//#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE testUtilityFilesystemFunctions
#include <boost/test/unit_test.hpp>
//#include <boost/test/included/unit_test.hpp>
#include "SpecUtils/UtilityFunctions.h"

using namespace std;
using namespace boost::unit_test;


BOOST_AUTO_TEST_CASE( testUtilityFilesystemFunctions ) {
//A few easy filesystem functions; assumes UNIX

//BOOST_CHECK(results.size() == 1);
BOOST_CHECK_EQUAL( UtilityFunctions::fs_relative( "/a/b/c/d", "/a/b/foo/bar"), "../../foo/bar" );
BOOST_CHECK_EQUAL( UtilityFunctions::fs_relative( "a", "a/b/c"), "b/c" );
BOOST_CHECK_EQUAL( UtilityFunctions::fs_relative( "a/b/c/x/y", "a/b/c"), "../.." );
BOOST_CHECK_EQUAL( UtilityFunctions::fs_relative( "output_dir", "output_dir/lessson_plan/File1.txt"), "lessson_plan/File1.txt" );

BOOST_CHECK_EQUAL( UtilityFunctions::filename( "/path/to/some/file.txt"), "file.txt" );
BOOST_CHECK_EQUAL( UtilityFunctions::filename( "/path/to/some"), "some" );
BOOST_CHECK_EQUAL( UtilityFunctions::filename( "/path/to/some/"), "." );

BOOST_CHECK_EQUAL( UtilityFunctions::parent_path( "/path/to/some/file.txt"), "/path/to/some" );
BOOST_CHECK_EQUAL( UtilityFunctions::parent_path( "/path/to/some/path"), "/path/to/some" );
BOOST_CHECK_EQUAL( UtilityFunctions::parent_path( "/path/to/some/path/"), "/path/to/some/path" );
BOOST_CHECK_EQUAL( UtilityFunctions::parent_path( "/path/to/some/path/.."), "/path/to" ); //

BOOST_CHECK_EQUAL( UtilityFunctions::file_extension( "/path/to/some/file.txt"), ".txt" );
BOOST_CHECK_EQUAL( UtilityFunctions::file_extension( "/path/to/filename"), "" );
BOOST_CHECK_EQUAL( UtilityFunctions::file_extension( ".profile"), ".profile" );

BOOST_CHECK_EQUAL( UtilityFunctions::append_path( "path", "file.txt"), "path/file.txt" );
BOOST_CHECK_EQUAL( UtilityFunctions::append_path( "path/", "file.txt"), "path/file.txt" );
BOOST_CHECK_EQUAL( UtilityFunctions::append_path( "path/", "/file.txt"), "path/file.txt" );
BOOST_CHECK_EQUAL( UtilityFunctions::append_path( "/path", "file.txt"), "/path/file.txt" );
BOOST_CHECK_EQUAL( UtilityFunctions::append_path( "path", "file" ), "path/file" );

}
