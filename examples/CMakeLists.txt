cmake_minimum_required(VERSION 3.1...3.13)

if(${CMAKE_VERSION} VERSION_LESS 3.12)
  cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
endif()

project(SpecUtilExamples)

add_executable( convert_to_n42 convert_to_n42.cpp )
target_link_libraries( convert_to_n42 PUBLIC SpecUtils )

set_target_properties( convert_to_n42 PROPERTIES
  CXX_STANDARD 11
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)