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
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdlib.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <cstdlib>
#include <filesystem>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/Filesystem.h"

using namespace std;


TEST_CASE( "testUtilityFilesystemFunctions" ) {
//A few easy filesystem functions; assumes UNIX
  const string hexs = "0123456789abcdef";
  
#ifdef _WIN32
  CHECK_EQ( SpecUtils::fs_relative( "\\a\\b\\c\\d", "\\a\\b\\foo\\bar"), "..\\..\\foo\\bar" );
  CHECK_EQ( SpecUtils::fs_relative( "a", "a\\b\\c"), "b\\c" );
  CHECK_EQ( SpecUtils::fs_relative( "a\\b\\\\c\\x\\y", "a/b/c"), "..\\.." );
  CHECK_EQ( SpecUtils::fs_relative( "output_dir", "output_dir/lessson_plan/File1.txt"), "lessson_plan\\File1.txt" );
  
  CHECK_EQ( SpecUtils::filename( "path\\to\\some\\file.txt"), "file.txt" );
  CHECK_EQ( SpecUtils::filename( "C:\\\\path\\to\\some"), "some" );
  CHECK_EQ( SpecUtils::filename( "C:\\\\path\\to\\some\\.."), ".." );
  CHECK_EQ( SpecUtils::filename( "C:\\\\path\\to\\some\\"), "some" );
  CHECK_EQ( SpecUtils::filename( "/path/to/some/file.txt"), "file.txt" );
  CHECK_EQ( SpecUtils::filename( "\\path\\to\\some\\file.txt"), "file.txt" );
  CHECK_EQ( SpecUtils::filename( "/path\\to/some"), "some" );
  CHECK_EQ( SpecUtils::filename( "/path/to\\some/"), "some" );
  CHECK_EQ( SpecUtils::filename( "/path/to/some\\.."), ".." );
  
  CHECK_EQ( SpecUtils::filename( "usr"), "usr" );
  CHECK_EQ( SpecUtils::filename( "\\\\"), "" );
  CHECK_EQ( SpecUtils::filename( "/"), "" );
  CHECK_EQ( SpecUtils::filename( "."), "." );
  CHECK_EQ( SpecUtils::filename( ".."), ".." );
  
  
  CHECK_EQ( SpecUtils::parent_path( "C:\\\\path\\to\\some\\file.txt"), "C:\\\\path\\to\\some" );
  CHECK_EQ( SpecUtils::parent_path( "C:\\\\path\\to\\some\\path"), "C:\\\\path\\to\\some" );
  CHECK_EQ( SpecUtils::parent_path( "C:\\\\path\\to\\some\\path\\"), "C:\\\\path\\to\\some" );
  CHECK_EQ( SpecUtils::parent_path( "C:\\\\path\\to\\some\\path\\.."), "C:\\\\path\\to" ); //
  
  CHECK_EQ( SpecUtils::parent_path( "C:\\\\" ), "C:" );
  CHECK_EQ( SpecUtils::parent_path( "." ), "" );
  CHECK_EQ( SpecUtils::parent_path( ".." ), "" );
  CHECK_EQ( SpecUtils::parent_path( "somefile" ), "" );
  CHECK_EQ( SpecUtils::parent_path( "C:\\\\somefile" ), "C:" );

  CHECK_EQ( SpecUtils::parent_path( R"str(/user/docs/Letter.txt)str" ), R"str(/user/docs)str" );
  CHECK_EQ( SpecUtils::parent_path( R"str(C:\Letter.txt)str" ), R"str(C:)str" );
  CHECK_EQ( SpecUtils::parent_path( R"str(\\Server01\user\docs\Letter.txt)str" ), R"str(\\Server01\user\docs)str" );
  CHECK_EQ( SpecUtils::parent_path( R"str(C:\user\docs\somefile.ext)str" ), R"str(C:\user\docs)str" );
  CHECK_EQ( SpecUtils::parent_path( R"str(./inthisdir)str" ), R"str(.)str" );
  CHECK_EQ( SpecUtils::parent_path( R"str(../../greatgrandparent)str" ), R"str(../..)str" );
  CHECK_EQ( SpecUtils::parent_path( R"str(\Program Files\Custom Utilities\StringFinder.exe)str" ), R"str(\Program Files\Custom Utilities)str" );
  CHECK_EQ( SpecUtils::parent_path( R"str(2018\January.xlsx)str" ), R"str(2018)str" );
  CHECK_EQ( SpecUtils::parent_path( R"str(C:\Projects\apilibrary\apilibrary.sln)str" ), R"str(C:\Projects\apilibrary)str" );
  CHECK_EQ( SpecUtils::parent_path( R"str(C:Projects\apilibrary\apilibrary.sln)str" ), R"str(C:Projects\apilibrary)str" );
  CHECK_EQ( SpecUtils::parent_path( R"str(\\system07\C$\)str" ), R"str(\\system07)str" );
  CHECK_EQ( SpecUtils::parent_path( R"str(\\Server2\Share\Test\Foo.txt)str" ), R"str(\\Server2\Share\Test)str" );
  CHECK_EQ( SpecUtils::parent_path( R"str(\\.\C:\Test\Foo.txt)str" ), R"str(\\.\C:\Test)str" );
  CHECK_EQ( SpecUtils::parent_path( R"str(\\?\C:\Test\Foo.txt)str" ), R"str(\\?\C:\Test)str" );
  CHECK_EQ( SpecUtils::parent_path( R"str(\\.\Volume{b75e2c83-0000-0000-0000-602f00000000}\Test\Foo.txt)str" ),
                                             R"str(\\.\Volume{b75e2c83-0000-0000-0000-602f00000000}\Test)str" );
  CHECK_EQ( SpecUtils::parent_path( R"str(\\?\Volume{b75e2c83-0000-0000-0000-602f00000000}\Test\Foo.txt)str" ),
                                             R"str(\\?\Volume{b75e2c83-0000-0000-0000-602f00000000}\Test)str" );
  
  
  CHECK_EQ( SpecUtils::file_extension( "C:\\\\path\\to\\some\\file.txt"), ".txt" );
  CHECK_EQ( SpecUtils::file_extension( "C:\\\\path\\to\\filename"), "" );
  CHECK_EQ( SpecUtils::file_extension( ".profile"), ".profile" );
  
  CHECK_EQ( SpecUtils::append_path( "path", "file.txt"), "path\\file.txt" );
  CHECK_EQ( SpecUtils::append_path( "path/", "file.txt"), "path\\file.txt" );
  CHECK_EQ( SpecUtils::append_path( "path\\", "/file.txt"), "path\\file.txt" );
  CHECK_EQ( SpecUtils::append_path( "/path", "file.txt"), "\\path\\file.txt" );
  CHECK_EQ( SpecUtils::append_path( "path", "file" ), "path\\file" );
  
  CHECK_EQ( SpecUtils::lexically_normalize_path(R"str(\\foo)str"), R"str(\\foo)str" );
  CHECK_EQ( SpecUtils::lexically_normalize_path(R"str(\\foo/bar)str"), R"str(\\foo\bar)str" );
  CHECK_EQ( SpecUtils::lexically_normalize_path(R"str(\\foo/bar/)str"), R"str(\\foo\bar\)str" );
  CHECK_EQ( SpecUtils::lexically_normalize_path(R"str(C:\foo\bar)str"), R"str(C:\foo\bar)str" );
  CHECK_EQ( SpecUtils::lexically_normalize_path(R"str(C:\foo\bar\..)str"), R"str(C:\foo)str" );
  
  // @TODO Read the following to actually understand Windows paths
  //       https://docs.microsoft.com/en-us/windows/win32/fileio/naming-a-file?redirectedfrom=MSDN
  
  
  //Some Example Paths From Wikipedia:
  // C:\user\docs\Letter.txt
  // /user/docs/Letter.txt
  // C:\Letter.txt
  // \\Server01\user\docs\Letter.txt
  // \\?\UNC\Server01\user\docs\Letter.txt
  // \\?\C:\user\docs\Letter.txt
  // C:\user\docs\somefile.ext
  // ./inthisdir
  //  ../../greatgrandparent
  
  //Some example paths from https://docs.microsoft.com/en-us/dotnet/standard/io/file-path-formats
  //  C:\Documents\Newsletters\Summer2018.pdf           //An absolute file path from the root of drive C:
  //  \Program Files\Custom Utilities\StringFinder.exe  //An absolute path from the root of the current drive.
  //  2018\January.xlsx                                 //A relative path to a file in a subdirectory of the current directory.
  //  ..\Publications\TravelBrochure.pdf                //A relative path to file in a directory that is a peer of the current directory.
  //  C:\Projects\apilibrary\apilibrary.sln             //An absolute path to a file from the root of drive C:
  //  C:Projects\apilibrary\apilibrary.sln              //A relative path from the current directory of the C: drive.
  //
  //  \\system07\C$\                                    //The root directory of the C: drive on system07.
  //  \\Server2\Share\Test\Foo.txt                      //The Foo.txt file in the Test directory of the \\Server2\Share volume.
  //  \\.\C:\Test\Foo.txt
  //  \\?\C:\Test\Foo.txt
  //  \\.\Volume{b75e2c83-0000-0000-0000-602f00000000}\Test\Foo.txt
  //  \\?\Volume{b75e2c83-0000-0000-0000-602f00000000}\Test\Foo.txt
  
  
  
  //UNC
  // R"str(\\host-name\share-name\file_path)str"
  // R"str(C:\Windows)str"
  //Extended
  // R"str(\\?\C:\Path\path\file.log)str"
  // R"str(\\?\C:\)str"
  // R"str(\\.\C:\)str"
  // R"str(\\?\)str"
  // R"str(\\?\UNC\server\share)str"
  
  
  
  CHECK_EQ( SpecUtils::fs_relative( "C:\\a\\b\\c\\d", "C:\\a\\b\\foo\\bar"), "..\\..\\foo\\bar" );
  CHECK_EQ( SpecUtils::fs_relative( "a", "a\\b\\\\c"), "b\\c" );
  CHECK_EQ( SpecUtils::fs_relative( "a\\", "a\\b\\\\c"), "b\\c" );
  CHECK_EQ( SpecUtils::fs_relative( "a\\b\\c\\x\\y", "a\\b\\c"), "..\\.." );
  CHECK_EQ( SpecUtils::fs_relative( "a\\b\\c\\\\x\\y", "a\\\\b\\c"), "..\\.." );
  CHECK_EQ( SpecUtils::fs_relative( "output_dir", "output_dir\\lessson_plan\\File1.txt"), "lessson_plan\\File1.txt" );
  CHECK_EQ( SpecUtils::fs_relative( "\\\\foo\\bar\\..\\daz", "\\\\foo\\daz"), "" );
  CHECK_EQ( SpecUtils::fs_relative( "\\\\foo\\bar\\..\\daz", "\\\\foo\\daz\\hello.txt"), "hello.txt" );
  CHECK_EQ( SpecUtils::fs_relative( "\\\\foo\\bar\\.\\\\\\..\\daz\\..\\daz\\dude", "\\\\foo\\daz\\hello.txt"), "..\\hello.txt" );
  
  CHECK_EQ( SpecUtils::parent_path( "C:\\path\\to\\some\\file.txt"), "C:\\path\\to\\some" );
  CHECK_EQ( SpecUtils::parent_path( "C:\\path\\to\\some\\path"), "C:\\path\\to\\some" );
  CHECK_EQ( SpecUtils::parent_path( "C:\\path\\to\\some\\path\\"), "C:\\path\\to\\some" );
  CHECK_EQ( SpecUtils::parent_path( "C:\\path\\to\\some\\path\\.."), "C:\\path\\to" ); //
  CHECK_EQ( SpecUtils::parent_path( "C:\\path\\to\\some\\path\\..\\..\\"), "C:\\path" ); //
  CHECK_EQ( SpecUtils::parent_path( "C:\\path\\to\\some\\..\\path" ), "C:\\path\\to\\some\\.." ); //
  CHECK_EQ( SpecUtils::parent_path( "C:\\path\\to\\some\\..\\..\\..\\" ), "" );
  CHECK_EQ( SpecUtils::parent_path( "C:\\path\\to\\some\\..\\..\\..\\..\\" ), "" );
  CHECK_EQ( SpecUtils::parent_path( "//" ), "" );
  CHECK_EQ( SpecUtils::parent_path( "\\\\" ), "" );
  CHECK_EQ( SpecUtils::parent_path( "C:" ), "" );
  CHECK_EQ( SpecUtils::parent_path( "C:\\" ), "C:" );
  CHECK_EQ( SpecUtils::parent_path( "." ), "" );
  CHECK_EQ( SpecUtils::parent_path( ".." ), "" );
  CHECK_EQ( SpecUtils::parent_path( "somefile" ), "" );
  CHECK_EQ( SpecUtils::parent_path( ".\\somefile" ), "." );
  CHECK_EQ( SpecUtils::parent_path( "/somefile" ), "" );
  CHECK_EQ( SpecUtils::parent_path( "\\somefile" ), "" );
  

  
  
  CHECK_EQ( SpecUtils::file_extension( "/path/to/some/file.txt"), ".txt" );
  CHECK_EQ( SpecUtils::file_extension( "\\path\\to\\some\\file.txt"), ".txt" );
  CHECK_EQ( SpecUtils::file_extension( "/path/to/filename"), "" );
  CHECK_EQ( SpecUtils::file_extension( ".profile"), ".profile" );
  
  CHECK_EQ( SpecUtils::append_path( "path", "file.txt"), "path\\file.txt" );
  CHECK_EQ( SpecUtils::append_path( "path/", "file.txt"), "path\\file.txt" );
  CHECK_EQ( SpecUtils::append_path( "path/", "/file.txt"), "path\\file.txt" );
  CHECK_EQ( SpecUtils::append_path( "/path", "file.txt"), "\\path\\file.txt" );
  CHECK_EQ( SpecUtils::append_path( "path", "file" ), "path\\file" );

  
  CHECK( !SpecUtils::is_absolute_path( "." ) );
  CHECK( !SpecUtils::is_absolute_path( "./someFile" ) );
  CHECK( SpecUtils::is_absolute_path( "\\\\" ) );
  CHECK( SpecUtils::is_absolute_path( "C:\\" ) );
#else //#ifdef _WIN32
  CHECK_EQ( SpecUtils::lexically_normalize_path("foo/./bar/.."), "foo" );
  CHECK_EQ( SpecUtils::lexically_normalize_path("foo/.///bar/../"), "foo/" );
  CHECK_EQ( SpecUtils::lexically_normalize_path("foo/bar/../../../dude"), "../dude" );
  CHECK_EQ( SpecUtils::lexically_normalize_path("../"), "../" );
  CHECK_EQ( SpecUtils::lexically_normalize_path(".."), ".." );
  CHECK_EQ( SpecUtils::lexically_normalize_path("foo/bar/.."), "foo" );
  CHECK_EQ( SpecUtils::lexically_normalize_path("foo/bar/../"), "foo/" );
  CHECK_EQ( SpecUtils::lexically_normalize_path("/foo/bar/"), "/foo/bar/" );
  CHECK_EQ( SpecUtils::lexically_normalize_path("/foo/bar"), "/foo/bar" );
  CHECK_EQ( SpecUtils::lexically_normalize_path("/foo///bar"), "/foo/bar" );
  CHECK_EQ( SpecUtils::lexically_normalize_path("/"), "/" );
  CHECK_EQ( SpecUtils::lexically_normalize_path("//"), "/" );
  CHECK_EQ( SpecUtils::lexically_normalize_path("/.."), "/" );
  CHECK_EQ( SpecUtils::lexically_normalize_path("/../.."), "/" );
  CHECK_EQ( SpecUtils::lexically_normalize_path("/foo/../../.."), "/" );
  CHECK_EQ( SpecUtils::lexically_normalize_path(""), "" );
  CHECK_EQ( SpecUtils::lexically_normalize_path("."), "" );
  CHECK_EQ( SpecUtils::lexically_normalize_path("/."), "/" );
  CHECK_EQ( SpecUtils::lexically_normalize_path("/foo/../."), "/" );
  CHECK_EQ( SpecUtils::lexically_normalize_path("foo"), "foo" );
  CHECK_EQ( SpecUtils::lexically_normalize_path("./foo/bar"), "foo/bar" );
  CHECK_EQ( SpecUtils::lexically_normalize_path("./foo/bar/.."), "foo" );
  CHECK_EQ( SpecUtils::lexically_normalize_path("./foo/bar/."), "foo/bar" );
  
  CHECK_EQ( SpecUtils::fs_relative( "/a/b/c/d", "/a/b/foo/bar"), "../../foo/bar" );
  CHECK_EQ( SpecUtils::fs_relative( "a", "a/b//c"), "b/c" );
  CHECK_EQ( SpecUtils::fs_relative( "a/", "a/b//c"), "b/c" );
  CHECK_EQ( SpecUtils::fs_relative( "a/b/c/x/y", "a/b/c"), "../.." );
  CHECK_EQ( SpecUtils::fs_relative( "a/b/c//x/y", "a//b/c"), "../.." );
  CHECK_EQ( SpecUtils::fs_relative( "output_dir", "output_dir/lessson_plan/File1.txt"), "lessson_plan/File1.txt" );
  CHECK_EQ( SpecUtils::fs_relative( "/foo/bar/../daz", "/foo/daz"), "" );
  CHECK_EQ( SpecUtils::fs_relative( "/foo/bar/../daz", "/foo/daz/hello.txt"), "hello.txt" );
  CHECK_EQ( SpecUtils::fs_relative( "/foo/bar/.///../daz/../daz/dude", "/foo/daz/hello.txt"), "../hello.txt" );
  
  CHECK_EQ( SpecUtils::parent_path( "/path/to/some/file.txt"), "/path/to/some" );
  CHECK_EQ( SpecUtils::parent_path( "/path/to/some/path"), "/path/to/some" );
  CHECK_EQ( SpecUtils::parent_path( "/path/to/some/path/"), "/path/to/some" );
  CHECK_EQ( SpecUtils::parent_path( "/path/to/some/path/.."), "/path/to" ); //
  CHECK_EQ( SpecUtils::parent_path( "/path/to/some/path/../../"), "/path" ); //
  CHECK_EQ( SpecUtils::parent_path( "/path/to/some/../path" ), "/path/to/some/.." ); //
  CHECK_EQ( SpecUtils::parent_path( "/path/to/some/../../../" ), "/" );
  CHECK_EQ( SpecUtils::parent_path( "/path/to/some/../../../../" ), "/" );
  CHECK_EQ( SpecUtils::parent_path( "/" ), "" );
  CHECK_EQ( SpecUtils::parent_path( "." ), "" );
  CHECK_EQ( SpecUtils::parent_path( ".." ), "" );
  CHECK_EQ( SpecUtils::parent_path( "somefile" ), "" );
  CHECK_EQ( SpecUtils::parent_path( "./somefile" ), "." );
  CHECK_EQ( SpecUtils::parent_path( "/somefile" ), "/" );
  
  CHECK_EQ( SpecUtils::parent_path( "/path/to/some/../../../../" ), "/" );
  
  CHECK_EQ( SpecUtils::filename( "/path/to/some/file.txt"), "file.txt" );
  CHECK_EQ( SpecUtils::filename( "/path/to/some"), "some" );
  CHECK_EQ( SpecUtils::filename( "/path/to/some/"), "" );
  CHECK_EQ( SpecUtils::filename( "/path/to/some/.."), "" );
  
  CHECK_EQ( SpecUtils::filename( "usr"), "usr" );
  CHECK_EQ( SpecUtils::filename( "/"), "" );
  CHECK_EQ( SpecUtils::filename( "."), "" );
  CHECK_EQ( SpecUtils::filename( ".."), "" );
  
  
  CHECK_EQ( SpecUtils::file_extension( "/path/to/some/file.txt"), ".txt" );
  CHECK_EQ( SpecUtils::file_extension( "/path/to/filename"), "" );
  CHECK_EQ( SpecUtils::file_extension( ".profile"), ".profile" );
  
  CHECK_EQ( SpecUtils::append_path( "path", "file.txt"), "path/file.txt" );
  CHECK_EQ( SpecUtils::append_path( "path/", "file.txt"), "path/file.txt" );
  CHECK_EQ( SpecUtils::append_path( "path/", "/file.txt"), "path/file.txt" );
  CHECK_EQ( SpecUtils::append_path( "/path", "file.txt"), "/path/file.txt" );
  CHECK_EQ( SpecUtils::append_path( "path", "file" ), "path/file" );

  
  CHECK( !SpecUtils::is_absolute_path( "." ) );
  CHECK( !SpecUtils::is_absolute_path( "./someFile" ) );
  CHECK( SpecUtils::is_absolute_path( "/" ) );
#endif
  
  
  CHECK( SpecUtils::is_absolute_path( SpecUtils::temp_dir() ) );
  CHECK( SpecUtils::is_absolute_path( SpecUtils::get_working_path() ) );
  
  cout << "SpecUtils::temp_dir()=" << SpecUtils::temp_dir() << endl;
  cout << "SpecUtils::get_working_path()=" << SpecUtils::get_working_path() << endl;
  
  
  
  const string tmpdir = SpecUtils::temp_dir();
  REQUIRE( !tmpdir.empty() );
  REQUIRE( SpecUtils::is_directory(tmpdir) );
  CHECK( !SpecUtils::is_file(tmpdir) );
  
  
  const string testname1 = SpecUtils::temp_file_name( "myuniquename", SpecUtils::temp_dir() );
  CHECK( SpecUtils::contains( testname1, "myuniquename") );
  CHECK( testname1.size() > (tmpdir.size() + 12 + 8) );
  CHECK( !SpecUtils::is_directory(testname1) );
  CHECK( !SpecUtils::is_file(testname1) );
  
  const string testname2 = SpecUtils::temp_file_name( "myuniquename-%%%%%%%%%%", SpecUtils::temp_dir() );
  CHECK( SpecUtils::contains( testname2, "myuniquename") );;
  CHECK( !SpecUtils::is_directory(testname2) );
  CHECK( !SpecUtils::is_file(testname2) );
  const string testname2_ending = testname2.substr( testname2.length() - 11, 11 );
  CHECK( testname2_ending[0] == '-' );
  
  
  for( size_t i = 1; i < testname2_ending.size(); ++i )
  {
    CHECK( hexs.find(testname2_ending[i]) != string::npos );
  }
  
  CHECK( !SpecUtils::can_rw_in_directory(testname2) );
  REQUIRE( SpecUtils::create_directory(testname2) == 1 );
  CHECK( SpecUtils::is_directory(testname2) );
  CHECK( !SpecUtils::is_file(testname2) );
  
  //cout << "Created directory '" << testname2 << "'" << endl;
  
  CHECK( SpecUtils::can_rw_in_directory(testname2) );
  
  
  //std::filesystem::status(wtestname2).
  
#ifdef _WIN32
  const auto wtestname2 = SpecUtils::convert_from_utf8_to_utf16(testname2);
  std::filesystem::permissions( wtestname2, std::filesystem::perms::all, std::filesystem::perm_options::remove );
  std::filesystem::permissions( wtestname2, std::filesystem::perms::owner_all, std::filesystem::perm_options::add );
  std::filesystem::permissions( wtestname2, std::filesystem::perms::owner_write, std::filesystem::perm_options::remove );
  CHECK( !SpecUtils::can_rw_in_directory(testname2) );
  std::filesystem::permissions( wtestname2, std::filesystem::perms::owner_write, std::filesystem::perm_options::add );
  CHECK( SpecUtils::can_rw_in_directory(testname2) );
  
  //On windows
  //std::filesystem::permissions( wtestname2, std::filesystem::perms::owner_read, std::filesystem::perm_options::remove );
  //CHECK( !SpecUtils::can_rw_in_directory(testname2) );
  
  std::filesystem::permissions( wtestname2, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perm_options::add );
#else
  std::filesystem::permissions( testname2, std::filesystem::perms::all, std::filesystem::perm_options::remove );
  std::filesystem::permissions( testname2, std::filesystem::perms::owner_all, std::filesystem::perm_options::add );

#ifndef  __linux__
  // TODO: For some reason, running in Docker (so as root), the removing of permissions doesnt necessarily work
  std::filesystem::permissions( testname2, std::filesystem::perms::owner_write, std::filesystem::perm_options::remove );
  CHECK( !SpecUtils::can_rw_in_directory(testname2) );
#endif
  
  std::filesystem::permissions( testname2, std::filesystem::perms::owner_write, std::filesystem::perm_options::add );
  CHECK( SpecUtils::can_rw_in_directory(testname2) );
  
#ifndef  __linux__
  // Similar Linux issue
  std::filesystem::permissions( testname2, std::filesystem::perms::owner_read, std::filesystem::perm_options::remove );
  CHECK( !SpecUtils::can_rw_in_directory(testname2) );
#endif
  
  std::filesystem::permissions( testname2, std::filesystem::perms::owner_read, std::filesystem::perm_options::add );
#endif

  
  vector<string> added_dirs, added_files, toplevel_dirs;
  for( size_t subdirnum = 0; subdirnum < 25; ++subdirnum )
  {
    int depth = 0;
    string currentdir = testname2;
    
    while( (rand() % 2) == 1 )
    {
      ++depth;
      currentdir = SpecUtils::temp_file_name( "subdir-" + std::to_string(depth) + "-%%%%%%%%%%", currentdir );
      CHECK( !SpecUtils::is_directory(currentdir) );
      REQUIRE( SpecUtils::create_directory(currentdir) == 1 );
      CHECK( SpecUtils::is_directory(currentdir) );
      added_dirs.push_back( currentdir );
      if( depth == 1 )
        toplevel_dirs.push_back( currentdir );
      
      const int nfilescreate = (rand() % 100);
      vector<string> filesinthisdir;
      for( int filenum = 0; filenum < nfilescreate; ++filenum )
      {
        string fname = SpecUtils::append_path( currentdir, "file_" + std::to_string(filenum) + ".txt" );
        CHECK( !SpecUtils::is_file(fname) );
        const int nbytes = (rand() % (1024*512));
        vector<char> writtenbytes;
        
        {//Begin writing to file
          #ifdef _WIN32
            const std::wstring wfname = SpecUtils::convert_from_utf8_to_utf16(fname);
            ofstream outputfile( wfname.c_str(), ios::out | ios::binary );
          #else
            ofstream outputfile( fname.c_str(), ios::out | ios::binary );
          #endif

          REQUIRE( outputfile.is_open() );
          for( int i = 0 ; i < nbytes; ++i )
          {
            const char byte = rand();
            outputfile.put( byte );
            writtenbytes.push_back( byte );
          }
        }//End writing to file
        
        
        CHECK( !SpecUtils::can_rw_in_directory(fname) );
        
        //CHECK( SpecUtils::make_canonical_path( f ) );
        string forigname = SpecUtils::filename(fname);
        string fparent = SpecUtils::parent_path(fname);
        string fparentname = SpecUtils::filename(fparent);
        
        string fgparent = SpecUtils::parent_path(fparent);
        string fgparentname = SpecUtils::filename(fgparent);
        
        string fggparent = SpecUtils::parent_path(fgparent);
        string fggparentname = SpecUtils::filename(fggparent);
        
        string onedown = SpecUtils::append_path( fparent, ".." );
        string twodown = SpecUtils::append_path( onedown, ".." );
        string threedown = SpecUtils::append_path( twodown, ".." );
        
        string onedotequiv = SpecUtils::append_path( SpecUtils::append_path(onedown, fparentname ), forigname );
        
        string twodotequiv = SpecUtils::append_path(
                               SpecUtils::append_path(
                                 SpecUtils::append_path(twodown, fgparentname),
                               fparentname),
                             forigname );
        
        string threedotequiv = SpecUtils::append_path(
                                 SpecUtils::append_path(
                                   SpecUtils::append_path(
                                     SpecUtils::append_path(threedown, fggparentname),
                                   fgparentname),
                                 fparentname ),
                               forigname );
        
        string fname_canonical = fname;
        CHECK( SpecUtils::make_canonical_path(fname_canonical) );
        string fname_canonical_check = fname_canonical;
        CHECK( SpecUtils::make_canonical_path(fname_canonical_check) );
        CHECK_EQ( fname_canonical_check, fname_canonical );
        
        CHECK_MESSAGE( SpecUtils::make_canonical_path(onedotequiv), "Failed to canoniclize " << onedotequiv );
        CHECK_MESSAGE( SpecUtils::make_canonical_path(twodotequiv), "Failed to canoniclize " << twodotequiv );
        CHECK_MESSAGE( SpecUtils::make_canonical_path(threedotequiv), "Failed to canoniclize " << threedotequiv );
        
        CHECK_MESSAGE( threedotequiv.find("..") == string::npos, "threedotequiv='" << threedotequiv << "'" );
        
        std::vector<char> read_bytes;
        SpecUtils::load_file_data( fname.c_str(), read_bytes );
        if( !read_bytes.empty() )
          read_bytes.resize(read_bytes.size()-1);  //Get rid of '\0' that was inserted
        
        CHECK_EQ( writtenbytes.size(), read_bytes.size() );
        CHECK( writtenbytes == read_bytes );
        
        CHECK( SpecUtils::is_file(fname) );
        //CHECK_EQ( SpecUtils::file_size(fname), nbytes );
        
#ifdef _WIN32
        CHECK_EQ( SpecUtils::file_size(fname),
                 std::filesystem::file_size(SpecUtils::convert_from_utf8_to_utf16(fname) ) );
#else
        CHECK_EQ( SpecUtils::file_size(fname), std::filesystem::file_size(fname) );
#endif
  
        filesinthisdir.push_back( fname );
        added_files.push_back( fname );
      }//for( int filenum = 0; filenum < nfilescreate; ++filenum )
      
      const vector<string> lsfiles = SpecUtils::ls_files_in_directory( currentdir, "" );
      CHECK_EQ( lsfiles.size(), filesinthisdir.size() );
      for( string createdfile : filesinthisdir )
      {
        bool found_file = false;
        CHECK( SpecUtils::make_canonical_path( createdfile ) );
                    
        for( string f : lsfiles )
        {
          CHECK( SpecUtils::make_canonical_path( f ) );
          found_file = (f == createdfile);
          //cout << "createdfile='" << createdfile << "', f='" << f << "'" << endl;
          if( found_file )
            break;
        }
        
        CHECK_MESSAGE( found_file, "Failed on " << createdfile );
      }//for( string createdfile : filesinthisdir )
    }//while( (rand() % 2) == 1 )
  }//for( size_t subdirnum = 0; subdirnum < 100; ++subdirnum )
  
  vector<string> toplevel_ls_dirs = SpecUtils::ls_directories_in_directory( testname2 );
  for( string &s : toplevel_dirs )
    SpecUtils::make_canonical_path(s);
  for( string &s : toplevel_ls_dirs )
    SpecUtils::make_canonical_path(s);
  
  std::sort( begin(toplevel_dirs), end(toplevel_dirs) );
  std::sort( begin(toplevel_ls_dirs), end(toplevel_ls_dirs) );
  
  
  CHECK_MESSAGE( toplevel_dirs == toplevel_ls_dirs, "Expected " << toplevel_dirs.size() << " dirs, and got " << toplevel_ls_dirs.size() );
  
  
  vector<string> rls = SpecUtils::recursive_ls(testname2);
  vector<string> rlstxt = SpecUtils::recursive_ls(testname2,".txt");
  vector<string> rlsnone = SpecUtils::recursive_ls(testname2,".a");
  
  std::sort( begin(added_files), end(added_files) );
  std::sort( begin(rls), end(rls) );
  std::sort( begin(rlstxt), end(rlstxt) );
  std::sort( begin(rlsnone), end(rlsnone) );
  
  CHECK_EQ(rls.size(), added_files.size());
  CHECK( rls == rlstxt );
  CHECK( added_files == rls );
  CHECK( rlsnone.empty() );
  
  for( const string f : added_files )
  {
    CHECK( SpecUtils::is_file(f) );
    CHECK( !SpecUtils::is_directory(f) );
    
    vector<char> old_file_data, new_file_data;
    SpecUtils::load_file_data( f.c_str(), old_file_data );
    
    const string newname = f + "renamed.t";
    CHECK( SpecUtils::rename_file( f, newname ) );
    CHECK( !SpecUtils::is_file(f) );
    CHECK( SpecUtils::is_file(newname) );
    
    SpecUtils::load_file_data( newname.c_str(), new_file_data );
    CHECK( old_file_data == new_file_data );
    
    CHECK( SpecUtils::remove_file(newname) );
    CHECK( !SpecUtils::is_file(newname) );
  }//
  
  
#ifdef _WIN32
  std::filesystem::remove_all(wtestname2);
#else
  std::filesystem::remove_all(testname2);
#endif
}
