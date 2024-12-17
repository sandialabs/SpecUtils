cmake_minimum_required(VERSION 3.15...3.26)

project(SpecUtils_py LANGUAGES CXX)

set( SpecUtils_ENABLE_D3_CHART ON CACHE BOOL "" )
set( SpecUtils_D3_SUPPORT_FILE_STATIC ON CACHE BOOL "" )
set( SpecUtils_ENABLE_URI_SPECTRA OFF CACHE BOOL "" )  #Lets not worry about linking to zlib right now
set( SpecUtils_PYTHON_BINDINGS ON CACHE BOOL "Build the python bindings" )
set( SpecUtils_ENABLE_EQUALITY_CHECKS OFF CACHE BOOL "" )
set( PERFORM_DEVELOPER_CHECKS OFF CACHE BOOL "" )
set( SpecUtils_SHARED_LIB OFF CACHE BOOL "" )  #we'll statically link SpecUtils into the lib we create here
set( SpecUtils_FLT_PARSE_METHOD "strtod" CACHE STRING "Float parsing method" )

add_subdirectory( ${CMAKE_CURRENT_SOURCE_DIR}/../.. ${CMAKE_CURRENT_BINARY_DIR}/LibSpecUtils )

if (NOT SKBUILD)
  message(FATAL_ERROR "\
  This CMake file is must be executed using 'scikit-build'. 
  If you are a user trying to install this package, please use the command
  below, which will install all necessary build dependencies, compile
  the package in an isolated environment, and then install it.
  =====================================================================
   $ pip install .
  =====================================================================
  If you are a software developer, and this is your own package, then
  it is usually much more efficient to install the build dependencies
  in your environment once and use the following command that avoids
  a costly creation of a new virtual environment at every compilation:
  =====================================================================
   $ pip install nanobind scikit-build-core
   $ pip install --no-build-isolation -ve .
  =====================================================================
  You may optionally add -Ceditable.rebuild=true to auto-rebuild when
  the package is imported. Otherwise, you need to re-run the above
  after editing C++ files.")
endif()

# Try to import all Python components potentially needed by nanobind
find_package(Python 3.8
  REQUIRED COMPONENTS Interpreter Development.Module
  OPTIONAL_COMPONENTS Development.SABIModule)

# Import nanobind through CMake's find_package mechanism
find_package(nanobind CONFIG REQUIRED)

# We are now ready to compile the actual extension module
nanobind_add_module(
  # Name of the extension
  PySpecUtils

  # Target the stable ABI for Python 3.12+, which reduces
  # the number of binary wheels that must be built. This
  # does nothing on older Python versions
  STABLE_ABI

  # Build libnanobind statically and merge it into the
  # extension (which itself remains a shared library)
  #
  # If your project builds multiple extensions, you can
  # replace this flag by NB_SHARED to conserve space by
  # reusing a shared libnanobind across libraries
  NB_STATIC

  #Perform link time optimization.
  #LTO


  # Source code goes here
  SpecFile_py.cpp
)

set_target_properties( PySpecUtils PROPERTIES OUTPUT_NAME "SpecUtils" )
target_link_libraries( PySpecUtils PUBLIC SpecUtils )

# Install directive for scikit-build-core
install(TARGETS PySpecUtils LIBRARY DESTINATION SpecUtils)
install(FILES __init__.py DESTINATION SpecUtils) 

nanobind_add_stub(
  SpecUtils_stub
    MODULE SpecUtils
    OUTPUT SpecUtils.pyi
    DEPENDS PySpecUtils
)