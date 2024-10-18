#ifndef SpecUtils_Filesystem_h
#define SpecUtils_Filesystem_h
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

#include <string>
#include <vector>

/** Some functions for working with files and the filesystem, especially when
 C++17 and boost arent available.
 
 The file and filesystem functions implemented are ones that seem to be commonly
 useful for working with spectrum files, both in applications like InterSpec and
 cambio, but also custom code to bulk process data, make websites, etc.
 All of the functions seem to work on macOS, Linux, and Windows, however given
 the limited testing (see unit_test directory) if C++17 or boost::filesystem are
 available, it would probably be better to use them.
 
 Important note for Windows: All function take in, and return, UTF-8 encoded
 filesystem paths.  However, all char based C or C++ stdlib functions that take
 filenames assume the local code-point, not UTF-8.  So instead you should always
 use the "wide" version of these functions, or when creating a
 boost::filesystem::path object.
 Example use of opening a file.
 \code{.cpp}
 for( std::string path : SpecUtils::recursive_ls("/some/path") )
 {
 #ifdef _WIN32
   const wstring wpath = SpecUtils::convert_from_utf8_to_utf16(path);
   std::ifstream input( wpath.c_str() );
 #else
   std::ifstream input( path.c_str() );
 #endif
   //... use input
 }//for(...)
 \endcode
 
 
 */
namespace  SpecUtils
{ 
  //The below uses home-spun methods, and hasn't been tested with symbolic links.
  /** \brief Removes file from the filesystem, returning true if successful. */
  bool remove_file( const std::string &name );
  
  
  /** \brief Returns if the specified name corresponds to a file that can be
   read. */
  bool is_file( const std::string &name );
  
  /** Renames a file from source to destination, returning if operation was
   successful.
   
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
   On Unix corresponds to +rwx.
   On Windows it checks you can access the directory and that it does not have the read-only set;
   however, even if this bit is set you still may be able to write in the directory, so it isnt
   actually much use.
   
   \TODO: for windows move to using `AccessCheck(...)` or just remove this function.
   */
  bool can_rw_in_directory( const std::string &name );
  
  /** \brief Concatenates parts of a filesystem name according to the operating
   system.
   
   ex. append_path("path/to","file.txt") return "path/to/file.txt" on UNIX
   or "path\to\file.txt" on Windows.
   On Windows will convert all '/' characters to '\'.
   */
  std::string append_path( const std::string &base, const std::string &name );
  
  /** \brief Returns just the filename of a path passed in
   
   ex:
   "/path/to/some/file.txt" --> "file.txt"
   "/path/to/some"          --> "some"
   "/path/to/some/"         --> ""
   "/path/to/some/.."       --> ""
   "usr"                    --> "usr"
   "/"                      --> ""
   "."                      --> ""
   ".."                     --> ""
   
   May throw exception, although very unlikely.
   */
  std::string filename( const std::string &path_and_name );
  
  /** \brief Returns the parent path of the passed in path
   
   Unix examples:
   "/path/to/some/file.txt"     --> "/path/to/some"
   "/path/to/some/path"         --> "/path/to/some"
   "/path/to/some/path/"        --> "/path/to/some";
   "/path/to/some/.."           --> "/path/to"
   "/path/to/some/../.."        --> "/path"
   "/path/to/some/../path"      --> "/path/to/some/.."
   "/"                          --> ""
   "/usr"                       --> "/"
   "."                          --> ""
   ".."                         --> ""
   "somefile"                   --> ""
   "/somefile"                  --> "/"
   "./somefile"                 --> "."
   "/path/to/some/../../../"    --> "/"
   "/path/to/some/../../../../" --> "/"
   
   Windows Examples:
   "C:" -> ""
   "C:\\" -> "C:"
   "C:\\somefile" -> "C:\"
   
   
   Note that paths like "/path/to/some/path/.." are treated differently than
   POSIX dirname(), since the ".." are resolved before getting the parent,
   rather than it just being a simple string operation.
   
   Note: does not resolve symbolic links or anything; strictly string
   operations.
   
   This function operated in the passed in path string, not the absolute
   path on the filesystem.
   
   May throw exception if input path has illegal characters (e.g., basename(...)
   (unix) or _wsplitpath_s (win32) fails.
   */
  std::string parent_path( const std::string &path );
  
  /** \brief Returns the extension of a filename, if there is one.
   
   Returns last '.' and any trailing characters from input.
   
   ex. "/path/to/some/file.txt" --> ".txt"
   "/path/to/filename"      --> ""
   ".profile"               --> ".profile"
   
   May throw exception if input path has illegal characters (e.g., basename(...)
   (unix) or _wsplitpath_s (win32) fails.
   */
  std::string file_extension( const std::string &path );
  
  /** \brief Gives the size of the file in bytes.  Returns 0 if not a file.
   */
  size_t file_size( const std::string &path );
  
  /** \brief Returns temporary directory as designated by the operating system
   (or /tmp or C:\Temp on unix and windows respectively, if not specified).
   
   Note that if you are deployed in as a FCGI app, the system environment
   wont specify the temporary directory, in that case you should consult the
   CGI values (this function will just return /tmp in that case).
   
   Does not check that the returned path is a directory, or that you have read
   and/or write permissions on it.
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
   located; if blank, only the filename will be returned.  Not checked to see
   if it is a valid directory either.
   
   A common use pattern is:
   \code{.cpp}
   ofstream tmp( temp_file_name( "mybase", temp_dir() ).c_str() );
   \endcode
   
   Note: this function is home-spun, so dont rely on it being the best.
   */
  std::string temp_file_name( std::string filebasename, std::string directory );
  
  /** Converts path to a canonical absolute path (no dot, dot-dot or symbolic
   links). If the path is not an absolute path, it is first made absolute.
   
   The file/path pointed to must exist.
   
   If the current working directory is not specified, then the directory
   returned by cwd() will be used, if the path is not already absolute.
   
   Returns true if successful, and false if it fails for any reason.
   
   On Windows, currently the path must be less than _MAX_PATH (260
   UTF-16 characters); there is is commented out code that supports
   arbitrary length, but this would then require dropping support for
   Windows 7.
   */
  bool make_canonical_path( std::string &path, const std::string &cwd = "" );
  
  /** Limit of how far down any of the recursive 'ls' functions can recurse down.
   I.e., how many directories deep can you go.
   */
  static const size_t sm_recursive_ls_max_depth = 25;
  
  /** The approximate maximum number of file names any of the 'ls' functions can
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
   be returned; ending is not case sensitive.
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
   answer is returned.  Returned answers do include 'src' in
   them (i.e. if src contains directors "a", "b", "c", you will
   get back {"src/path/a", "src/path/b", "src/path/c"}.
   
   */
  std::vector<std::string> ls_directories_in_directory( const std::string &src );
  
  
  /** Get a relative path from 'from_path' to 'to_path'
   
   @param from_path The starting path.  If not absolute, will be prepended by
          the current working path.
   @param to_path The destination filesystem location.  If not absolute, will be
          prepended by the current working path.
   @returns The path necessary to get from 'from_path' to 'to_path'
   
   Note: files are not resolved, so they do not need to exist.  File links are
   not accounted for.  When passing in non-absolute paths, be careful that
   the paths do not have so many ".." elements such that they would go up above
   the root path from the current working directory, in which case, the extra
   ".." elements are discarded, and not accounted for.
   
   assert( fs_relative( "/a/b/c/d", "/a/b/foo/bar" ) == "../../foo/bar" );
   assert( fs_relative( "a", "a/b/c") == "b/c");
   assert( fs_relative( "a/b/c/x/y", "a/b/c") == "../..");
   */
  std::string fs_relative( std::string from_path, std::string to_path );
  
  /** Removes all "." elements; for absolute paths will resolve/remove all ".."
   elements, and for relative paths will resolve/remove all ".." elements that
   wont cause a loss of path information. Preserves trailing slash
   Similar to #SpecUtils::make_canonical_path, but this function is strictly
   string based, so passed in file does not need to exist.
   
   Examples:
   "foo/./bar/.."           --> "foo"
   "foo/bar/.."             --> "foo"
   "foo/bar/../"            --> "foo/"
   "/foo/bar"               --> "/foo/bar"
   "/foo/bar/"              --> "/foo/bar/"
   "foo/.///bar/../"        --> "foo/"
   "foo/bar/../../../dude"  --> "../dude"
   ".."                     --> ".."
   "/../..")                --> "/"
   "/foo/../../.."          --> "/"
   ""                       --> ""
   "."                      --> ""
   "/foo/bar"               --> "/foo/bar"
   "/foo///bar"             --> "/foo/bar"
   "/"                      --> "/"
   "/.."                    --> "/"
   "./foo/bar/."            --> "foo/bar"
   */
  std::string lexically_normalize_path( const std::string &input );
  
  //ToDo: add in path comparisons (which don't resolve files, just use strings)
  //std::string lexically_normalize_path( std::string &input );
  //std::string fs_lexically_relative( const std::string &source, const std::string &target );
  
  //assert( fs_relative("/a/b/c","/a/d") == "../../d");
  //assert( fs_relative("/a/d","/a/b/c") == "../b/c");
  //assert( fs_relative("a","a/b/c") == "b/c");
  //assert( fs_relative("a/b/c/x/y","a/b/c") == "../..");
  //assert( fs_relative("a/b/c","a/b/c") == ".");
  //assert( fs_relative("c/d","a/b") == "../../a/b");
  
  
  /** Loads data from specified file on filesystem into the `data` vector and
   terminate with a null character.
   
   Similar functionality to rapidxml::file<char>, but takes UTF-8 filename
   as input.
   
   Throws exception on failure.
   */
  void load_file_data( const char * const filename, std::vector<char> &data );
  
  
  /** Returns true if the file is likely a spectrum file, based off of file
    extension, file size, etc..  By no means definitive, but useful when
   looping through a large amount of files in order to filter out files likely
    to not be spectrum files (but may also filter out a small amount of actual
    spectrum files in practice).
   */
  bool likely_not_spec_file( const std::string &file );
}//namespace  SpecUtils

#endif //SpecUtils_Filesystem_h
