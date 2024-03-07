cmake_policy(SET CMP0048 NEW)
project(SpecUtilsTests VERSION 1)

cmake_minimum_required(VERSION 3.1.0 FATAL_ERROR)

enable_testing()

option( ADD_TEST_SPEC_DIR "Base Directory containing other directories that have valid spectrum files for test opening" "" )
set( testdir "${CMAKE_CURRENT_SOURCE_DIR}" )


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


find_package( Boost REQUIRED COMPONENTS program_options date_time system filesystem unit_test_framework )

if( NOT Boost_FOUND )
  message(FATAL_ERROR "Couldnt Find Boost")
endif( NOT Boost_FOUND )

#set( BOOST_LINK_LIBS ${Boost_LIBRARIES} ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY} ${Boost_PROGRAM_OPTIONS_LIBRARY} ${Boost_DATE_TIME_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_FILESYSTEM_LIBRARY} )

add_executable( test_energy_calibration.exe test_energy_calibration.cpp )
target_link_libraries( test_energy_calibration.exe PRIVATE SpecUtils Boost::unit_test_framework )
set_target_properties( test_energy_calibration.exe PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
add_test( TestEnergyCalibration
                 ${EXECUTABLE_OUTPUT_PATH}/test_energy_calibration.exe
                 --log_level=test_suite --report_level=detailed --catch_system_error=yes "-- --indir=${testdir}" )


add_executable( test_spec_file_open.exe test_spec_file_open.cpp )
target_link_libraries( test_spec_file_open.exe PRIVATE SpecUtils Boost::unit_test_framework )
set_target_properties( test_spec_file_open.exe PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
FILE(GLOB children RELATIVE ${ADD_TEST_SPEC_DIR} ${ADD_TEST_SPEC_DIR}/*)
FOREACH(child ${children})
  SET( filetestdir ${curdir}/${child} )
  IF(IS_DIRECTORY ${filetestdir})
    #uhg, if we dont get rid of spaces in the file name, it gets interprested as a
    #  seperate command line argument - even if we escape it with quotes
    string(REPLACE " " "%20" filetestdir ${filetestdir} )
    message( ${filetestdir} )
    add_test( "\"Test open ${child}\""
                 ${EXECUTABLE_OUTPUT_PATH}/test_spec_file_open.exe 
                 --log_level=test_suite --report_level=detailed --catch_system_error=yes "-- --indir=${filetestdir}")
  ENDIF()
ENDFOREACH()


add_executable( test_iso_time_string.exe test_iso_time_string.cpp )
target_link_libraries( test_iso_time_string.exe PRIVATE SpecUtils Boost::unit_test_framework Boost::date_time)
set_target_properties( test_iso_time_string.exe PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
add_test( TestIsoTimeString ${EXECUTABLE_OUTPUT_PATH}/test_iso_time_string.exe --log_level=test_suite --report_level=detailed --catch_system_error=yes "-- --indir=${testdir}" )

add_executable( test_safe_get_line.exe test_safe_get_line.cpp )
target_link_libraries( test_safe_get_line.exe PRIVATE SpecUtils Boost::unit_test_framework )
set_target_properties( test_safe_get_line.exe PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
add_test( TestSafeGetLine ${EXECUTABLE_OUTPUT_PATH}/test_safe_get_line.exe --log_level=test_suite --report_level=detailed --catch_system_error=yes "-- --indir=${testdir}" )


add_executable( test_time_from_string.exe test_time_from_string.cpp )
target_link_libraries( test_time_from_string.exe PRIVATE SpecUtils Boost::unit_test_framework )
set_target_properties( test_time_from_string.exe PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
add_test( TestTimeFromString ${EXECUTABLE_OUTPUT_PATH}/test_time_from_string.exe --log_level=test_suite --report_level=detailed --catch_system_error=yes "-- --indir=${testdir}" )


add_executable( test_string_functions.exe test_string_functions.cpp )
target_link_libraries( test_string_functions.exe PRIVATE SpecUtils Boost::unit_test_framework )
set_target_properties( test_string_functions.exe PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
add_test( TestUtilityStringFunctions ${EXECUTABLE_OUTPUT_PATH}/test_string_functions.exe "--indir=${testdir}" --log_level=test_suite --report_level=detailed --catch_system_error=yes )


add_executable( test_split_to_floats_and_ints.exe test_split_to_floats_and_ints.cpp )
target_link_libraries( test_split_to_floats_and_ints.exe PRIVATE SpecUtils Boost::unit_test_framework )
set_target_properties( test_split_to_floats_and_ints.exe PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
add_test( TestSplitToFloats ${EXECUTABLE_OUTPUT_PATH}/test_split_to_floats_and_ints.exe "--indir=${testdir}" --log_level=test_suite --report_level=detailed --catch_system_error=yes )


add_executable( test_utf8_limit_str_size.exe test_utf8_limit_str_size.cpp )
target_link_libraries( test_utf8_limit_str_size.exe PRIVATE SpecUtils Boost::unit_test_framework )
set_target_properties( test_utf8_limit_str_size.exe PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
add_test( TestUtf8LimitStrSize ${EXECUTABLE_OUTPUT_PATH}/test_utf8_limit_str_size.exe --log_level=test_suite --report_level=detailed --catch_system_error=yes "-- --indir=${testdir}" )


add_executable( test_utf8_str_len.exe test_utf8_str_len.cpp )
target_link_libraries( test_utf8_str_len.exe PRIVATE SpecUtils Boost::unit_test_framework )
set_target_properties( test_utf8_str_len.exe PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
add_test( TestUtf8StrLen ${EXECUTABLE_OUTPUT_PATH}/test_utf8_str_len.exe --log_level=test_suite --report_level=detailed --catch_system_error=yes "-- --indir=${testdir}" )

add_executable( test_cubic_spline.exe test_cubic_spline.cpp )
target_link_libraries( test_cubic_spline.exe PRIVATE SpecUtils Boost::unit_test_framework )
set_target_properties( test_cubic_spline.exe PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
add_test( TestCubicSpline ${EXECUTABLE_OUTPUT_PATH}/test_cubic_spline.exe --log_level=test_suite --report_level=detailed --catch_system_error=yes "-- --indir=${testdir}" )


add_executable( test_filesystem_functions.exe test_filesystem_functions.cpp )
target_link_libraries( test_filesystem_functions.exe PRIVATE SpecUtils Boost::unit_test_framework Boost::filesystem )
set_target_properties( test_filesystem_functions.exe PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
add_test( testUtilityFilesystemFunctions ${EXECUTABLE_OUTPUT_PATH}/test_filesystem_functions.exe --log_level=test_suite --report_level=detailed --catch_system_error=yes )

add_executable( test_rebin_by_lower_energy.exe test_rebin_by_lower_energy.cpp )
target_link_libraries( test_rebin_by_lower_energy.exe PRIVATE SpecUtils Boost::unit_test_framework )
set_target_properties( test_rebin_by_lower_energy.exe PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
add_test( TestRebinByLowerEnergy ${EXECUTABLE_OUTPUT_PATH}/test_rebin_by_lower_energy.exe --log_level=test_suite --report_level=detailed --catch_system_error=yes )

if( SpecUtils_ENABLE_URI_SPECTRA )
add_executable( test_uri_spectrum.exe test_uri_spectrum.cpp )
target_link_libraries( test_uri_spectrum.exe PRIVATE SpecUtils Boost::unit_test_framework )
set_target_properties( test_uri_spectrum.exe PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
add_test( TestRebinByLowerEnergy ${EXECUTABLE_OUTPUT_PATH}/test_uri_spectrum.exe --log_level=test_suite --report_level=detailed --catch_system_error=yes )
endif()