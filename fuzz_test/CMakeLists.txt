cmake_policy(SET CMP0048 NEW)
project( SpecUtilsFuzz VERSION 1 )
cmake_minimum_required( VERSION VERSION 3.11 FATAL_ERROR )

add_executable( file_parse_fuzz file_parse_fuzz.cpp )

target_compile_options( file_parse_fuzz PRIVATE $<$<CXX_COMPILER_ID:Clang>:-g -O1 -fsanitize=fuzzer,address,undefined> )
target_link_libraries( file_parse_fuzz PRIVATE SpecUtils $<$<CXX_COMPILER_ID:Clang>:-fsanitize=fuzzer,address,undefined> )
set_target_properties( file_parse_fuzz PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )
