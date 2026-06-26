cmake_minimum_required(VERSION 3.15...4.3)
cmake_policy(SET CMP0048 NEW)
cmake_policy(SET CMP0079 NEW) #allow altering SpecUtils linking from this file
project( SpecUtilsFuzz VERSION 1 )

add_executable( file_parse_fuzz file_parse_fuzz.cpp fuzz_interface.h fuzz_interface.cpp )

# spirit float parser can assert with inputs like
#  ".000000000000000000000000000000000000000000000000000000000000000000000000000"
#  when we compile as RelWithDebInfo; so we'll disable this asserting for fuzzing.
# Scope this PRIVATE to SpecUtils' own translation units (which is where boost::spirit is
#  compiled) rather than PUBLIC, so we don't strip boost asserts from the fuzz harness or any
#  other consumer in the build tree - that would reduce the coverage fuzzing is meant to provide.
target_compile_definitions( SpecUtils PRIVATE BOOST_DISABLE_ASSERTS )

target_compile_options( file_parse_fuzz PRIVATE $<$<CXX_COMPILER_ID:Clang>:-g -O1 -fsanitize=fuzzer,address,undefined -fprofile-instr-generate -fcoverage-mapping> )
target_compile_options( SpecUtils PRIVATE $<$<CXX_COMPILER_ID:Clang>:-g -O1 -fsanitize=fuzzer,address,undefined -fprofile-instr-generate -fcoverage-mapping> )
target_link_libraries( SpecUtils PUBLIC $<$<CXX_COMPILER_ID:Clang>:-fsanitize=fuzzer,address,undefined -fprofile-instr-generate -fcoverage-mapping > )
target_link_libraries( file_parse_fuzz PRIVATE SpecUtils $<$<CXX_COMPILER_ID:Clang>:-fsanitize=fuzzer,address,undefined> )

#add_executable( run_one_file_parse run_one_file_parse.cpp fuzz_interface.h fuzz_interface.cpp )
#target_link_libraries( run_one_file_parse PRIVATE SpecUtils )

add_executable( fuzz_str_utils fuzz_str_utils.cpp )
target_link_libraries( fuzz_str_utils PRIVATE SpecUtils $<$<CXX_COMPILER_ID:Clang>:-fsanitize=fuzzer,address,undefined -fprofile-instr-generate -fcoverage-mapping> )

if( SpecUtils_ENABLE_URI_SPECTRA )
  add_executable( fuzz_uri_spectrum fuzz_uri_spectrum.cpp )
  target_link_libraries( fuzz_uri_spectrum PRIVATE SpecUtils $<$<CXX_COMPILER_ID:Clang>:-fsanitize=fuzzer,address,undefined -fprofile-instr-generate -fcoverage-mapping> )
endif( SpecUtils_ENABLE_URI_SPECTRA )