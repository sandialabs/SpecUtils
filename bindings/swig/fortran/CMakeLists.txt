cmake_minimum_required (VERSION 3.29)
project (specutils-swig-fortran Fortran CXX C)

add_library(test_drive testdrive.F90)

add_library(specutils_fortran_wrap SHARED SpecUtilsFortran_wrap.cxx )
add_library(specutils_fortran SpecUtilsWrap.f90 )
target_link_libraries(specutils_fortran_wrap PUBLIC SpecUtils)
target_compile_options(specutils_fortran PUBLIC -ffree-line-length-none -ffixed-line-length-150)
target_link_libraries(specutils_fortran PRIVATE specutils_fortran_wrap)

add_executable(test_specutils_fortran test_specutils_fortran.f90 )
target_link_libraries(test_specutils_fortran test_drive specutils_fortran )

add_test(NAME test_specutils_fortran COMMAND test_specutils_fortran)