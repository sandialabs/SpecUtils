cmake_policy(SET CMP0048 NEW)
project(SpecUtilsTests VERSION 1)

cmake_minimum_required(VERSION VERSION 3.1...3.29 FATAL_ERROR)

option( ADD_TEST_SPEC_DIR "Base Directory containing other directories that have valid spectrum files for test opening" "" )
set( testdir "${CMAKE_CURRENT_SOURCE_DIR}" )
set( test_data_dir ${CMAKE_CURRENT_SOURCE_DIR}/test_data )


# Set the CWD to the build folder, for Xcode at least
if( BUILD_AS_LOCAL_SERVER AND (CMAKE_GENERATOR STREQUAL "Xcode") )
    # Setting Xcode working directory requires cmake 3.17
    if( ${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.17.0" )
        set_target_properties(InterSpecExe PROPERTIES
            XCODE_GENERATE_SCHEME TRUE
            XCODE_SCHEME_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        )
    endif( ${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.17.0" )
endif( BUILD_AS_LOCAL_SERVER AND (CMAKE_GENERATOR STREQUAL "Xcode") )


find_package( Boost REQUIRED COMPONENTS program_options date_time system filesystem )

if( NOT Boost_FOUND )
  message(FATAL_ERROR "Couldnt Find Boost")
endif( NOT Boost_FOUND )

# Set some command line arguments that all/most of the tests will use
set( COMMON_TEST_ARGS --log_level=test_suite --report_level=detailed --catch_system_errors=yes )
set( DATA_DIR_ARGS --indir=${CMAKE_CURRENT_SOURCE_DIR}/test_data )


if( CMAKE_GENERATOR STREQUAL "Xcode" )
  list( APPEND COMMON_TEST_ARGS --no-colors=1 )
endif( CMAKE_GENERATOR STREQUAL "Xcode" )
message( "Args: ${COMMON_TEST_ARGS}")

add_executable( test_energy_calibration test_energy_calibration.cpp )
target_link_libraries( test_energy_calibration PRIVATE SpecUtils )
set_target_properties( test_energy_calibration PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
add_test( NAME TestEnergyCalibration
          COMMAND $<TARGET_FILE:test_energy_calibration> ${COMMON_TEST_ARGS} -- ${DATA_DIR_ARGS}
)


add_executable( test_spec_file_open test_spec_file_open.cpp )
target_link_libraries( test_spec_file_open PRIVATE SpecUtils )
set_target_properties( test_spec_file_open PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
FILE(GLOB children RELATIVE ${ADD_TEST_SPEC_DIR} ${ADD_TEST_SPEC_DIR}/*)
FOREACH(child ${children})
  SET( filetestdir ${curdir}/${child} )
  IF(IS_DIRECTORY ${filetestdir})
    #uhg, if we dont get rid of spaces in the file name, it gets interprested as a
    #  separate command line argument - even if we escape it with quotes

    # HB - this isn't running for me. 
    string(REPLACE " " "%20" filetestdir ${filetestdir} )
    message( ${filetestdir} )
    add_test( NAME "\"Test open ${child}\""
              COMMAND $<TARGET_FILE:test_spec_file_open> ${COMMON_TEST_ARGS} -- ${DATA_DIR_ARGS} )
  ENDIF()
ENDFOREACH()


add_executable( test_iso_time_string test_iso_time_string.cpp )
target_link_libraries( test_iso_time_string PRIVATE SpecUtils Boost::date_time)
set_target_properties( test_iso_time_string PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
add_test(
  NAME TestIsoTimeString
  COMMAND $<TARGET_FILE:test_iso_time_string> ${COMMON_TEST_ARGS} -- ${DATA_DIR_ARGS}
  WORKING_DIRECTORY ${test_data_dir}
)

add_executable( test_safe_get_line test_safe_get_line.cpp )
target_link_libraries( test_safe_get_line PRIVATE SpecUtils )
set_target_properties( test_safe_get_line PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
add_test(
  NAME TestSafeGetLine
  COMMAND $<TARGET_FILE:test_safe_get_line> ${COMMON_TEST_ARGS} -- ${DATA_DIR_ARGS}
  WORKING_DIRECTORY ${test_data_dir}
)


add_executable( test_time_from_string test_time_from_string.cpp )
target_link_libraries( test_time_from_string PRIVATE SpecUtils )
set_target_properties( test_time_from_string PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
add_test(
  NAME TestTimeFromString
  COMMAND $<TARGET_FILE:test_time_from_string> ${COMMON_TEST_ARGS} -- ${DATA_DIR_ARGS}
  WORKING_DIRECTORY ${test_data_dir}
)


add_executable( test_string_functions test_string_functions.cpp )
target_link_libraries( test_string_functions PRIVATE SpecUtils )
set_target_properties( test_string_functions PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
add_test(
  NAME TestUtilityStringFunctions
  COMMAND $<TARGET_FILE:test_string_functions> ${COMMON_TEST_ARGS} -- ${DATA_DIR_ARGS}
  WORKING_DIRECTORY ${test_data_dir}
)


add_executable( test_split_to_floats_and_ints test_split_to_floats_and_ints.cpp )
target_link_libraries( test_split_to_floats_and_ints PRIVATE SpecUtils )
set_target_properties( test_split_to_floats_and_ints PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
add_test(
  NAME TestSplitToFloats
  COMMAND $<TARGET_FILE:test_split_to_floats_and_ints> ${COMMON_TEST_ARGS} -- ${DATA_DIR_ARGS}
  WORKING_DIRECTORY ${test_data_dir}
)


add_executable( test_utf8_limit_str_size test_utf8_limit_str_size.cpp )
target_link_libraries( test_utf8_limit_str_size PRIVATE SpecUtils )
set_target_properties( test_utf8_limit_str_size PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
add_test(
  NAME TestUtf8LimitStrSize
  COMMAND $<TARGET_FILE:test_utf8_limit_str_size> ${COMMON_TEST_ARGS} -- ${DATA_DIR_ARGS}
  WORKING_DIRECTORY ${test_data_dir}
)


add_executable( test_utf8_str_len test_utf8_str_len.cpp )
target_link_libraries( test_utf8_str_len PRIVATE SpecUtils )
set_target_properties( test_utf8_str_len PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
add_test(
  NAME TestUtf8StrLen
  COMMAND $<TARGET_FILE:test_utf8_str_len> ${COMMON_TEST_ARGS} -- ${DATA_DIR_ARGS}
  WORKING_DIRECTORY ${test_data_dir}
)

add_executable( test_cubic_spline test_cubic_spline.cpp )
target_link_libraries( test_cubic_spline PRIVATE SpecUtils )
set_target_properties( test_cubic_spline PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
add_test(
  NAME TestCubicSpline
  COMMAND $<TARGET_FILE:test_cubic_spline> ${COMMON_TEST_ARGS} -- ${DATA_DIR_ARGS}
  WORKING_DIRECTORY ${test_data_dir}
)


add_executable( test_filesystem_functions test_filesystem_functions.cpp )
target_link_libraries( test_filesystem_functions PRIVATE SpecUtils Boost::filesystem )
set_target_properties( test_filesystem_functions PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
add_test(
  NAME testUtilityFilesystemFunctions
  COMMAND $<TARGET_FILE:test_filesystem_functions> ${COMMON_TEST_ARGS} -- ${DATA_DIR_ARGS}
)

add_executable( test_rebin_by_lower_energy test_rebin_by_lower_energy.cpp )
target_link_libraries( test_rebin_by_lower_energy PRIVATE SpecUtils )
set_target_properties( test_rebin_by_lower_energy PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
add_test(
  NAME TestRebinByLowerEnergy
  COMMAND $<TARGET_FILE:test_rebin_by_lower_energy> ${COMMON_TEST_ARGS} -- ${DATA_DIR_ARGS}
)

if( SpecUtils_ENABLE_URI_SPECTRA )
  add_executable( test_uri_spectrum test_uri_spectrum.cpp )
  target_link_libraries( test_uri_spectrum PRIVATE SpecUtils )
  set_target_properties( test_uri_spectrum PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
  add_test(
    NAME test_uri_spectrum
    COMMAND $<TARGET_FILE:test_uri_spectrum> ${COMMON_TEST_ARGS} -- ${DATA_DIR_ARGS}
  )
endif()
