cmake_minimum_required(VERSION 3.15...4.3)
cmake_policy(SET CMP0048 NEW)
project(SpecUtilsRegressionTest VERSION 1)

if( NOT SpecUtils_ENABLE_EQUALITY_CHECKS )
  message( FATAL_ERROR "SpecUtils_ENABLE_EQUALITY_CHECKS must be enabled when building regression_test executable" )
endif( NOT SpecUtils_ENABLE_EQUALITY_CHECKS )

add_executable( regression_test regression_test.cpp )

target_link_libraries( regression_test PUBLIC SpecUtils )


