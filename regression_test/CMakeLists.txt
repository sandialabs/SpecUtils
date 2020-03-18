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


#Android Studio has an old version of cmake... gotta accomidate, see
# https://cliutils.gitlab.io/modern-cmake/chapters/packages/Boost.html for an alternative
#target_link_libraries( regression_test PUBLIC SpecUtils Boost::program_options Boost::date_time Boost::system Boost::filesystem )

target_link_libraries( regression_test PRIVATE SpecUtils ${Boost_LIBRARIES} ${Boost_PROGRAM_OPTIONS_LIBRARY} ${Boost_DATE_TIME_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_FILESYSTEM_LIBRARY} )

set_target_properties( regression_test PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )


