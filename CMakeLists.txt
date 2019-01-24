cmake_minimum_required(VERSION 3.1...3.13)

if(${CMAKE_VERSION} VERSION_LESS 3.12)
  cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
endif()

project( SpecUtils )

option( SpecUtils_ENABLE_D3_CHART "Enables exporting of the D3 chart format" ON )
option( SpecUtils_D3_SUPPORT_FILE_STATIC "Compiles the JS and CSS files into memory, rather than reading files on disk; disable to allow changing JS/CSS without having to recompile" ON )
option( SpecUtils_NO_BOOST_LIB "Feature under development to avoid having to link to boost (however, boost headers still necassary)." OFF )
option( SpecUtils_USE_WT_THREADPOOL "Whether or not to use the Wt thread pool (only for use within InterSpec)" OFF )
option( SpecUtils_USING_NO_THREADING "Whether to disable multithreading" OFF )
option( SpecUtils_REBIN_FILES_TO_SINGLE_BINNING "Rebins all spectra in spectrum file to the same energy calibration" OFF )
option( SpecUtils_BUILD_TEST "Creates executuable to perform interactive regression test" OFF )
option( SpecUtils_BUILD_EXAMPLE "Builds example SpecUtil applications" OFF )
option( SpecUtils_PYTHON_BINDINGS "Creates python bindings to the c++ code" OFF )
option( SpecUtils_JAVA_SWIG "Creates swig/java bindings to the c++ code" OFF )

#This is a temporary (20170629) hack to deal with error messages... should be handled a lot better...
option( SpecUtils_EXTERNALLY_DEFINED_LOG_MESSAGE "If true, you must have a externally defined log_error_message(string,string,int) function defined when linking" OFF )

option( PERFORM_DEVELOPER_CHECKS "Performs additional computationally expensive tests during execution" off )

#I cant quite seem to get rpath stuff to work on macOS for regression_test and
# python targets when using boost in a non-standard location, so instead just
# statically link... sorry!
option( TRY_TO_STATIC_LINK "Try to link to static libs whenever possible" OFF )
if( TRY_TO_STATIC_LINK )
  set( Boost_USE_STATIC_LIBS ON )
  if( WIN32 )
    set(CMAKE_FIND_LIBRARY_SUFFIXES .lib .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
  else( WIN32 )
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
  endif( WIN32 )
else( TRY_TO_STATIC_LINK )
  set( Boost_USE_STATIC_LIBS OFF )
  set( CMAKE_FIND_LIBRARY_SUFFIXES .so .dylib ${CMAKE_FIND_LIBRARY_SUFFIXES} )
endif( TRY_TO_STATIC_LINK )

list( APPEND boost_libs_required date_time system filesystem )

if( SpecUtils_BUILD_TEST )
  list( APPEND boost_libs_required program_options )
endif( SpecUtils_BUILD_TEST )

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


set( RAPID_XML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty" )
#find_path( RAPID_XML_DIR rapidxml/rapidxml.hpp ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty )


if( SpecUtils_PYTHON_BINDINGS )
  FIND_PACKAGE(PythonLibs 2.7)
  IF(NOT PYTHONLIBS_FOUND)
    MESSAGE(FATAL_ERROR "Unable to find PythonLibs.")
  ENDIF()
endif( SpecUtils_PYTHON_BINDINGS )

set( sources
     src/SpectrumDataStructs.cpp
     src/UtilityFunctions.cpp
     src/SpecUtilsAsync.cpp
     src/SerialToDetectorModel.cpp
     README.md
)

set( headers
     SpecUtils/SpectrumDataStructs.h
     SpecUtils/UtilityFunctions.h
     SpecUtils/SpecUtilsAsync.h
     SpecUtils/SpecUtils_config.h.in
     SpecUtils/SerialToDetectorModel.h
)


#check if we have CNF implementation files; if so use them
file( GLOB SpecUtils_CNF_IMP_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/*_cnf_imp.inc" )
if( SpecUtils_CNF_IMP_FILE )
  set( SpecUtils_HAS_CNF_IMP_FILE ON )
else()
  set( SpecUtils_HAS_CNF_IMP_FILE OFF )
endif()



if( SpecUtils_ENABLE_D3_CHART )
  list( APPEND sources src/D3SpectrumExport.cpp )
  list( APPEND headers SpecUtils/D3SpectrumExport.h
                       SpecUtils/D3SpectrumExportResources.h.in )


# IMPORTANT: These 2 lists below must be of same length, corresponding with each index
#   For outputting necessary library in D3 HTML files
  set( D3_SUPPORT_FILES
       d3_resources/d3.v3.min.js
       d3_resources/c.min.js
       d3_resources/SpectrumChartD3.js
       d3_resources/SpectrumChartD3.css
  )

  set( D3_SUPPORT_FILE_MACROS
       D3_MIN_JS
       CASSOWARY_MIN_JS
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
      file(READ ${D3_SUPPORT_FILE_NAME} hexString HEX)
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
      set( ${D3_SUPPORT_FILE_MACRO_NAME}_FILE "const char * const ${D3_SUPPORT_FILE_MACRO_NAME}_FILENAME = \"${CMAKE_CURRENT_SOURCE_DIR}/${D3_SUPPORT_FILE_NAME}\";" )
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
        d3_resources/SpectrumChartD3.min.index.js
        d3_resources/SpectrumChartD3.css
  )
endif( SpecUtils_ENABLE_D3_CHART )

#default to building a static lib
set( SpecUtils_LIB_TYPE STATIC )

if( SpecUtils_PYTHON_BINDINGS )
  set( SpecUtils_LIB_TYPE SHARED )
  list( APPEND sources bindings/python/SpectrumDataStructs_py.cpp )
endif( SpecUtils_PYTHON_BINDINGS )

if( SpecUtils_JAVA_SWIG )
  set( SpecUtils_LIB_TYPE SHARED )
  list( APPEND sources bindings/swig/SpecUtils.i )
endif( SpecUtils_JAVA_SWIG )


add_library( SpecUtils ${SpecUtils_LIB_TYPE} ${sources} ${headers} ${OTHER_SUPPORT_FILES} )
set_target_properties( SpecUtils PROPERTIES PREFIX "lib" OUTPUT_NAME "SpecUtils" )
target_compile_options( SpecUtils PRIVATE -DBOOST_FILESYSTEM_VERSION=3 )

set_target_properties( SpecUtils PROPERTIES
  CXX_STANDARD 11
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

#Android Studio has an old version of cmake... gotta accomidate, see
# https://cliutils.gitlab.io/modern-cmake/chapters/packages/Boost.html for an alternative
#target_link_libraries( SpecUtils PUBLIC Boost::date_time Boost::system Boost::filesystem )
target_link_libraries( SpecUtils PUBLIC ${Boost_LIBRARIES} ${Boost_DATE_TIME_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_FILESYSTEM_LIBRARY} )


if( SpecUtils_PYTHON_BINDINGS )
  #target_link_libraries( SpecUtils PUBLIC Boost::python ${PYTHON_LIBRARIES} )
  target_link_libraries( SpecUtils PUBLIC ${Boost_PYTHON_LIBRARY} ${PYTHON_LIBRARIES} )
endif( SpecUtils_PYTHON_BINDINGS )


if( SpecUtils_JAVA_SWIG )
  target_link_libraries( SpecUtils PUBLIC ${JAVA_LIBRARIES} )
endif( SpecUtils_JAVA_SWIG )


target_include_directories( SpecUtils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR} ${Boost_INCLUDE_DIRS}
                            PRIVATE ${RAPID_XML_DIR} ${PYTHON_INCLUDE_DIRS} )

if( SpecUtils_BUILD_TEST )
  if( NOT PERFORM_DEVELOPER_CHECKS )
    message( FATAL_ERROR "PERFORM_DEVELOPER_CHECKS must be enabled when building testing executable" )
  endif( NOT PERFORM_DEVELOPER_CHECKS )

  add_executable( regression_test testing/regression_test.cpp )
  #target_link_libraries( regression_test PRIVATE SpecUtils Boost::program_options )
  target_link_libraries( regression_test PRIVATE SpecUtils ${Boost_PROGRAM_OPTIONS_LIBRARY} )
  set_target_properties( regression_test PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
)
endif( SpecUtils_BUILD_TEST )


configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/SpecUtils/SpecUtils_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/SpecUtils_config.h
)


