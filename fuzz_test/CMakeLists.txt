project( SpecUtilsFuzz VERSION 1 )
cmake_minimum_required( VERSION VERSION 3.11 FATAL_ERROR )
cmake_policy(SET CMP0048 NEW)
cmake_policy(SET CMP0079 NEW) #allow altering SpecUtils linking from this file

add_executable( file_parse_fuzz file_parse_fuzz.cpp fuzz_interface.h fuzz_interface.cpp )

target_compile_options( file_parse_fuzz PRIVATE $<$<CXX_COMPILER_ID:Clang>:-g -O1 -fsanitize=fuzzer,address,undefined -fprofile-instr-generate -fcoverage-mapping> )
target_compile_options( SpecUtils PRIVATE $<$<CXX_COMPILER_ID:Clang>:-g -O1 -fsanitize=fuzzer,address,undefined -fprofile-instr-generate -fcoverage-mapping> )
target_link_libraries( SpecUtils PUBLIC $<$<CXX_COMPILER_ID:Clang>:-fsanitize=fuzzer,address,undefined -fprofile-instr-generate -fcoverage-mapping > )
target_link_libraries( file_parse_fuzz PRIVATE SpecUtils $<$<CXX_COMPILER_ID:Clang>:-fsanitize=fuzzer,address,undefined> )
set_target_properties( file_parse_fuzz PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )


add_executable( run_one_file_parse run_one_file_parse.cpp fuzz_interface.h fuzz_interface.cpp )
target_link_libraries( run_one_file_parse PRIVATE SpecUtils )
set_target_properties( run_one_file_parse PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO )