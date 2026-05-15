cmake_minimum_required(VERSION 3.15...4.3)
project(SpecUtilExamples)

add_executable( convert_to_n42 convert_to_n42.cpp )
target_link_libraries( convert_to_n42 PUBLIC SpecUtils )

add_executable( convert_to_cnf convert_to_cnf.cpp )
target_link_libraries( convert_to_cnf PUBLIC SpecUtils )
# C++ standard floor (cxx_std_11) is inherited via PUBLIC linkage to SpecUtils.


if( SpecUtils_C_BINDINGS )
  add_executable( c_interface_example c_interface_example.c )
  set_source_files_properties( c_interface_example.c PROPERTIES LANGUAGE C )
  set_target_properties( c_interface_example PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES )
  target_link_libraries( c_interface_example PUBLIC SpecUtils )
endif( SpecUtils_C_BINDINGS )
