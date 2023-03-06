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

#include <memory>
#include <string>
#include <vector>
#include <random>
#include <fstream>
#include <cstring>
#include <algorithm>

#include <iostream> //temporatry for debug

#if( ANDROID )
#include <android/log.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <pathcch.h>
#include <Lmcons.h>
#include <direct.h>
#include <io.h>
#include <shlwapi.h>
//We will need to link to Shlawapi.lib, which I think uncommenting the next line would do... untested
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib,"pathcch.lib") 
#elif __APPLE__
#include <sys/time.h>
#include <sys/sysctl.h>
#include <dirent.h>
#include <libgen.h>
#else
#include <sys/time.h>
#include <unistd.h>
#include <dirent.h>
#include <libgen.h>
#include <unistd.h>
#endif

#ifndef _WIN32
#include <unistd.h>
#endif
#include <sys/stat.h>

#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/Filesystem.h"


#if(PERFORM_DEVELOPER_CHECKS)
#include <iostream>
#endif


#ifdef _WIN32
// Copied from linux libc sys/stat.h:
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)

#undef min
#undef max
#endif

//Currently CMakeLists.txt isnt setup to link against boost, so we'll rely on
//  the unit-test to make sure the recursive_ls functions are okay
#define CHECK_RECURSIVE_LS_AGAINST_BOOST 0

#if( CHECK_RECURSIVE_LS_AGAINST_BOOST )
#include <boost/filesystem.hpp>
#endif

using namespace std;


namespace
{
  /*
   //Get a relative path from 'from_path' to 'to_path'
   //  assert( make_relative( "/a/b/c/d", "/a/b/foo/bar" ) == "../../foo/bar" );
   // Return path when appended to from_path will resolve to same as to_path
   
   
   namespace fs = boost::filesystem;
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
   */
}//namespace


namespace SpecUtils
{
std::string temp_dir()
{
#if( ANDROID )
  {
    const char *val = std::getenv("TMPDIR");
    if( !val )
    {
      __android_log_write( ANDROID_LOG_ERROR, "temp_dir",
                          "Warning, unable to get \"TMPDIR\" environment variable; returning: \"/data/local/tmp/\"" );
      
      return "/data/local/tmp/";
    }
    
    return val;
  }
#endif
  
  
#if( defined(_WIN32) )
  //Completely un-tested
  const DWORD len = GetTempPathW( 0, NULL );
  vector<wchar_t> buf( len + 1 );
  
  if( !len )
  {
    //const wchar_t *val = 0;
    //(val = _wgetenv(L"temp" )) ||
    //(val = _wgetenv(L"TEMP"));

#if defined(_MSC_VER)
    wchar_t *val = 0;
    errno_t status = _wdupenv_s( &val, nullptr, L"temp" );
    if( !val )
      status = _wdupenv_s( &val, nullptr, L"TEMP" );

    if( val )
    {
      auto answer = convert_from_utf16_to_utf8( val );
      free( val ); // It's OK to call free with NULL
      return answer;
    }
#else
    wchar_t *val = _wgetenv( L"temp" );
    if( !val )
      val = _wgetenv( L"TEMP" );
    
    if( val )
    {
      auto answer = convert_from_utf16_to_utf8( val );
      return answer;
    }
#endif

#if(PERFORM_DEVELOPER_CHECKS)
    log_developer_error( __func__, "Couldnt find temp path on Windows" );
#endif
    return "C:\\Temp";
  }//if( !len )
  
  GetTempPathW( len + 1, &(buf[0]) );
  return convert_from_utf16_to_utf8( std::wstring( buf.begin(), buf.end() ) );
#else
  
  const char *val = NULL;
  (val = std::getenv("TMPDIR" )) ||
  (val = std::getenv("TMP"    )) ||
  (val = std::getenv("TEMP"   )) ||
  (val = std::getenv("TEMPDIR"));
  
  if( val && SpecUtils::is_directory(val) )
    return val;
  
  return "/tmp";
#endif
}//std::string temp_dir()
  
  
bool remove_file( const std::string &name )
{
#ifdef _WIN32
  const std::wstring wname = convert_from_utf8_to_utf16( name );
  return (0 == _wunlink( wname.c_str()) );
#else
  return (0 == unlink( name.c_str()) );
#endif
}//bool remove_file( const std::string &name )
  
  
//bool remove_directory( const std::string &name )
//{
//#ifdef _WIN32
//  const std::wstring wname = convert_from_utf8_to_utf16( name );
//  return RemoveDirectory( wname.c_str() );
//#else
//  return (0 == rmdir( name.c_str()) );
//#endif
//}
  
  
bool rename_file( const std::string &source, const std::string &destination )
{
  if( !is_file(source) || is_file(destination) || is_directory(destination) )
    return false;
  
#ifdef _WIN32
  const std::wstring wsource = convert_from_utf8_to_utf16( source );
  const std::wstring wdestination = convert_from_utf8_to_utf16( destination );
  return MoveFileW( wsource.c_str(), wdestination.c_str() );
#else
  const int result = rename( source.c_str(), destination.c_str() );
  return (result==0);
#endif
}//bool rename_file( const std::string &from, const std::string &destination )
  
  
bool is_file( const std::string &name )
{
#ifdef _WIN32
  const std::wstring wname = convert_from_utf8_to_utf16( name );
  //ifstream file( wname.c_str() );
  //return file.good();
  
  struct _stat statbuf;
  _wstat( wname.c_str(), &statbuf);
  return S_ISREG(statbuf.st_mode) && ((statbuf.st_mode & _S_IREAD) == _S_IREAD);
  //return (_waccess(wname.c_str(), 0x04) == 0) && !S_ISDIR(statbuf.st_mode);  //0x04 checks for read
#else
  struct stat statbuf;
  stat( name.c_str(), &statbuf);
  // @TODO Make sure the ISREG and ISLNK is what we want, and also make sure Windows uses a consistent definition of what a file is...
  return (S_ISREG(statbuf.st_mode) || S_ISLNK(statbuf.st_mode)) && (access(name.c_str(), F_OK) == 0);
#endif
  /*
   #ifdef _WIN32
   const std::wstring wname = convert_from_utf8_to_utf16( name );
   ifstream file( wname.c_str() );
   return file.good();
   #else
   //  struct stat fileInfo;
   //  const int status = stat( name.c_str(), &fileInfo );
   //  if( status != 0 )
   //    return false;
   //  return S_ISREG(fileinfo.st_mode);
   ifstream file( name.c_str() );
   return file.good();
   #endif
   */
}//bool is_file( const std::string &name )
  
  
bool is_directory( const std::string &name )
{
#ifdef _WIN32
  const std::wstring wname = convert_from_utf8_to_utf16( name );
  //could also use PathIsDirectoryW
  struct _stat statbuf;
  _wstat( wname.c_str(), &statbuf);
  return S_ISDIR(statbuf.st_mode);
#else
  struct stat statbuf;
  stat( name.c_str(), &statbuf);
  return S_ISDIR(statbuf.st_mode);
#endif
}//bool is_directory( const std::string &name )
  
  
int create_directory( const std::string &name )
{
  if( is_directory(name) )
    return -1;
  
  int nError = 0;
#if ( defined(WIN32) || defined(UNDER_CE) || defined(_WIN32) || defined(WIN64) )
  const std::wstring wname = convert_from_utf8_to_utf16( name );
  nError = _wmkdir(wname.c_str()); // can be used on Windows
#else
  mode_t nMode = 0733; // UNIX style permissions
  nError = mkdir(name.c_str(),nMode); // can be used on non-Windows
#endif
  if (nError != 0) {
    // handle your error here
    return 0;
  }
  
  return 1;
}//int create_directory( const std::string &name )
  
  
bool can_rw_in_directory( const std::string &name )
{
  if( !is_directory(name) )
    return false;

#ifdef _WIN32
  const std::wstring wname = convert_from_utf8_to_utf16( name );
  
  DWORD attributes = GetFileAttributesW( wname.c_str() );
  return ( (attributes != 0xFFFFFFFF)
           && (attributes & FILE_ATTRIBUTE_DIRECTORY)
           && !(attributes & FILE_ATTRIBUTE_READONLY) );

  //const int can_access = _waccess( wname.c_str(), 0x06 );
  //return (can_access == 0);
#else
  const int can_access = access( name.c_str(), R_OK | W_OK | X_OK );
  return (can_access == 0);
#endif
}//bool can_rw_in_directory( const std::string &name )
  
  
std::string append_path( const std::string &base, const std::string &name )
{
  if( base.empty() || name.empty() )
    return base + name;
  
#if( defined(_WIN32) )
  const char separator = '\\';
  const bool base_ends = (base[base.size()-1]==separator || base[base.size()-1]=='/');
  const bool name_starts = (name[0]==separator || name[0]=='/');
#else
  const char separator = '/';
  const bool base_ends = (base[base.size()-1]==separator);
  const bool name_starts = (name[0]==separator);
#endif
  
  string answer;
  if( !base_ends && !name_starts )
    answer = base + separator + name;
  else if( base_ends != name_starts ) //base end ins separator, or name starts with separator, but not both.
    answer = base + name;
  else //base ends in '/' and name starts with '/'
    answer = base + name.substr(1);
  
#if( defined(_WIN32) )
  SpecUtils::ireplace_all( answer, "/", "\\" );
#endif
  
  return answer;
  //#else
  //  boost::filesystem::path p(base);
  //  p /= name;
  //#if( BOOST_VERSION < 106501 )
  //  return p.make_preferred().string<string>();
  //#else
  //  return p.make_preferred().lexically_normal().string<string>();
  //#endif
  //#endif
}//std::string append_path( const std::string &base, const std::string &name )
  
  
std::string filename( const std::string &path_and_name )
{
#ifdef _WIN32
  //const wstring wpath = convert_from_utf8_to_utf16(path_and_name);
  
  //this function species filename("/path/to/some/")=="some", but the MS function
  //  will give a empty filename (very reasonably!), so lets fix that.
  string p = path_and_name;
  while( !p.empty() && (p.back()=='/' || p.back()=='\\') )
    p = p.substr(0,p.size()-1);
  const wstring wpath = convert_from_utf8_to_utf16(p);

  wchar_t drive[_MAX_DRIVE+1];
  const size_t maxdirlen = (std::max)(2*wpath.size(),static_cast<size_t>(_MAX_DIR));
  const size_t maxfilenamelen = (std::max)(2*wpath.size(),static_cast<size_t>(_MAX_FNAME));
  std::unique_ptr<wchar_t []> dir( new wchar_t [maxdirlen+1] );
  std::unique_ptr<wchar_t []> fname( new wchar_t [maxfilenamelen+1] );
  wchar_t ext[_MAX_EXT+1];
  
  errno_t err = _wsplitpath_s( wpath.c_str(), drive, _MAX_DRIVE,
                               dir.get(), maxdirlen,
                               fname.get(), maxfilenamelen,
                               ext, _MAX_EXT );

  if( err != 0 )
    throw runtime_error( "Failed to _splitpath_s in filename()" );
  
  return convert_from_utf16_to_utf8( wstring(fname.get()) + ext );
#else  // _WIN32
  // "/usr/lib" -> "lib"
  // "/usr/"    -> ""
  // "usr"      -> "usr"
  // "/"        -> ""
  // "."        -> ""
  // ".."       -> ""
  if( path_and_name.empty() )
    return path_and_name;
  
  if( path_and_name.back() == '/' )
    return "";
  
  vector<char> pathvec( path_and_name.size() + 1 );
  memcpy( &(pathvec[0]), path_and_name.c_str(), path_and_name.size() + 1 );
  
  //basename is supposedly thread safe, and also you arent supposed to free what
  //  it returns
  char *bname = basename( &(pathvec[0]) );
  if( !bname ) //shouldnt ever happen!
    throw runtime_error( "Error with basename in filename()" );
  
  string answer = bname;
  if( answer == ".." || answer == "." )
    return "";
  
  return answer;
#endif
}//std::string filename( const std::string &path_and_name )
  
  
std::string parent_path( const std::string &path )
{
  
#ifdef _WIN32  
  string p = path;
  int numdotdot = 0;
  while( p.size() > 3 )
  {
    const size_t len = p.length();
    if( p.back() == '/' || p.back() == '\\' )
    {
      p = p.substr(0,len-1);
      continue;
    }
    
    if( (p[len-3]=='/' || p[len-3]=='\\') && p[len-2]=='.' && p[len-1]=='.' )
    {
      ++numdotdot;
      p = p.substr(0,len-3);
      continue;
    }
    
    if( numdotdot > 0 )
    {
      p = p.substr(0, p.find_last_of( "/\\" ) );
      --numdotdot;
      continue;
    }
    
    break;
  }//while( normalized.size() > 3 )
  
  
  std::wstring wpath = convert_from_utf8_to_utf16( p );

  wchar_t drive[_MAX_DRIVE+1];
  const size_t maxdirlen = (std::max)(2*wpath.size(),static_cast<size_t>(_MAX_DIR)) + 1;
  const size_t maxfilenamelen = (std::max)(2*wpath.size(),static_cast<size_t>(_MAX_FNAME)) + 1;
  std::unique_ptr<wchar_t []> dir( new wchar_t [maxdirlen+1] );
  std::unique_ptr<wchar_t []> fname( new wchar_t [maxfilenamelen+1] );
  wchar_t ext[_MAX_EXT+1];
  
  errno_t err = _wsplitpath_s( wpath.c_str(), drive, _MAX_DRIVE,
                               dir.get(), maxdirlen,
                               fname.get(), maxfilenamelen,
                               ext, _MAX_EXT );
  
  if( err != 0 )
    throw runtime_error( "Failed to get parent-path" );
  
  //wcout << L"0) '" << wpath << L"' -> drive='" << drive << L"', dir='" << dir.get()
  //      << L"', fname='" << fname.get() << L"', ext='" << ext << L"'" << endl;
  
  //"C:"  -> ""
  if( wcslen(dir.get())==0 && wcslen(fname.get())==0 && wcslen(ext)==0 )
    return "";
  
  //Remove trailing '\' from Dir
  //  "C:\\"  -> "C:"
  //  ".\\somefile" -> "." (but right now  dir='.\\')
  //  "\\\\" -> "" (but right now dir='\\\\')
  size_t dirlen = wcslen(dir.get());
  while( dirlen && ((dir[dirlen-1] == L'\\') || (dir[dirlen-1] == L'/')) )
    dir[--dirlen] = L'\0';
  
  //wcout << L"1) '" << wpath << L"' -> drive='" << drive << L"', dir='" << dir.get()
  //      << L"', fname='" << fname.get() << L"', ext='" << ext << L"'" << endl;
  
  
  const size_t maxpathlen = (std::max)(2*wpath.size(),static_cast<size_t>(_MAX_PATH));
  std::unique_ptr<wchar_t []> path_buffer( new wchar_t [maxpathlen+1] );
  
  err = _wmakepath_s( path_buffer.get(), maxpathlen, drive, dir.get(), nullptr, nullptr );
  
  if( err != 0 )
    throw runtime_error( "Failed to make path in parent_path" );
  
  string answer = convert_from_utf16_to_utf8( path_buffer.get() );
  
  if( answer.size() && dirlen && (answer.back()=='\\' || answer.back()=='/') && dir[dirlen-1]!=L'/' && dir[dirlen-1]!=L'\\' )
    answer.resize( answer.size() - 1 );
  
  // We need to remove the trailing '\' character.
  /// @TODO is the below always valid
  if( answer.size()>3 && answer[answer.size()-1] == '\\' )
    answer = answer.substr(0,answer.size()-1);
  
  return answer;
#else
  //dirname from libgen.h
  //"/usr/lib" -> "/usr"
  //"/usr/" -> "/"
  //"usr" -> ""
  //"." -> ""
  vector<char> pathvec( path.size() + 1 );
  memcpy( &(pathvec[0]), path.c_str(), path.size() + 1 );
  
  char *bname = basename( &(pathvec[0]) );
  
  if( !bname )
    throw runtime_error( "Failed to make path in parent_path" );
  
  int nparent = 0;
  while( strcmp(bname,"..") == 0 )
  {
    char *parname = dirname( &(pathvec[0]) );
    if( !parname )
    {
      // I think we can get here for like invalid utf-8 characters or something, or maybe
      //  just really long input, or maybe invalid paths; not sure, only hit this during fuzzing
      break;
    }
    
    size_t newlen = strlen(parname);
    pathvec.resize( newlen + 1 );
    pathvec[newlen] = '\0';
    
    ++nparent;
    bname = basename( &(pathvec[0]) );
  }
  
  for( int i = 0; i < nparent; ++i )
  {
    char *parname = dirname( &(pathvec[0]) );
    size_t newlen = strlen(parname);
    pathvec.resize( newlen + 1 );
    pathvec[newlen] = '\0';
  }
  
  //dirname is supposedly thread safe, and also you arent supposed to free what
  //  it returns
  char *parname = dirname( &(pathvec[0]) );

  if( !parname )
    throw runtime_error( "Failed to make dirname in parent_path" );
  
  string answer = parname;
  
  if( answer == path )
    answer.clear();
  
  if( answer=="." && !path.empty() && path[0]!='.' )
    answer.clear();
  
  if( answer=="." && path.size()>1 && path[0]=='.' && path[1]=='.' )
    answer.clear();
  
  
  return answer;
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
  
  
size_t file_size( const std::string &path )
{
#ifdef _WIN32
  std::wstring wpath = convert_from_utf8_to_utf16( path );
  struct _stat st;
  if( _wstat( wpath.c_str(), &st) < 0 )
    return 0;
  
  if( S_ISDIR(st.st_mode) )
    return 0;
  
  return st.st_size;
#else
  struct stat st;
  if( stat(path.c_str(), &st) < 0 )
    return 0;
  
  if( S_ISDIR(st.st_mode) )
    return 0;
  
  return st.st_size;
#endif
}//size_t file_size( const std::string &path )
  
  
bool is_absolute_path( const std::string &path )
{
#ifdef WIN32
  std::wstring wpath = convert_from_utf8_to_utf16( path );
  return !PathIsRelativeW( wpath.c_str() );
#else
  return (path.size() && (path[0]=='/'));
#endif
}//bool is_absolute_path( const std::string &path )
  
  
std::string get_working_path()
{
#ifdef WIN32
  wchar_t *buffer = _wgetcwd(nullptr, 0);
  if( !buffer )
    return "";
  
  const std::wstring cwdtemp = buffer;
  free( buffer );
  
  //cout << "get_working_path()='" << convert_from_utf16_to_utf8( cwdtemp ) << "'" << std::endl;
  return convert_from_utf16_to_utf8(cwdtemp);
#else
  char buffer[PATH_MAX];
  return (getcwd(buffer, sizeof(buffer)) ? std::string(buffer) : std::string(""));
#endif
}//std::string get_working_path();
  
  
std::string temp_file_name( std::string base, std::string directory )
{
  //For alternative implementations (this one is probably by no means
  //trustworthy) see http://msdn.microsoft.com/en-us/library/aa363875%28VS.85%29.aspx
  // for windows
  // Or just grab from unique_path.cpp in boost.
  
  
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
  static_assert( sizeof(hex) == 17, "" );
  
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
}//temp_file_name
  
  
bool make_canonical_path( std::string &path, const std::string &cwd )
{
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
  
#if( defined(_WIN32) )
  const std::wstring wpath = convert_from_utf8_to_utf16( path );
  
  //MinGW has trouble linking since its libraries dont have PathCchCanonicalizeEx(...) it doesnt look like
  //PathCchCanonicalizeEx requires linking to "Pathcch.lib", and is only supported on Windows 8 and
  //  newer, but both Desktop and UWP
  //PathCanonicalizeW Is supported on Windows XP on, but only on desktop
/*
#if( defined(_MSC_VER) )
  const ULONG flags = PATHCCH_ALLOW_LONG_PATHS;
  const size_t pathlen = (std::max)( (std::max)( static_cast<size_t>(_MAX_PATH), static_cast<size_t>(2048) ), 2*wpath.size() );
  std::unique_ptr<wchar_t []> buffer( new wchar_t [pathlen+1] );
  if( !buffer )
    return false;
  
  if( PathCchCanonicalizeEx( buffer.get(), pathlen, wpath.c_str(), flags ) == S_OK )
  {
    path = convert_from_utf16_to_utf8( buffer.get() );
    return true;
  }else
  {
    path.clear();
  }
#else
*/
  wchar_t buffer[_MAX_PATH];
  if( PathCanonicalizeW( buffer, wpath.c_str() ) )
  {
    path = convert_from_utf16_to_utf8( buffer );
    return true;
  }
  path.clear();
/*
#endif
*/
  return false;
#else //WIN32
  vector<char> resolved_name(PATH_MAX + 1, '\0');
  char *linkpath = realpath( path.c_str(), &(resolved_name[0]) );
  if( !linkpath )
    return false;
  path = linkpath;
  return true;
#endif
}//bool make_canonical_path( std::string &path )
  
  
  
  
  
  
bool filter_ending( const std::string &path, void *user_match_data )
{
  const std::string *ending = (const std::string *)user_match_data;
  return SpecUtils::iends_with(path, *ending);
}

  
#ifdef _WIN32
//Could replace recursive_ls_internal() with the following to help get rid of linking to boost
//https://stackoverflow.com/questions/2314542/listing-directory-contents-using-c-and-windows


vector<std::string> recursive_ls_internal_windows( const std::string &sourcedir,
         file_match_function_t match_fcn,
         void *user_match_data,
         const size_t depth,
         const size_t numfiles )
{
  vector<std::string> files;
  
  if( depth >= sm_recursive_ls_max_depth )
    return files;
  
  WIN32_FIND_DATAW fdFile;
  HANDLE hFind = NULL;
  
  wstring wpath = convert_from_utf8_to_utf16(sourcedir);
  if( wpath.size() && wpath.back()!=L'/' && wpath.back()!=L'\\' )
    wpath += L"\\";
  
  //std::wcout << L"recursive_ls_internal_windows(\"" << wpath << L"\")" << std::endl;
  
  //FindFirstFileExW
  //Specify a file mask. *.* = We want everything!
  if((hFind = FindFirstFileW( (wpath + L"*").c_str(), &fdFile)) == INVALID_HANDLE_VALUE)
  {
    //std::wcout << L"recursive_ls_internal_windows(\"" << wpath << L"\"): Path not found." << std::endl;
    return files;
  }
  
  do
  {
    if( (files.size()+numfiles) >= sm_ls_max_results )
      break;
      
    //Find first file will always return "."
    //    and ".." as the first two directories.
    if( (fdFile.cFileName == wstring(L".")) || (fdFile.cFileName == wstring(L"..")) )
      continue;
    
    //std::wcout << L"On filename: '" << fdFile.cFileName << L"'" << std::endl;
    
    //Build up our file path using the passed in
    //  [sDir] and the file/foldername we just found:
    wstring newpath = wpath + ((wpath.size() && wpath.back() != L'\\') ? L"\\" : L"") + fdFile.cFileName;
    //sprintf(sPath, "%s\\%s", sDir, fdFile.cFileName);
    
    //Is the entity a File or Folder?
    if( fdFile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
      //std::wcout << L"recursive_ls_internal_windows(...) Directory: '" << newpath << L"'" << std::endl;
      string newdirname = SpecUtils::convert_from_utf16_to_utf8(newpath);
      
      const auto r = recursive_ls_internal_windows( newdirname,
                                   match_fcn, user_match_data, depth+1, files.size() + numfiles );
      
      files.insert( files.end(), r.begin(), r.end() );
    }else
    {
      //std::wcout << L"recursive_ls_internal_windows(...) File: '" << newpath << L"'" << std::endl;
      string newfilename = SpecUtils::convert_from_utf16_to_utf8(newpath);
      if( !match_fcn || match_fcn(newfilename,user_match_data) )
        files.push_back( newfilename );
    }
  }while( FindNextFileW( hFind, &fdFile) ); //Find the next file.
  
  FindClose(hFind);
  
  return files;
}
#endif  //#ifdef _WIN32

 
#if( PERFORM_DEVELOPER_CHECKS && CHECK_RECURSIVE_LS_AGAINST_BOOST )
vector<std::string> recursive_ls_internal_boost( const std::string &sourcedir,
                                                  file_match_function_t match_fcn,
                                                  void *user_match_data,
                                                  const size_t depth,
                                                  const size_t numfiles )
{
  using namespace boost::filesystem;
  
  vector<string> files;
  
   //A shorter untested implementation, that might be better.
   //for( recursive_directory_iterator iter(sourcedir), end; iter != end; ++iter )
   //{
   //const std::string name = iter->path().filename().string();
   //const bool isdir = SpecUtils::is_directory( name );
   //if( !isdir && (!match_fcn || match_fcn(name,user_match_data)) )
   //  files.push_back( name );
   //if( files.size() >= sm_ls_max_results )
   //  break;
   //}
   //return files;
  
  
  if( depth >= sm_recursive_ls_max_depth )
    return files;
  
  if ( !SpecUtils::is_directory( sourcedir ) )
    return files;
  
  directory_iterator end_itr; // default construction yields past-the-end
  
  directory_iterator itr;
  try
  {
#ifdef _WIN32
    const std::wstring wsourcedir = convert_from_utf8_to_utf16( sourcedir );
    itr = directory_iterator( wsourcedir );
#else
    itr = directory_iterator( sourcedir );
#endif
  }catch( std::exception & )
  {
    //ex: boost::filesystem::filesystem_error: boost::filesystem::directory_iterator::construct: Permission denied: "..."
    return files;
  }
  
  for( ; (itr != end_itr) && ((files.size()+numfiles) < sm_ls_max_results); ++itr )
  {
    const boost::filesystem::path &p = itr->path();
#ifdef _WIN32
    const wstring wpstr = p.string<std::wstring>();
    const string pstr = convert_from_utf16_to_utf8( wpstr );
#else
    const string pstr = p.string<string>();
#endif
    
    const bool isdir = SpecUtils::is_directory( pstr );
    
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
          if( SpecUtils::starts_with( pcanon.string<string>(), resvedpath.string<string>().c_str() ) )
            continue;
        }catch(...)
        {
        }
      }//if( boost::filesystem::is_symlink(pstr) )
#endif
      
      const vector<string> r = recursive_ls_internal_boost( pstr, match_fcn, user_match_data, depth + 1, files.size() );
      files.insert( files.end(), r.begin(), r.end() );
    }else if( SpecUtils::is_file( pstr ) ) //if ( itr->leaf() == patern ) // see below
    {
      if( !match_fcn || match_fcn(pstr,user_match_data) )
        files.push_back( pstr );
    }//
  }//for( loop over
  
  return files;
}//recursive_ls(...)
#endif //PERFORM_DEVELOPER_CHECKS && CHECK_RECURSIVE_LS_AGAINST_BOOST

  
  
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
    log_developer_error( __func__, errormsg );
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
    log_developer_error( __func__, errormsg );
#endif
    return -1;
  }//if( r > sb.st_size )
  
  //Check if symlink is relative or absolute, and if relative
  string linkfull;
  if( linkname[0] == '.' )
    linkfull = SpecUtils::append_path( SpecUtils::parent_path(filename), &(linkname[0]) );
  else
    linkfull = &(linkname[0]);
  
  vector<char> resolved_link_name, resolved_parent_name;
  
  resolved_link_name.resize(PATH_MAX + 1);
  resolved_parent_name.resize(PATH_MAX + 1);
  
  char *linkpath = realpath( linkfull.c_str(), &(resolved_link_name[0]) );
  char *parentpath = realpath( SpecUtils::parent_path(filename).c_str(), &(resolved_parent_name[0]) );
  
  resolved_link_name[resolved_link_name.size()-1] = '\0';     //JIC
  resolved_parent_name[resolved_parent_name.size()-1] = '\0'; //JIC
  
  if( !linkpath || !parentpath )
  {
#if(PERFORM_DEVELOPER_CHECKS)
    char errormsg[1024];
    snprintf( errormsg, sizeof(errormsg), "Warning: Couldnt resolve real path for '%s' or %s ", linkfull.c_str(), filename.c_str() );
    log_developer_error( __func__, errormsg );
#endif
    return -1;
  }
  
  if( SpecUtils::starts_with(parentpath, linkpath) )
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
    log_developer_error( __func__, errormsg );
#endif
    return files;
  }//if( couldnt open directory )
  
  errno = 0;
  struct dirent *dent = nullptr;
  
  while( (dent = readdir(dir)) && ((numfiles + files.size()) < sm_ls_max_results) )
  {
    if( !strcmp(dent->d_name, ".") || !strcmp(dent->d_name, "..") )
      continue;
    
    const string filename = SpecUtils::append_path( sourcedir, dent->d_name );
    
    //handling (dent->d_type == DT_UNKNOWN) is probably unecassary, but we'll
    //  do it anyway since the cost is cheap.
    //We dont want to bother calling is_diretory() or is_file() (or stat in
    //  general) since these operations are kinda expensive, so we will only
    //  do it if necassary.
    
    const bool follow_sym_links = true;
    bool is_dir = (dent->d_type == DT_DIR) || ((dent->d_type == DT_UNKNOWN) && SpecUtils::is_directory(filename));
    if( !is_dir && follow_sym_links && (dent->d_type == DT_LNK) && SpecUtils::is_directory(filename) )
    {
      is_dir = (0==check_if_symlink_is_to_parent(filename));
    }//if( a symbolic link that doesnt resolve to a file )
    
    const bool is_file = !is_dir
    && ((dent->d_type == DT_REG)
        || (follow_sym_links && (dent->d_type == DT_LNK) && SpecUtils::is_file(filename))  //is_file() checks for broken link
        || ((dent->d_type == DT_UNKNOWN) && SpecUtils::is_file(filename)));
    
    if( is_dir )
    {
      //Note that we are leaving the directory open here - there is a limit on
      //  the number of directories we can have open (like a couple hundred)
      //  (we could refactor things to avoid this, but whatever for now)
      const vector<string> r = recursive_ls_internal_unix( filename, match_fcn, user_match_data, depth + 1, files.size() + numfiles );
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
  
  
#if( PERFORM_DEVELOPER_CHECKS && CHECK_RECURSIVE_LS_AGAINST_BOOST )
  auto from_boost = recursive_ls_internal_boost( sourcedir, match_fcn, user_match_data, depth, files.size() + numfiles );
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
      log_developer_error( __func__, errormsg );
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
    return recursive_ls_internal_windows( sourcedir, (file_match_function_t)0, 0, 0, 0 );
  return recursive_ls_internal_windows( sourcedir, &filter_ending, (void *)&ending, 0, 0 );
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
#ifdef _WIN32
  return recursive_ls_internal_windows( sourcedir, match_fcn, match_data, 0, 0 );
#else
  return recursive_ls_internal_unix( sourcedir, match_fcn, match_data, 0, 0 );
#endif
}//recursive_ls
  
  
vector<string> ls_files_in_directory( const std::string &sourcedir, const std::string &ending )
{
  if( ending.empty() )
    return ls_files_in_directory( sourcedir, (file_match_function_t)0, 0 );
  return ls_files_in_directory( sourcedir, &filter_ending, (void *)&ending );
}//ls_files_in_directory
  

std::vector<std::string> ls_files_in_directory( const std::string &sourcedir,
                                                 file_match_function_t match_fcn,
                                                 void *user_data )
{
#ifndef _WIN32
  return recursive_ls_internal_unix( sourcedir, match_fcn, user_data, sm_recursive_ls_max_depth-1, 0 );
#else
  return recursive_ls_internal_windows( sourcedir, match_fcn, user_data, sm_recursive_ls_max_depth-1, 0 );
/*
  using namespace boost::filesystem;
  
  vector<string> files;
  if ( !SpecUtils::is_directory( sourcedir ) )
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
    const bool isfile = SpecUtils::is_file( pstr );
    
    if( isfile )
      if( !match_fcn || match_fcn(pstr,user_data) )
        files.push_back( pstr );
  }//for( loop over
  
  return files;
*/
#endif
}//ls_files_in_directory(...)
  
  
vector<string> ls_directories_in_directory( const std::string &src )
{
  vector<string> answer;
  
#ifndef _WIN32
  errno = 0;
  DIR *dir = opendir( src.c_str() );
  if( !dir )
  {
#if(PERFORM_DEVELOPER_CHECKS)
    char buff[1024], errormsg[1024];
    strerror_r( errno, buff, sizeof(buff)-1 );
    snprintf( errormsg, sizeof(errormsg), "ls dir failed to open directory '%s' with error: %s", src.c_str(), buff );
    log_developer_error( __func__, errormsg );
#endif
    return answer;
  }//if( couldnt open directory )
  
  errno = 0;
  struct dirent *dent = nullptr;
  
  while( (dent = readdir(dir)) && ((answer.size()) < sm_ls_max_results) )
  {
    if( !strcmp(dent->d_name, ".") || !strcmp(dent->d_name, "..") )
      continue;
    
    const string filename = SpecUtils::append_path( src, dent->d_name );
    
    //handling (dent->d_type == DT_UNKNOWN) is probably unecassary, but we'll
    //  do it anyway since the cost is cheap.
    //We dont want to bother calling is_diretory() or is_file() (or stat in
    //  general) since these operations are kinda expensive, so we will only
    //  do it if necassary.
    
    const bool follow_sym_links = true;
    bool is_dir = (dent->d_type == DT_DIR) || ((dent->d_type == DT_UNKNOWN) && SpecUtils::is_directory(filename));
    if( !is_dir && follow_sym_links && (dent->d_type == DT_LNK) && SpecUtils::is_directory(filename) )
    {
      is_dir = (0==check_if_symlink_is_to_parent(filename));
    }//if( a symbolic link that doesnt resolve to a file )
    
    if( is_dir )
      answer.push_back( append_path(src,dent->d_name) );
  }//while( dent )
  
  closedir( dir ); //Should we bother checking/handling errors
#else
  
  WIN32_FIND_DATAW fdFile;
  HANDLE hFind = NULL;
  
  wstring wpath = convert_from_utf8_to_utf16(src);
  if( wpath.size() && wpath.back()!=L'/' && wpath.back()!=L'\\' )
    wpath += L"\\";
  
  //Specify a file mask. *.* = We want everything!
  //std::wcout << L"ls_directories_in_directory(\"" << wpath << L"\")" << std::endl;
  
  
  //FindFirstFileExW
  if((hFind = FindFirstFileW((wpath + L"*").c_str(), &fdFile)) == INVALID_HANDLE_VALUE)
  {
    //std::wcout << L"ls_directories_in_directory(\"" << wpath << L"\"): Path not found." << std::endl;
    return answer;
  }
  
  do
  {
    if( (fdFile.cFileName == wstring(L".")) || (fdFile.cFileName == wstring(L"..")) )
      continue;
    
    wstring newpath = wpath + ((wpath.size() && wpath[wpath.size()-1] != L'\\') ? L"\\" : L"") + fdFile.cFileName;
    
    //Is the entity a File or Folder?
    if( fdFile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
      //std::wcout << L"ls_directories_in_directory(...) Directory: '" << newpath << L"'" << std::endl;
      string newdirname = SpecUtils::convert_from_utf16_to_utf8(newpath);
      answer.push_back( newdirname );
    }
  }while( FindNextFileW( hFind, &fdFile) ); //Find the next file.
  
  FindClose(hFind);
  
  
  /*
  using namespace boost::filesystem;
  directory_iterator end_itr; // default construction yields past-the-end
  
  directory_iterator itr;
  try
  {
    itr = directory_iterator( src );
  }catch( std::exception & )
  {
    //ex: boost::filesystem::filesystem_error: boost::filesystem::directory_iterator::construct: Permission denied: "..."
    return answer;
  }
  
  for( ; itr != end_itr; ++itr )
  {
    const boost::filesystem::path &p = itr->path();
    const string pstr = p.string<string>();
    const bool isdir = SpecUtils::is_directory( pstr );
    
    if( isdir )
      answer.push_back( append_path(src, p.filename().string<string>()) );
  }//for( loop over
  */
#endif  //ifndef windows / else
  
  return answer;
}//std::vector<std::string> ls_directories_in_directory( const std::string &src )
  
  
std::string lexically_normalize_path( const std::string &input )
{
  const bool isabs = is_absolute_path(input);
  
  vector<string> components;
  
#if( defined(_WIN32) )
  const char * const delim = "\\";
  SpecUtils::split( components, input, "/\\" );
  
  if( isabs && input.size() > 1 && !components.empty() && input[0]=='\\' && input[1]=='\\' )
    components[0] = "\\\\" + components[0];
#else
  const char * const delim = "/";
  SpecUtils::split( components, input, "/" );
#endif
  
  //Remove all "." elements
  bool found = true;
  while( found )
  {
    auto pos = std::find( begin(components), end(components), string(".") );
    found = (pos != end(components));
    if( found )
      components.erase(pos);
  }//while( found )
  
  size_t nlead_dotdot = 0;
  
  //Remove all ".." elements, and the path component proceeding them.
  found = true;
  while( found && !components.empty() )
  {
    auto pos = std::find( begin(components), end(components), string("..") );
    found = (pos != end(components));
    if( pos == begin(components) )
    {
      if( !isabs )
        ++nlead_dotdot;
      components.erase(pos);
    }else if( pos != end(components) )
    {
      components.erase(pos-1,pos+1);
    }
  }//while( found )
  
  //now combine together, and make sure to get trailing slash right.
  string answer;
  
  for( size_t i = 0; i < nlead_dotdot; ++i )
    answer += (i ? delim : "") + string("..");
  
  for( size_t i = 0; i < components.size(); ++i )
    answer += (answer.empty() ? "" : delim) + components[i];
  
  const char first = (!input.empty() ? input[0] : ' ');
  const char last = (input.size() > 1) ? input[input.size()-1] : ' ';
  
  if( (first==delim[0] || first=='/') && (answer.empty() || answer[0]!=delim[0])  )
    answer = delim + answer;
  
  if( (last==delim[0] || last=='/') && (answer.empty() || answer[answer.size()-1]!=delim[0]) )
    answer += delim;
  
  return answer;
}//std::string lexically_normalize_path( std::string &input )
  
  
std::string fs_relative( std::string from_path, std::string to_path )
{
  string answer;
  
  const bool from_is_abs = SpecUtils::is_absolute_path(from_path);
  const bool to_is_abs = SpecUtils::is_absolute_path(to_path);
  
  const string cwd = (!from_is_abs || !to_is_abs) ? SpecUtils::get_working_path() : std::string();
  
  if( !from_is_abs )
    from_path = SpecUtils::append_path( cwd, from_path );
  if( !to_is_abs )
    to_path = SpecUtils::append_path( cwd, to_path );
  
  from_path = SpecUtils::lexically_normalize_path(from_path);
  to_path = SpecUtils::lexically_normalize_path(to_path);
  
#if( defined(_WIN32) )
  const char * const delim = "\\";
#else
  const char * const delim = "/";
#endif
  
  std::vector<std::string> from_components, to_components;
  SpecUtils::split( from_components, from_path, delim );
  SpecUtils::split( to_components, to_path, delim );
  
  
#if( defined(_WIN32) )
  //For windows check if on network drive or extended (ex. R"(\\?\C:\mypath)" ),
  // and if so append to zeroth element
  //  ... however, I 'm not sure relative paths work across drives on windows...
  //TODO: Check if relative paths work across drives, and how that should be handled..
  if( from_path.size() > 1 && !from_components.empty() && from_path[0]=='\\' && from_path[1]=='\\' )
    from_components[0] = "\\\\" + from_components[0];
  
  if( to_path.size() > 1 && !to_components.empty() && to_path[0]=='\\' && to_path[1]=='\\' )
    to_components[0] = "\\\\" + to_components[0];
#endif
  
  
  size_t from_index = 0, to_index = 0;
  
  for( ; from_index < from_components.size() && to_index < to_components.size()
      && from_components[from_index]==to_components[to_index];
      ++from_index, ++to_index )
  {
  }
  
  for( ; from_index < from_components.size(); ++from_index )
  {
    if( from_components[from_index] != "." )
      answer = SpecUtils::append_path(answer, "..");
  }
  
  // Now navigate down the directory branch
  for( ; to_index < to_components.size(); ++to_index )
    answer = SpecUtils::append_path( answer, to_components[to_index] );
  
  
  return answer;
}//std::string fs_relative( const std::string &target, const std::string &base )
  
  
void load_file_data( const char * const filename, std::vector<char> &data )
{
  data.clear();
  
#ifdef _WIN32
  const std::wstring wfilename = convert_from_utf8_to_utf16(filename);
  basic_ifstream<char> stream(wfilename.c_str(), ios::binary);
#else
  basic_ifstream<char> stream(filename, ios::binary);
#endif
  
  if (!stream)
    throw runtime_error(string("cannot open file ") + filename);
  stream.unsetf(ios::skipws);
  
  // Determine stream size
  stream.seekg(0, ios::end);
  size_t size = static_cast<size_t>( stream.tellg() );
  stream.seekg(0);
  
  // Load data and add terminating 0
  data.resize(size + 1);
  stream.read(&data.front(), static_cast<streamsize>(size));
  data[size] = 0;
}//void load_file_data( const std::string &filename, std::vector<char> &data )
  
  

  
  
bool likely_not_spec_file( const std::string &fullpath )
{
  try
  {
    const std::string extension = SpecUtils::file_extension( fullpath );
    const std::string filename = SpecUtils::filename( fullpath );
    const std::string dir = SpecUtils::parent_path( fullpath );
    
    const char * const bad_exts[] = { ".jpg", ".jpeg", ".zip", ".docx", ".png",
      ".pdf", ".html", ".xtk3d", ".xtk", ".doc", /*".txt",*/ ".An1", ".rpt",
      ".ufo", ".bmp", ".IIF", ".xls", ".xlsx", ".ds_store", ".kmz", ".msg",
      ".exe", ".jp2", ".wmv", /*".gam",*/ ".pptx", ".htm", ".ppt", ".mht",
      ".ldb", ".lis", ".zep", ".ana", ".eft", ".clb", ".lib", ".wav", ".gif",
      ".wmf", /*".phd",*/ ".log", ".vi", ".incident", ".tiff", ".cab", ".ANS",
      ".menc", ".tif", ".psd", ".mdb", ".drill", ".lnk", ".mov", ".rtf", ".shx",
      ".dbf", ".prj", ".sbn", ".shb", ".inp1", ".bat", ".xps", ".svy", ".ini",
      ".2", ".mp4", ".sql", ".gz", ".url", ".zipx", ".001", ".002", ".003",
      ".html", ".sqlite3"
    };
    const size_t num_bad_exts = sizeof(bad_exts) / sizeof(bad_exts[0]);
    
    for( size_t i = 0; i < num_bad_exts; ++i )
    if( SpecUtils::iequals_ascii(extension, bad_exts[i]) )
      return true;
    
    if( //(filename.find("_AA_")!=std::string::npos && SpecUtils::iequals_ascii(extension, ".n42") )
       //|| (filename.find("_AN_")!=std::string::npos && SpecUtils::iequals_ascii(extension, ".n42") )
       filename.find("Neutron.n42") != std::string::npos
       || filename.find(".xml.XML") != std::string::npos
       || filename.find("results.xml") != std::string::npos
       || filename.find("Rebin.dat") != std::string::npos
       || filename.find("Detector.dat") != std::string::npos
       || SpecUtils::iends_with( fullpath, ".html")
       || filename.empty()
       || filename[0] == '.'
       || extension.empty()
       || SpecUtils::file_size(fullpath) < 100
       )
    {
      return true;
    }
  }catch( std::exception & )
  {
    //we only get here when SpecUtils::filename(fullpath) is an invalid file path
    return true;
  }
  
  return false;
}//bool likely_not_spec_file( const std::string &file )

}//namespace  SpecUtils
