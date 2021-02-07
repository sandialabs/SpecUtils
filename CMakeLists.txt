cmake_minimum_required(VERSION 3.1...3.13)

if(${CMAKE_VERSION} VERSION_LESS 3.12)
  cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
endif()

project( SpecUtils )

option( SpecUtils_ENABLE_D3_CHART "Enables exporting of the D3 chart format" ON )
option( SpecUtils_D3_SUPPORT_FILE_STATIC "Compiles the JS and CSS files into memory, rather than reading files on disk; disable to allow changing JS/CSS without having to recompile" ON )
option( SpecUtils_USE_WT_THREADPOOL "Whether or not to use the Wt thread pool (only for use within InterSpec)" OFF )
option( SpecUtils_USING_NO_THREADING "Whether to disable multithreading" OFF )
option( SpecUtils_REBIN_FILES_TO_SINGLE_BINNING "Rebins all spectra in spectrum file to the same energy calibration" OFF )
option( SpecUtils_BUILD_UNIT_TESTS "Builds unit tests" OFF )
option( SpecUtils_BUILD_FUZZING_TESTS "Builds fuzzing tests, requires clang" OFF )
option( SpecUtils_BUILD_REGRESSION_TEST "Creates executuable to perform interactive regression test" OFF )
option( SpecUtils_BUILD_EXAMPLE "Builds example SpecUtil applications" OFF )
option( SpecUtils_PYTHON_BINDINGS "Creates python bindings to the c++ code" OFF )
option( SpecUtils_JAVA_SWIG "Creates swig/java bindings to the c++ code" OFF )
option( SpecUtils_INJA_TEMPLATES "Creates inja template interface" OFF )


option( PERFORM_DEVELOPER_CHECKS "Performs additional computationally expensive tests during execution" off )

#I cant quite seem to get rpath stuff to work on macOS for regression_test and
# python targets when using boost in a non-standard location, so instead just
# statically link... sorry!
option( TRY_TO_STATIC_LINK "Try to link to static libs whenever possible" OFF )
if( TRY_TO_STATIC_LINK )
  set( Boost_USE_STATIC_LIBS ON )
  if( WIN32 )
    #find static libraries first
	set(CMAKE_FIND_LIBRARY_SUFFIXES .lib .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
	
	#Link to static runtime
	set( CMAKE_CXX_FLAGS "/MT ${CMAKE_CXX_FLAGS}" )
  	set(CompilerFlags
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_RELEASE
        CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO
        )
    foreach(CompilerFlag ${CompilerFlags})
        string(REGEX REPLACE "/MD" "/MT" ${CompilerFlag} "${${CompilerFlag}}")
    endforeach()
  else( WIN32 )
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
  endif( WIN32 )
else( TRY_TO_STATIC_LINK )
  set( Boost_USE_STATIC_LIBS OFF )
  set( CMAKE_FIND_LIBRARY_SUFFIXES .so .dylib ${CMAKE_FIND_LIBRARY_SUFFIXES} )
endif( TRY_TO_STATIC_LINK )


if( SpecUtils_BUILD_UNIT_TESTS )
  list( APPEND boost_libs_required unit_test_framework )
endif( SpecUtils_BUILD_UNIT_TESTS )


if( SpecUtils_PYTHON_BINDINGS )
  list( APPEND boost_libs_required python )
endif( SpecUtils_PYTHON_BINDINGS )

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
  SET( CMAKE_SWIG_FLAGS -I${CMAKE_CURRENT_SOURCE_DIR} -I${CMAKE_CURRENT_BINARY_DIR} -DSpecUtils_JAVA_SWIG=1 )
  SET_SOURCE_FILES_PROPERTIES(bindings/swig/SpecUtils.i PROPERTIES CPLUSPLUS ON)
  SET_SOURCE_FILES_PROPERTIES(bindings/swig/SpecUtils.i PROPERTIES SWIG_FLAGS "")
  SWIG_ADD_MODULE( SpecUtilsJni java bindings/swig/SpecUtils.i )
  SWIG_LINK_LIBRARIES( SpecUtilsJni ${JAVA_LIBRARIES} SpecUtils )
endif( SpecUtils_JAVA_SWIG )

find_package( Boost REQUIRED COMPONENTS ${boost_libs_required} )

if( NOT Boost_FOUND )
  message(FATAL_ERROR "Couldnt Find Boost")
endif( NOT Boost_FOUND )


set( THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty" )

if( SpecUtils_PYTHON_BINDINGS )
  FIND_PACKAGE(PythonLibs 2.7)
  IF(NOT PYTHONLIBS_FOUND)
    MESSAGE(FATAL_ERROR "Unable to find PythonLibs.")
  ENDIF()
endif( SpecUtils_PYTHON_BINDINGS )

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
     src/SpecUtilsAsync.cpp
     src/SerialToDetectorModel.cpp
     src/EnergyCalibration.cpp
     src/CubicSpline.cpp
     src/StringAlgo.cpp
     src/Filesystem.cpp
     src/DateTime.cpp
     src/ParseUtils.cpp
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
       SpectrumChartD3StandAlone.css
  )

  set( D3_SUPPORT_FILE_MACROS
       D3_MIN_JS
       SPECTRUM_CHART_D3_JS
       SPECTRUM_CHART_D3_CSS
       SPECTRUM_CHART_D3_STANDALONE_CSS
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
        d3_resources/SpectrumChartD3StandAlone.css
  )
endif( SpecUtils_ENABLE_D3_CHART )

#default to building a static lib
set( SpecUtils_LIB_TYPE STATIC )

if( SpecUtils_PYTHON_BINDINGS )
  set( SpecUtils_LIB_TYPE SHARED )
  list( APPEND sources bindings/python/SpecFile_py.cpp )
endif( SpecUtils_PYTHON_BINDINGS )

if( SpecUtils_JAVA_SWIG )
  set( SpecUtils_LIB_TYPE SHARED )
  list( APPEND sources bindings/swig/SpecUtils.i )
endif( SpecUtils_JAVA_SWIG )

add_library( SpecUtils ${SpecUtils_LIB_TYPE} ${sources} ${headers} ${OTHER_SUPPORT_FILES} )
set_target_properties( SpecUtils PROPERTIES PREFIX "lib" OUTPUT_NAME "SpecUtils" )

#For CMake 3.8+, could specify C++11 using the following so it will be propagated to down-line targets...
#target_compile_features(SpecUtils PUBLIC cxx_std_11)
#set_target_properties(SpecUtils PROPERTIES CXX_EXTENSIONS OFF)

set_target_properties( SpecUtils PROPERTIES
  CXX_STANDARD 11
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)


if( SpecUtils_USE_WT_THREADPOOL )
  include( ${CMAKE_SOURCE_DIR}/cmake/FindWt.cmake)
  target_include_directories( SpecUtils PUBLIC ${Wt_INCLUDE_DIR} )
endif( SpecUtils_USE_WT_THREADPOOL )


if( SpecUtils_PYTHON_BINDINGS )
  #target_link_libraries( SpecUtils PUBLIC Boost::python ${PYTHON_LIBRARIES} )
  target_link_libraries( SpecUtils PUBLIC ${Boost_PYTHON_LIBRARY} ${PYTHON_LIBRARIES} )
endif( SpecUtils_PYTHON_BINDINGS )


if( SpecUtils_JAVA_SWIG )
  target_link_libraries( SpecUtils PUBLIC ${JAVA_LIBRARIES} )
endif( SpecUtils_JAVA_SWIG )


if( MINGW )
  target_link_libraries( SpecUtils PUBLIC -static-libgcc -static libshlwapi.a libpthread.a libstdc++.a libwinpthread.a )
elseif( WIN32 )
target_link_libraries( SpecUtils PUBLIC "Pathcch.lib" "Shlwapi.lib" )
endif( MINGW )

target_include_directories( SpecUtils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR} ${Boost_INCLUDE_DIRS}
                            PRIVATE ${THIRD_PARTY_DIR} ${PYTHON_INCLUDE_DIRS} )

if( SpecUtils_BUILD_REGRESSION_TEST )
  add_subdirectory( regression_test )
endif( SpecUtils_BUILD_REGRESSION_TEST )


IF( SpecUtils_BUILD_UNIT_TESTS )
  add_subdirectory( unit_tests )
ENDIF( SpecUtils_BUILD_UNIT_TESTS )


if( SpecUtils_BUILD_FUZZING_TESTS )
  add_subdirectory( fuzz_test )
endif( SpecUtils_BUILD_FUZZING_TESTS )

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/SpecUtils/SpecUtils_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/SpecUtils_config.h
)


