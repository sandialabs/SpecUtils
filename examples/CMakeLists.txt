cmake_minimum_required(VERSION 3.1...3.13)

if(${CMAKE_VERSION} VERSION_LESS 3.12)
  cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
endif()

project(SpecUtilExamples)

add_executable( convert_to_n42 convert_to_n42.cpp )
target_link_libraries( convert_to_n42 PUBLIC SpecUtils )

add_executable( convert_to_cnf convert_to_cnf.cpp )
target_link_libraries( convert_to_cnf PUBLIC SpecUtils )
set_target_properties( convert_to_n42 convert_to_cnf PROPERTIES
  CXX_STANDARD 11
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)


if( SpecUtils_C_BINDINGS )
  add_executable( c_interface_example c_interface_example.c )
  set_source_files_properties( c_interface_example.c PROPERTIES LANGUAGE C )
  set_target_properties( c_interface_example PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES )
  target_link_libraries( c_interface_example PUBLIC SpecUtils )
endif( SpecUtils_C_BINDINGS )
