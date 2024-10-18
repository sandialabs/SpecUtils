if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW) # For CMake 3.15 and newer, enable use of CMAKE_MSVC_RUNTIME_LIBRARY, without requiring CMake 3.15
endif()

cmake_minimum_required(VERSION 3.1...3.29)

if(${CMAKE_VERSION} VERSION_LESS 3.12)
  cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
endif()

project( SpecUtils VERSION 1.0.0 )

enable_testing()

option( SpecUtils_ENABLE_D3_CHART "Enables exporting of the D3 chart format" ON )
option( SpecUtils_D3_SUPPORT_FILE_STATIC "Compiles the JS and CSS files into memory, rather than reading files on disk; disable to allow changing JS/CSS without having to recompile" ON )
option( SpecUtils_ENABLE_URI_SPECTRA "Enables encoding/decoding spectra to URIs (for, ex, QR-codes) - requires linking to zlib" OFF )
option( SpecUtils_USE_WT_THREADPOOL "Whether or not to use the Wt thread pool (only for use within InterSpec)" OFF )
option( SpecUtils_USING_NO_THREADING "Whether to disable multithreading" OFF )
option( SpecUtils_BUILD_UNIT_TESTS "Builds unit tests" OFF )
option( SpecUtils_BUILD_FUZZING_TESTS "Builds fuzzing tests, requires clang" OFF )
option( SpecUtils_BUILD_REGRESSION_TEST "Creates executable to perform interactive regression test" OFF )
option( SpecUtils_BUILD_EXAMPLE "Builds example SpecUtil applications" OFF )
option( SpecUtils_PYTHON_BINDINGS "Creates python bindings to the c++ code" OFF )
option( SpecUtils_JAVA_SWIG "Creates swig/java bindings to the c++ code" OFF )
option( SpecUtils_FORTRAN_SWIG "Enables build/test of swig/FORTRAN bindings" OFF )
option( SpecUtils_FORTRAN_SWIG_GEN "When this is on, cmake will attemp to generate swig fortran bindings. 
                                    Requires swig-fortran: https://github.com/swig-fortran/swig" OFF )
option( SpecUtils_C_BINDINGS "Creates C bindings to the c++ code" OFF )
option( SpecUtils_INJA_TEMPLATES "Creates inja template interface" OFF )
option( SpecUtils_USE_SIMD "Use SIMD operations; i386/x64 only right now, and very alpha, and extremely minimally used" OFF )
option( SpecUtils_ENABLE_EQUALITY_CHECKS "Enables the equal_enough(...) functions for comparing two spectrum files." OFF )  #code size is only reason to default to off, I think
option( PERFORM_DEVELOPER_CHECKS "Performs additional computationally expensive tests during execution (requires linking to boost)" OFF )


if( SpecUtils_PYTHON_BINDINGS )
  option( SpecUtils_SHARED_LIB "Whether to compile a shared, or static library" ON )
  list( APPEND sources bindings/python/SpecFile_py.cpp )
else()
  #default to building a static lib
  option( SpecUtils_SHARED_LIB "Whether to compile a shared, or static library" OFF )
endif()


set( SpecUtils_FLT_PARSE_METHOD "default_value" CACHE STRING [[How to parse lists of numbers.
Choose one of: FastFloat, FromChars, boost, strtod.
Boost is fastest, FromChars requires a C++17 conformant compiler and runtime (note Apple clang does not implement),
FastFloat requires having the header somewhere findable (or Fetching it), and strod is the slowest.
]])
set_property( CACHE SpecUtils_FLT_PARSE_METHOD PROPERTY STRINGS "FastFloat" "FromChars" "boost" "strtod" )


# Determine how we should parse floats/ints - we'll prefer boost, if it has been found,
# or else we'll look for `fast_float` if its around, then if the compiler supports
#   `std::from_chars`, we'll use that.  Otherwise we'll default back to fast_floats
#   and semi-offer to "Fetch" it.
if( (NOT DEFAULT_NUMBER_PARSE_METHOD) AND Boost_FOUND )
  set( DEFAULT_NUMBER_PARSE_METHOD "boost" )
endif( (NOT DEFAULT_NUMBER_PARSE_METHOD) AND Boost_FOUND )

if( NOT DEFAULT_NUMBER_PARSE_METHOD )
  find_file(FAST_FLOAT_FILE_PATH fast_float.h HINTS ${CMAKE_PREFIX_PATH} ${CMAKE_PREFIX_PATH}/include )
  if( FAST_FLOAT_FILE_PATH )
    set( DEFAULT_NUMBER_PARSE_METHOD "FastFloat" )
  else( FAST_FLOAT_FILE_PATH )
    if( (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
          AND (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 12.0) )
      set( DEFAULT_NUMBER_PARSE_METHOD "FromChars" )
    elseif( (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
          AND NOT (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "AppleClang")
          AND (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16.0) )
      set( DEFAULT_NUMBER_PARSE_METHOD "FromChars" )
    elseif( MSVC AND (MSVC_VERSION VERSION_GREATER_EQUAL 1920) )
      set( DEFAULT_NUMBER_PARSE_METHOD "FromChars" )
    elseif( (CMAKE_CXX_COMPILER_ID STREQUAL "Intel")
          AND (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 19.0) )
      set( DEFAULT_NUMBER_PARSE_METHOD "FromChars" )
    endif()
  endif( FAST_FLOAT_FILE_PATH )
endif( NOT DEFAULT_NUMBER_PARSE_METHOD )

if( (NOT DEFINED SpecUtils_FLT_PARSE_METHOD OR SpecUtils_FLT_PARSE_METHOD STREQUAL "default_value") 
    AND DEFAULT_NUMBER_PARSE_METHOD )
    set(SpecUtils_FLT_PARSE_METHOD ${DEFAULT_NUMBER_PARSE_METHOD} CACHE STRING "Choose an option" FORCE)
endif()



# If we are building this on Windows, not as a sub-project, lets enable selecting C++ runtime
if( MSVC )
  get_directory_property(hasParent PARENT_DIRECTORY)
  if(NOT hasParent)
    option(${PROJECT_NAME}_USE_MSVC_MultiThreadDLL "Use dynamically-link runtime library." OFF)

    if( ${PROJECT_NAME}_USE_MSVC_MultiThreadDLL)
      set(Boost_USE_STATIC_RUNTIME OFF)
      set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    else()
      set(Boost_USE_STATIC_RUNTIME ON)
      set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    message(STATUS "Using CMAKE_MSVC_RUNTIME_LIBRARY ${CMAKE_MSVC_RUNTIME_LIBRARY}")
  endif(NOT hasParent)
endif( MSVC )


if( SpecUtils_BUILD_EXAMPLE )
  add_subdirectory( examples )
endif( SpecUtils_BUILD_EXAMPLE )

if( SpecUtils_JAVA_SWIG )
  FIND_PACKAGE( SWIG REQUIRED )
  FIND_PACKAGE( JNI REQUIRED )
  INCLUDE( ${SWIG_USE_FILE} )
  INCLUDE_DIRECTORIES( ${JAVA_INCLUDE_PATH} )
  INCLUDE_DIRECTORIES( ${JAVA_INCLUDE_PATH}/linux )
  INCLUDE_DIRECTORIES( ${CMAKE_CURRENT_SOURCE_DIR}/bindings/swig )
  SET( CMAKE_SWIG_FLAGS -I${CMAKE_CURRENT_SOURCE_DIR} -I${CMAKE_CURRENT_BINARY_DIR} -DSpecUtils_JAVA_SWIG=1 -package gov.sandia.specutils )
  SET( CMAKE_SWIG_OUTDIR gov/sandia/specutils )
  SET_SOURCE_FILES_PROPERTIES(bindings/swig/SpecUtils.i PROPERTIES CPLUSPLUS ON)
  SET_SOURCE_FILES_PROPERTIES(bindings/swig/SpecUtils.i PROPERTIES SWIG_FLAGS "")
  SWIG_ADD_MODULE( SpecUtilsJni java bindings/swig/SpecUtils.i )
  SWIG_LINK_LIBRARIES( SpecUtilsJni ${JAVA_LIBRARIES} SpecUtils )
endif( SpecUtils_JAVA_SWIG )


set( THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty" )

if( SpecUtils_PYTHON_BINDINGS )
  FIND_PACKAGE(Python3 COMPONENTS Development )
  IF(NOT Python3_FOUND)
    MESSAGE(FATAL_ERROR "Unable to find PythonLibs.")
  ENDIF()
  message( "Python include dirs: ${Python3_INCLUDE_DIRS}")
endif()

set( sources
     src/SpecFile.cpp
     src/SpecFile_pcf.cpp
     src/SpecFile_cnf.cpp
     src/SpecFile_n42.cpp
     src/SpecFile_spc.cpp
     src/SpecFile_chn.cpp
     src/SpecFile_spe.cpp
     src/SpecFile_csv.cpp
     src/SpecFile_gr135.cpp
     src/SpecFile_aram.cpp
     src/SpecFile_lis.cpp
     src/SpecFile_lzs.cpp
     src/SpecFile_phd.cpp
     src/SpecFile_tka.cpp
     src/SpecFile_lsrm.cpp
     src/SpecFile_mca.cpp
     src/SpecFile_spmdf.cpp
     src/SpecFile_mps.cpp
     src/SpecFile_multiact.cpp
     src/SpecFile_uraider.cpp
     src/SpecFile_radiacode.cpp
     src/SpecFile_xml_other.cpp
     src/SpecFile_json.cpp
     src/SpecUtilsAsync.cpp
     src/SerialToDetectorModel.cpp
     src/EnergyCalibration.cpp
     src/CubicSpline.cpp
     src/StringAlgo.cpp
     src/Filesystem.cpp
     src/DateTime.cpp
     src/ParseUtils.cpp
     src/SpecFile_location.cpp
     README.md
)

set( headers
     SpecUtils/SpecFile.h
     SpecUtils/SpecUtilsAsync.h
     SpecUtils/SpecUtils_config.h.in
     SpecUtils/SerialToDetectorModel.h
     SpecUtils/EnergyCalibration.h
     SpecUtils/CubicSpline.h
     SpecUtils/StringAlgo.h
     SpecUtils/Filesystem.h
     SpecUtils/DateTime.h
     SpecUtils/ParseUtils.h
     SpecUtils/SpecFile_location.h
     SpecUtils/RapidXmlUtils.hpp
)

if( SpecUtils_INJA_TEMPLATES )
  list( APPEND sources src/SpecFile_template.cpp )
endif ( SpecUtils_INJA_TEMPLATES )

if( SpecUtils_ENABLE_D3_CHART )
  list( APPEND sources src/D3SpectrumExport.cpp )
  list( APPEND headers SpecUtils/D3SpectrumExport.h
                       SpecUtils/D3SpectrumExportResources.h.in )


# IMPORTANT: These 2 lists below must be of same length, corresponding with each index
#   For outputting necessary library in D3 HTML files
  set( D3_SUPPORT_FILES
       d3.v3.min.js
       SpectrumChartD3.js
       SpectrumChartD3.css
  )

  set( D3_SUPPORT_FILE_MACROS
       D3_MIN_JS
       SPECTRUM_CHART_D3_JS
       SPECTRUM_CHART_D3_CSS
  )

# https://stackoverflow.com/questions/7932205/parallel-iteration-over-lists-in-makefile-or-cmake-file
  list(LENGTH D3_SUPPORT_FILES D3_SUPPORT_FILES_LENGTH)
  math(EXPR D3_SUPPORT_FILES_LAST_INDEX "${D3_SUPPORT_FILES_LENGTH} - 1")

# Set the corresponding file content for each library file
  foreach(index RANGE ${D3_SUPPORT_FILES_LAST_INDEX})
    list(GET D3_SUPPORT_FILES ${index} D3_SUPPORT_FILE_NAME)
    list(GET D3_SUPPORT_FILE_MACROS ${index} D3_SUPPORT_FILE_MACRO_NAME)

    if( SpecUtils_D3_SUPPORT_FILE_STATIC )
      #Read file into hex array reads source file contents as hex string
      file(READ "d3_resources/${D3_SUPPORT_FILE_NAME}" hexString HEX)
      string(LENGTH ${hexString} hexStringLength)
      set(hexString "${hexString}00")
      math(EXPR arraySize "${hexStringLength} / 2")

      # adds '0x' prefix and comma suffix before and after every byte respectively
      string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " arrayValues ${hexString})
      # removes trailing comma
      string(REGEX REPLACE ", $" "" arrayValues ${arrayValues})

      # declares byte array and the length variables
      set( ${D3_SUPPORT_FILE_MACRO_NAME}_ARR "const unsigned char ${D3_SUPPORT_FILE_MACRO_NAME}[] = { ${arrayValues} };")
      set( ${D3_SUPPORT_FILE_MACRO_NAME}_LEN "const size_t ${D3_SUPPORT_FILE_MACRO_NAME}_SIZE = ${arraySize};")
    else( SpecUtils_D3_SUPPORT_FILE_STATIC )
       set( SpecUtils_D3_SCRIPTS_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE STRING "Directory to read D3 JS and CSS files from at runtime" )
       set( D3_SUPPORT_FILE_RUNTIME_DIR "const char * const D3_SCRIPT_RUNTIME_DIR = \"${SpecUtils_D3_SCRIPTS_RUNTIME_DIR}\";" )
       set( ${D3_SUPPORT_FILE_MACRO_NAME}_FILE "const char * const ${D3_SUPPORT_FILE_MACRO_NAME}_FILENAME = \"${D3_SUPPORT_FILE_NAME}\";" )
    endif( SpecUtils_D3_SUPPORT_FILE_STATIC )
  endforeach()

#We will create D3SpectrumExportResources.h that holds all the js/css, but this
#  is only done at CMake generation time, not build time, so can get out of date
#  See https://cmake.org/pipermail/cmake/2012-May/050221.html for a potential fix
  configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/SpecUtils/D3SpectrumExportResources.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/D3SpectrumExportResources.h
  )

  list( APPEND OTHER_SUPPORT_FILES
        d3_resources/SpectrumChartD3.js
        d3_resources/SpectrumChartD3.css
  )
endif( SpecUtils_ENABLE_D3_CHART )

if( SpecUtils_ENABLE_URI_SPECTRA )
  list( APPEND headers SpecUtils/UriSpectrum.h )
  list( APPEND sources src/UriSpectrum.cpp src/SpecFile_uri.cpp )
  #set( ZLIB_USE_STATIC_LIBS "ON" )
  find_package( ZLIB REQUIRED )
endif( SpecUtils_ENABLE_URI_SPECTRA )

if( SpecUtils_PYTHON_BINDINGS )
  list( APPEND sources bindings/python/SpecFile_py.cpp )
endif( SpecUtils_PYTHON_BINDINGS )

if( SpecUtils_JAVA_SWIG )
  list( APPEND sources bindings/swig/SpecUtils.i )
endif( SpecUtils_JAVA_SWIG )

if( SpecUtils_C_BINDINGS )
  list( APPEND headers bindings/c/SpecUtils_c.h )
  list( APPEND sources bindings/c/SpecUtils_c.cpp )
endif( SpecUtils_C_BINDINGS )

if( SpecUtils_SHARED_LIB )
  set( SpecUtils_LIB_TYPE SHARED )
else( SpecUtils_SHARED_LIB )
  set( SpecUtils_LIB_TYPE STATIC )
endif( SpecUtils_SHARED_LIB )

add_library( SpecUtils ${SpecUtils_LIB_TYPE} ${sources} ${headers} ${OTHER_SUPPORT_FILES} )
set_target_properties( SpecUtils PROPERTIES PREFIX "lib" OUTPUT_NAME "SpecUtils" )


# Include the GenerateExportHeader module
include(GenerateExportHeader)
generate_export_header(SpecUtils
    BASE_NAME SpecUtils
    EXPORT_FILE_NAME ${CMAKE_BINARY_DIR}/SpecUtilsExport.h
)

if( NOT SpecUtils_SHARED_LIB )
  target_compile_definitions( SpecUtils PRIVATE SPECUTILS_STATIC_DEFINE )
endif()


set( SpecUtils_USE_FAST_FLOAT OFF )   # Uses https://github.com/fastfloat/fast_float .  If fast_float.h isnt found, will be fetched
set( SpecUtils_USE_FROM_CHARS OFF )   # Supported by MSVC >= 2019, and gcc >= 12.  Not supported by Apple clang. In MSVC, about 50% slower than boost::spirit
set( SpecUtils_USE_BOOST_SPIRIT OFF ) # Uses boost::spirit, and fasted method (maybe a hair faster than fastfloat)
set( SpecUtils_USE_STRTOD OFF )       # Slowest method, but works

if(SpecUtils_FLT_PARSE_METHOD STREQUAL "FastFloat")
  set( SpecUtils_USE_FAST_FLOAT ON )
elseif(SpecUtils_FLT_PARSE_METHOD STREQUAL "FromChars")
  set( SpecUtils_USE_FROM_CHARS ON )
elseif(SpecUtils_FLT_PARSE_METHOD STREQUAL "boost")
  set( SpecUtils_USE_BOOST_SPIRIT ON )
elseif(SpecUtils_FLT_PARSE_METHOD STREQUAL "strtod")
  set( SpecUtils_USE_STRTOD ON )
else()
    message(FATAL_ERROR "Invalid value for SpecUtils_FLT_PARSE_METHOD: ${SpecUtils_FLT_PARSE_METHOD}")
endif()

if( SpecUtils_USE_FAST_FLOAT )
  find_file(FAST_FLOAT_FILE_PATH fast_float.h HINTS ${CMAKE_PREFIX_PATH} ${CMAKE_PREFIX_PATH}/include )
 
  if( FAST_FLOAT_FILE_PATH )
    # Extract the parent directory of the found file
    get_filename_component(PARENT_DIR ${FAST_FLOAT_PATH} DIRECTORY)
    
    message(STATUS "fast_float.h found at: ${FAST_FLOAT_PATH}")
    message(STATUS "Parent directory: ${FAST_FLOAT_PATH}")
    
    target_include_directories( SpecUtils PUBLIC ${FAST_FLOAT_PATH} )
  endif( FAST_FLOAT_FILE_PATH )
  
  if( NOT FAST_FLOAT_FILE_PATH )
    if( NOT SpecUtils_FETCH_FAST_FLOAT )
      message( "File fast_float.h not found - set SpecUtils_FETCH_FAST_FLOAT to ON to fetch")
      option( SpecUtils_FETCH_FAST_FLOAT "Fetch fast_float, if cant be found" OFF )
    endif( NOT SpecUtils_FETCH_FAST_FLOAT )
    
    if( SpecUtils_FETCH_FAST_FLOAT )
      # If you need to use a proxy, either configure it in your environment, or
      #  specify a cmake argument like `-Dhttps_proxy=https://proxy.example.com:8080`
      
      include(FetchContent)
      
      FetchContent_Declare(
        fast_float
        URL https://github.com/fastfloat/fast_float/releases/download/v6.1.4/fast_float.h
        URL_HASH SHA256=9567742ed302c6a5ff0000beaba3d84f45b31b5f296c3bf3f26a442c6f01f129
        DOWNLOAD_NO_EXTRACT TRUE
      )

      FetchContent_GetProperties( fast_float )
      FetchContent_MakeAvailable( fast_float )
      
      target_include_directories( SpecUtils PRIVATE ${fast_float_SOURCE_DIR} )
    endif( SpecUtils_FETCH_FAST_FLOAT )
  endif( NOT FAST_FLOAT_FILE_PATH )
endif( SpecUtils_USE_FAST_FLOAT )

# We only need boost libraries if we are using boost to parse floats, if we are performing developer
#  checks, or we are creating Python bindings
if( SpecUtils_USE_BOOST_SPIRIT OR PERFORM_DEVELOPER_CHECKS OR SpecUtils_PYTHON_BINDINGS )
  
  if( DEFINED boost_SOURCE_DIR )
    # We used CMake Fetch to get boost (e.g., in InterSpec)
    target_link_libraries( SpecUtils PUBLIC Boost::system Boost::spirit Boost::fusion Boost::algorithm Boost::functional Boost::crc )
  
  else( DEFINED boost_SOURCE_DIR )
  
    if( SpecUtils_PYTHON_BINDINGS )
      list( APPEND boost_libs_required python )
    endif( SpecUtils_PYTHON_BINDINGS )
  
    find_package( Boost REQUIRED COMPONENTS system ${boost_libs_required} )
    if( NOT Boost_FOUND )
      message(FATAL_ERROR "Couldnt Find Boost")
    endif( NOT Boost_FOUND )
    
    target_link_libraries( SpecUtils PUBLIC Boost::system )
  endif( DEFINED boost_SOURCE_DIR )
endif( SpecUtils_USE_BOOST_SPIRIT OR PERFORM_DEVELOPER_CHECKS OR SpecUtils_PYTHON_BINDINGS )

if( SpecUtils_USE_FROM_CHARS )
  if( NOT (MSVC AND (MSVC_VERSION GREATER_EQUAL 1920)) )
    # MSVC >=2019 supports floating point from_chars, while Xcode 14 still doesnt (havent checked Android Studio)
    message("Please double check your compiler does actually support std::from_chars for parsing floats")
  endif()

  set_target_properties( SpecUtils PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
  )
else(SpecUtils_USE_FROM_CHARS)
  set_target_properties( SpecUtils PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
  )

  #For CMake 3.8+, will specify following so it will be propagated to down-line targets
  # This seems duplicate, but needed when compiling with SWIG
  target_compile_features(SpecUtils PUBLIC cxx_std_11)
  set_target_properties(SpecUtils PROPERTIES CXX_EXTENSIONS OFF)
endif(SpecUtils_USE_FROM_CHARS)


if( SpecUtils_USE_WT_THREADPOOL )
  if( DEFINED wt_SOURCE_DIR )
    target_include_directories( SpecUtils PUBLIC "${wt_SOURCE_DIR}/src" "${wt_BINARY_DIR}" )
    
    # We'll need to pick up all the includes Wt/WApplication will require
    target_link_libraries( SpecUtils PUBLIC Boost::any Boost::asio Boost::signals2 )
  else( DEFINED wt_SOURCE_DIR )
    if( NOT Wt_INCLUDE_DIR OR NOT Wt_FOUND )
      message( FATAL "Wt not found; either find before adding SpecUtils, or set SpecUtils_USE_WT_THREADPOOL OFF" )
    else( NOT Wt_INCLUDE_DIR OR NOT Wt_FOUND )
      target_include_directories( SpecUtils PUBLIC ${Wt_INCLUDE_DIR} )
    endif( NOT Wt_INCLUDE_DIR OR NOT Wt_FOUND )
  endif( DEFINED wt_SOURCE_DIR )
elseif( NOT SpecUtils_USING_NO_THREADING )
  find_package(Threads REQUIRED)
  target_link_libraries( SpecUtils PUBLIC Threads::Threads )
endif( SpecUtils_USE_WT_THREADPOOL )



if( SpecUtils_ENABLE_URI_SPECTRA )
  target_link_libraries( SpecUtils PUBLIC ZLIB::ZLIB )
endif( SpecUtils_ENABLE_URI_SPECTRA )


if( SpecUtils_PYTHON_BINDINGS )
  target_link_libraries( SpecUtils PUBLIC Boost::python ${Boost_PYTHON_LIBRARY} ${Python3_LIBRARIES} )
endif( SpecUtils_PYTHON_BINDINGS )


if( SpecUtils_JAVA_SWIG )
  target_link_libraries( SpecUtils PUBLIC ${JAVA_LIBRARIES} )
endif( SpecUtils_JAVA_SWIG )

if( SpecUtils_FORTRAN_SWIG )
  add_subdirectory(bindings/swig/fortran)
endif( SpecUtils_FORTRAN_SWIG )

if( MINGW )
  target_link_libraries( SpecUtils PUBLIC -static-libgcc -static libshlwapi.a libpthread.a libstdc++.a libwinpthread.a libmsvcrt.a )
elseif( WIN32 )
  #Pathcch.lib is for PathCchCanonicalizeEx, and is available only on Windows 8 and newer, Desktop and UWP
  #Shlwapi.lib is desktop Windows only, XP and newer
  #target_link_libraries( SpecUtils PUBLIC "Pathcch.lib" "Shlwapi.lib" )
  target_link_libraries( SpecUtils PUBLIC "Shlwapi.lib" )
endif( MINGW )

target_include_directories( SpecUtils PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}> 
                            PRIVATE ${THIRD_PARTY_DIR} ${PYTHON_INCLUDE_DIRS} )

if( SpecUtils_BUILD_REGRESSION_TEST )
  if( NOT SpecUtils_ENABLE_EQUALITY_CHECKS )
    message(FATAL_ERROR "You must enable SpecUtils_ENABLE_EQUALITY_CHECKS to build regression test.")
  endif( NOT SpecUtils_ENABLE_EQUALITY_CHECKS )
  
  add_subdirectory( regression_test )
endif( SpecUtils_BUILD_REGRESSION_TEST )


if( SpecUtils_BUILD_UNIT_TESTS )
  enable_testing()  # Enable here to allow running `ctest` from top-most build dir

  add_subdirectory( unit_tests )
endif( SpecUtils_BUILD_UNIT_TESTS )


if( SpecUtils_BUILD_FUZZING_TESTS )
  add_subdirectory( fuzz_test )
endif( SpecUtils_BUILD_FUZZING_TESTS )

set_property( TARGET SpecUtils PROPERTY POSITION_INDEPENDENT_CODE ON )

# Specify installation rules
include(GNUInstallDirs)


# Install the library
install(TARGETS SpecUtils
    EXPORT SpecUtilsTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}  # For shared libraries on UNIX-like systems
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}  # For static libraries and import libraries on Windows
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}  # For DLLs
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

set( SpecUtils_config_OUT ${CMAKE_CURRENT_BINARY_DIR}/SpecUtils_config.h )
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/SpecUtils/SpecUtils_config.h.in
    ${SpecUtils_config_OUT}
)
LIST(APPEND headers ${SpecUtils_config_OUT})
# Install the headers
install(FILES ${headers} ${CMAKE_BINARY_DIR}/SpecUtilsExport.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/SpecUtils)

# Create and install the CMake package configuration files
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/SpecUtilsConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/SpecUtilsConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SpecUtilsConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SpecUtils
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/SpecUtilsConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/SpecUtilsConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SpecUtils
)

install(EXPORT SpecUtilsTargets
    FILE SpecUtilsTargets.cmake
    NAMESPACE SpecUtils::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SpecUtils
)

set(CPACK_PACKAGE_NAME "SpecUtils")


if( SpecUtils_SHARED_LIB )
  set( SpecUtils_PACKAGE_POSTFIX "SharedLib" )
else( SpecUtils_SHARED_LIB )
  set( SpecUtils_PACKAGE_POSTFIX "StaticLib" )
endif( SpecUtils_SHARED_LIB )

if( DEFINED ${PROJECT_NAME}_USE_MSVC_MultiThreadDLL )
  if( ${PROJECT_NAME}_USE_MSVC_MultiThreadDLL )
    set( SpecUtils_PACKAGE_POSTFIX "${SpecUtils_PACKAGE_POSTFIX}-SharedRuntime" )
  else()
    set( SpecUtils_PACKAGE_POSTFIX "${SpecUtils_PACKAGE_POSTFIX}-StaticRuntime" )
  endif()
endif()

# Lets just use the MAJOR.MINOR version of the compiler in the name.
string(REGEX MATCH "^[0-9]+\\.[0-9]+" COMPILER_VERSION_SHORT "${CMAKE_CXX_COMPILER_VERSION}")

set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CMAKE_SYSTEM_NAME}-${CMAKE_CXX_COMPILER_ID}-${COMPILER_VERSION_SHORT}-${SpecUtils_PACKAGE_POSTFIX}")
set(CPACK_GENERATOR "ZIP")
include(CPack)
