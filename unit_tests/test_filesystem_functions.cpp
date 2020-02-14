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
#include <boost/filesystem.hpp>
//#include <boost/test/included/unit_test.hpp>
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/Filesystem.h"

using namespace std;
using namespace boost::unit_test;


BOOST_AUTO_TEST_CASE( testUtilityFilesystemFunctions ) {
//A few easy filesystem functions; assumes UNIX
  const string hexs = "0123456789abcdef";
  
#ifdef _WIN32
  BOOST_CHECK_EQUAL( SpecUtils::fs_relative( "\\a\\b\\c\\d", "\\a\\b\\foo\\bar"), "..\\..\\foo\\bar" );
  BOOST_CHECK_EQUAL( SpecUtils::fs_relative( "a", "a\\b\\c"), "b\\c" );
  BOOST_CHECK_EQUAL( SpecUtils::fs_relative( "a\\b\\\\c\\x\\y", "a/b/c"), "../.." );
  BOOST_CHECK_EQUAL( SpecUtils::fs_relative( "output_dir", "output_dir/lessson_plan/File1.txt"), "lessson_plan\\File1.txt" );
  
  BOOST_CHECK_EQUAL( SpecUtils::filename( "path\\to\\some\\file.txt"), "file.txt" );
  BOOST_CHECK_EQUAL( SpecUtils::filename( "C:\\\\path\\to\\some"), "some" );
  BOOST_CHECK_EQUAL( SpecUtils::filename( "C:\\\\path\\to\\some\\"), "." );
  
  BOOST_CHECK_EQUAL( SpecUtils::parent_path( "C:\\\\path\\to\\some\\file.txt"), "C:\\\\path\\to\\some" );
  BOOST_CHECK_EQUAL( SpecUtils::parent_path( "C:\\\\path\\to\\some\\path"), "C:\\\\path\\to\\some" );
  BOOST_CHECK_EQUAL( SpecUtils::parent_path( "C:\\\\path\\to\\some\\path\\"), "C:\\\\path\\to\\some\\path" );
  BOOST_CHECK_EQUAL( SpecUtils::parent_path( "C:\\\\path\\to\\some\\path\\.."), "C:\\\\path\\to" ); //
  
  BOOST_CHECK_EQUAL( SpecUtils::parent_path( "C:\\\\" ), "C:\\\\" );
  BOOST_CHECK_EQUAL( SpecUtils::parent_path( "." ), "." );
  BOOST_CHECK_EQUAL( SpecUtils::parent_path( ".." ), "." );
  BOOST_CHECK_EQUAL( SpecUtils::parent_path( "somefile" ), "." );
  BOOST_CHECK_EQUAL( SpecUtils::parent_path( "C:\\\\somefile" ), "C:\\\\" );
  
  BOOST_CHECK_EQUAL( SpecUtils::file_extension( "C:\\\\path\\to\\some\\file.txt"), ".txt" );
  BOOST_CHECK_EQUAL( SpecUtils::file_extension( "C:\\\\path\\to\\filename"), "" );
  BOOST_CHECK_EQUAL( SpecUtils::file_extension( ".profile"), ".profile" );
  
  BOOST_CHECK_EQUAL( SpecUtils::append_path( "path", "file.txt"), "path/file.txt" );
  BOOST_CHECK_EQUAL( SpecUtils::append_path( "path/", "file.txt"), "path\\file.txt" );
  BOOST_CHECK_EQUAL( SpecUtils::append_path( "path\\", "/file.txt"), "path\\file.txt" );
  BOOST_CHECK_EQUAL( SpecUtils::append_path( "/path", "file.txt"), "\\path\\file.txt" );
  BOOST_CHECK_EQUAL( SpecUtils::append_path( "path", "file" ), "path/file" );
#endif //#ifdef _WIN32
  
//BOOST_CHECK(results.size() == 1);
  BOOST_CHECK_EQUAL( SpecUtils::fs_relative( "/a/b/c/d", "/a/b/foo/bar"), "../../foo/bar" );
  BOOST_CHECK_EQUAL( SpecUtils::fs_relative( "a", "a/b//c"), "b/c" );
  BOOST_CHECK_EQUAL( SpecUtils::fs_relative( "a/", "a/b//c"), "b/c" );
  BOOST_CHECK_EQUAL( SpecUtils::fs_relative( "a/b/c/x/y", "a/b/c"), "../.." );
  BOOST_CHECK_EQUAL( SpecUtils::fs_relative( "a/b/c//x/y", "a//b/c"), "../.." );
  BOOST_CHECK_EQUAL( SpecUtils::fs_relative( "output_dir", "output_dir/lessson_plan/File1.txt"), "lessson_plan/File1.txt" );
  
  BOOST_CHECK_EQUAL( SpecUtils::filename( "/path/to/some/file.txt"), "file.txt" );
  BOOST_CHECK_EQUAL( SpecUtils::filename( "/path/to/some"), "some" );
  BOOST_CHECK_EQUAL( SpecUtils::filename( "/path/to/some/"), "some" );
  BOOST_CHECK_EQUAL( SpecUtils::filename( "/path/to/some/.."), ".." );
  
  BOOST_CHECK_EQUAL( SpecUtils::filename( "usr"), "usr" );
  BOOST_CHECK_EQUAL( SpecUtils::filename( "/"), "/" );
  BOOST_CHECK_EQUAL( SpecUtils::filename( "."), "." );
  BOOST_CHECK_EQUAL( SpecUtils::filename( ".."), ".." );
  
  
  BOOST_CHECK_EQUAL( SpecUtils::parent_path( "/path/to/some/file.txt"), "/path/to/some" );
  BOOST_CHECK_EQUAL( SpecUtils::parent_path( "/path/to/some/path"), "/path/to/some" );
  BOOST_CHECK_EQUAL( SpecUtils::parent_path( "/path/to/some/path/"), "/path/to/some" );
  BOOST_CHECK_EQUAL( SpecUtils::parent_path( "/path/to/some/path/.."), "/path/to" ); //
  BOOST_CHECK_EQUAL( SpecUtils::parent_path( "/path/to/some/path/../../"), "/path" ); //
  BOOST_CHECK_EQUAL( SpecUtils::parent_path( "/path/to/some/../path" ), "/path/to/some/.." ); //
  BOOST_CHECK_EQUAL( SpecUtils::parent_path( "/path/to/some/../../../" ), "/" );
  BOOST_CHECK_EQUAL( SpecUtils::parent_path( "/path/to/some/../../../../" ), "/" );
  
  BOOST_CHECK_EQUAL( SpecUtils::parent_path( "/" ), "/" );
  BOOST_CHECK_EQUAL( SpecUtils::parent_path( "." ), "." );
  BOOST_CHECK_EQUAL( SpecUtils::parent_path( ".." ), "." );
  BOOST_CHECK_EQUAL( SpecUtils::parent_path( "somefile" ), "." );
  BOOST_CHECK_EQUAL( SpecUtils::parent_path( "/somefile" ), "/" );
  
  
  BOOST_CHECK_EQUAL( SpecUtils::file_extension( "/path/to/some/file.txt"), ".txt" );
  BOOST_CHECK_EQUAL( SpecUtils::file_extension( "/path/to/filename"), "" );
  BOOST_CHECK_EQUAL( SpecUtils::file_extension( ".profile"), ".profile" );
  
  BOOST_CHECK_EQUAL( SpecUtils::append_path( "path", "file.txt"), "path/file.txt" );
  BOOST_CHECK_EQUAL( SpecUtils::append_path( "path/", "file.txt"), "path/file.txt" );
  BOOST_CHECK_EQUAL( SpecUtils::append_path( "path/", "/file.txt"), "path/file.txt" );
  BOOST_CHECK_EQUAL( SpecUtils::append_path( "/path", "file.txt"), "/path/file.txt" );
  BOOST_CHECK_EQUAL( SpecUtils::append_path( "path", "file" ), "path/file" );

  
  BOOST_CHECK( !SpecUtils::is_absolute_path( "." ) );
  BOOST_CHECK( !SpecUtils::is_absolute_path( "./someFile" ) );
  BOOST_CHECK( SpecUtils::is_absolute_path( "/" ) );
  BOOST_CHECK( SpecUtils::is_absolute_path( SpecUtils::temp_dir() ) );
  BOOST_CHECK( SpecUtils::is_absolute_path( SpecUtils::get_working_path() ) );
  
  
  
  
  const string tmpdir = SpecUtils::temp_dir();
  BOOST_REQUIRE( !tmpdir.empty() );
  BOOST_REQUIRE( SpecUtils::is_directory(tmpdir) );
  
  const string testname1 = SpecUtils::temp_file_name( "myuniquename", SpecUtils::temp_dir() );
  BOOST_CHECK( SpecUtils::contains( testname1, "myuniquename") );
  BOOST_CHECK( testname1.size() > (tmpdir.size() + 12 + 8) );
  BOOST_CHECK( !SpecUtils::is_directory(testname1) );
  BOOST_CHECK( !SpecUtils::is_file(testname1) );
  
  const string testname2 = SpecUtils::temp_file_name( "myuniquename-%%%%%%%%%%", SpecUtils::temp_dir() );
  BOOST_CHECK( SpecUtils::contains( testname2, "myuniquename") );;
  BOOST_CHECK( !SpecUtils::is_directory(testname2) );
  BOOST_CHECK( !SpecUtils::is_file(testname2) );
  const string testname2_ending = testname2.substr( testname2.length() - 11, 11 );
  BOOST_CHECK( testname2_ending[0] == '-' );
  
  
  for( size_t i = 1; i < testname2_ending.size(); ++i )
  {
    BOOST_CHECK( hexs.find(testname2_ending[i]) != string::npos );
  }
  
  BOOST_REQUIRE( SpecUtils::create_directory(testname2) == 1 );
  BOOST_CHECK( SpecUtils::is_directory(testname2) );
  
  //cout << "Created directory '" << testname2 << "'" << endl;
  
  BOOST_CHECK( SpecUtils::can_rw_in_directory(testname2) );
  
#ifdef _WIN32
  const auto wtestname2 = SpecUtils::convert_from_utf8_to_utf16(testname2);
  boost::filesystem::permissions( wtestname2, boost::filesystem::perms::remove_perms | boost::filesystem::perms::all_all );
  boost::filesystem::permissions( wtestname2, boost::filesystem::perms::add_perms | boost::filesystem::perms::owner_all );
  boost::filesystem::permissions( wtestname2, boost::filesystem::perms::remove_perms | boost::filesystem::perms::owner_write );
  BOOST_CHECK( !SpecUtils::can_rw_in_directory(testname2) );
  boost::filesystem::permissions( wtestname2, boost::filesystem::perms::add_perms | boost::filesystem::perms::owner_write );
  BOOST_CHECK( SpecUtils::can_rw_in_directory(testname2) );
  boost::filesystem::permissions( wtestname2, boost::filesystem::perms::remove_perms | boost::filesystem::perms::owner_read );
  BOOST_CHECK( !SpecUtils::can_rw_in_directory(testname2) );
  boost::filesystem::permissions( wtestname2, boost::filesystem::perms::add_perms | boost::filesystem::perms::owner_read );
#else
  boost::filesystem::permissions( testname2, boost::filesystem::perms::remove_perms | boost::filesystem::perms::all_all );
  boost::filesystem::permissions( testname2, boost::filesystem::perms::add_perms | boost::filesystem::perms::owner_all );
  boost::filesystem::permissions( testname2, boost::filesystem::perms::remove_perms | boost::filesystem::perms::owner_write );
  BOOST_CHECK( !SpecUtils::can_rw_in_directory(testname2) );
  boost::filesystem::permissions( testname2, boost::filesystem::perms::add_perms | boost::filesystem::perms::owner_write );
  BOOST_CHECK( SpecUtils::can_rw_in_directory(testname2) );
  boost::filesystem::permissions( testname2, boost::filesystem::perms::remove_perms | boost::filesystem::perms::owner_read );
  BOOST_CHECK( !SpecUtils::can_rw_in_directory(testname2) );
  boost::filesystem::permissions( testname2, boost::filesystem::perms::add_perms | boost::filesystem::perms::owner_read );
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
      BOOST_CHECK( !SpecUtils::is_directory(currentdir) );
      BOOST_REQUIRE( SpecUtils::create_directory(currentdir) == 1 );
      BOOST_CHECK( SpecUtils::is_directory(currentdir) );
      added_dirs.push_back( currentdir );
      if( depth == 1 )
        toplevel_dirs.push_back( currentdir );
      
      const int nfilescreate = (rand() % 100);
      vector<string> filesinthisdir;
      for( int filenum = 0; filenum < nfilescreate; ++filenum )
      {
        string fname = SpecUtils::append_path( currentdir, "file_" + std::to_string(filenum) + ".txt" );
        BOOST_CHECK( !SpecUtils::is_file(fname) );
        const int nbytes = (rand() % (1024*512));
        vector<char> writtenbytes;
        
        {//Begin writing to file
          ofstream outputfile( fname.c_str() );
          BOOST_REQUIRE( outputfile.is_open() );
          for( int i = 0 ; i < nbytes; ++i )
          {
            const char byte = rand();
            outputfile.put( byte );
            writtenbytes.push_back( byte );
          }
        }//End writing to file
        
        
        //BOOST_CHECK( SpecUtils::make_canonical_path( f ) );
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
        BOOST_CHECK( SpecUtils::make_canonical_path(fname_canonical) );
        string fname_canonical_check = fname_canonical;
        BOOST_CHECK( SpecUtils::make_canonical_path(fname_canonical_check) );
        BOOST_CHECK_EQUAL( fname_canonical_check, fname_canonical );
        
        BOOST_CHECK_MESSAGE( SpecUtils::make_canonical_path(onedotequiv), "Failed to canoniclize " << onedotequiv );
        BOOST_CHECK_MESSAGE( SpecUtils::make_canonical_path(twodotequiv), "Failed to canoniclize " << twodotequiv );
        BOOST_CHECK_MESSAGE( SpecUtils::make_canonical_path(threedotequiv), "Failed to canoniclize " << threedotequiv );
        
        BOOST_CHECK_MESSAGE( threedotequiv.find("..") == string::npos, "threedotequiv='" << threedotequiv << "'" );
        
        std::vector<char> read_bytes;
        SpecUtils::load_file_data( fname.c_str(), read_bytes );
        if( !read_bytes.empty() )
          read_bytes.resize(read_bytes.size()-1);  //Get rid of '\0' that was inserted
        
        BOOST_CHECK( writtenbytes == read_bytes );
        
        BOOST_CHECK( SpecUtils::is_file(fname) );
        //BOOST_CHECK_EQUAL( SpecUtils::file_size(fname), nbytes );
        
#ifdef _WIN32
        BOOST_CHECK_EQUAL( SpecUtils::file_size(fname),
                           boost::filesystem::file_size(SpecUtils::convert_from_utf8_to_utf16(fname) ) );
#else
        BOOST_CHECK_EQUAL( SpecUtils::file_size(fname), boost::filesystem::file_size(fname) );
#endif

  
        filesinthisdir.push_back( fname );
        added_files.push_back( fname );
      }//for( int filenum = 0; filenum < nfilescreate; ++filenum )
      
      const vector<string> lsfiles = SpecUtils::ls_files_in_directory( currentdir, "" );
      BOOST_CHECK_EQUAL( lsfiles.size(), filesinthisdir.size() );
      for( string createdfile : filesinthisdir )
      {
        bool found_file = false;
        BOOST_CHECK( SpecUtils::make_canonical_path( createdfile ) );
                    
        for( string f : lsfiles )
        {
          BOOST_CHECK( SpecUtils::make_canonical_path( f ) );
          found_file = (f == createdfile);
          if( found_file )
            break;
        }
        
        BOOST_CHECK_MESSAGE( found_file, "Failed on " << createdfile );
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
  
  
  BOOST_CHECK_MESSAGE( toplevel_dirs == toplevel_ls_dirs, "Expected " << toplevel_dirs.size() << " dirs, and got " << toplevel_ls_dirs.size() );
  
  
  vector<string> rls = SpecUtils::recursive_ls(testname2);
  vector<string> rlstxt = SpecUtils::recursive_ls(testname2,".txt");
  vector<string> rlsnone = SpecUtils::recursive_ls(testname2,".a");
  
  std::sort( begin(added_files), end(added_files) );
  std::sort( begin(rls), end(rls) );
  std::sort( begin(rlstxt), end(rlstxt) );
  std::sort( begin(rlsnone), end(rlsnone) );
  
  BOOST_CHECK_EQUAL(rls.size(), added_files.size());
  BOOST_CHECK( rls == rlstxt );
  BOOST_CHECK( added_files == rls );
  BOOST_CHECK( rlsnone.empty() );
  
  for( const string f : added_files )
  {
    BOOST_CHECK( SpecUtils::is_file(f) );
    BOOST_CHECK( !SpecUtils::is_directory(f) );
    
    vector<char> old_file_data, new_file_data;
    SpecUtils::load_file_data( f.c_str(), old_file_data );
    
    const string newname = f + "renamed.t";
    BOOST_CHECK( SpecUtils::rename_file( f, newname ) );
    BOOST_CHECK( !SpecUtils::is_file(f) );
    BOOST_CHECK( SpecUtils::is_file(newname) );
    
    SpecUtils::load_file_data( newname.c_str(), new_file_data );
    BOOST_CHECK( old_file_data == new_file_data );
    
    BOOST_CHECK( SpecUtils::remove_file(newname) );
    BOOST_CHECK( !SpecUtils::is_file(newname) );
  }//
  

  
#ifdef _WIN32
  boost::filesystem::remove_all(wtestname2);
#else
  boost::filesystem::remove_all(testname2);
#endif
}
