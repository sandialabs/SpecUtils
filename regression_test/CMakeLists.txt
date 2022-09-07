cmake_policy(SET CMP0048 NEW)
project(SpecUtilsRegressionTest VERSION 1)

cmake_minimum_required(VERSION 3.1.0 FATAL_ERROR)

find_package( Boost REQUIRED COMPONENTS program_options date_time system filesystem )

if( NOT Boost_FOUND )
  message(FATAL_ERROR "Couldnt Find Boost")
endif( NOT Boost_FOUND )

if( NOT PERFORM_DEVELOPER_CHECKS )
  message( FATAL_ERROR "PERFORM_DEVELOPER_CHECKS must be enabled when building regression_test executable" )
endif( NOT PERFORM_DEVELOPER_CHECKS )

add_executable( regression_test regression_test.cpp )

target_link_libraries( regression_test PUBLIC SpecUtils Boost::program_options Boost::date_time Boost::system Boost::filesystem )

set_target_properties( regression_test PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )


