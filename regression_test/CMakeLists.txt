cmake_policy(SET CMP0048 NEW)
project(SpecUtilsRegressionTest VERSION 1)

cmake_minimum_required(VERSION 3.5.0 FATAL_ERROR)

if( NOT SpecUtils_ENABLE_EQUALITY_CHECKS )
  message( FATAL_ERROR "SpecUtils_ENABLE_EQUALITY_CHECKS must be enabled when building regression_test executable" )
endif( NOT SpecUtils_ENABLE_EQUALITY_CHECKS )

add_executable( regression_test regression_test.cpp )

target_link_libraries( regression_test PUBLIC SpecUtils )

set_target_properties( regression_test PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )


