cmake_minimum_required (VERSION 3.25)
project (specutils-swig-fortran Fortran CXX C)

add_library(specutils_fortran_wrap SpecUtilsFortran_wrap.cxx )
set_property(TARGET specutils_fortran_wrap PROPERTY POSITION_INDEPENDENT_CODE ON )
add_library(specutils_fortran SHARED SpecUtilsWrap.f90 )
target_link_libraries(specutils_fortran_wrap PUBLIC SpecUtils)
target_compile_options(specutils_fortran PUBLIC -ffree-line-length-none -ffixed-line-length-150)
target_link_libraries(specutils_fortran PRIVATE specutils_fortran_wrap)

if( SpecUtils_BUILD_UNIT_TESTS )
  add_library(test_drive testdrive.F90)
  add_executable(test_specutils_fortran test_specutils_fortran.f90 )
  target_link_libraries(test_specutils_fortran test_drive specutils_fortran )
  add_test(NAME test_specutils_fortran COMMAND test_specutils_fortran)
endif()



# Install the library
install(TARGETS specutils_fortran
    EXPORT SpecUtilsTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}  # For shared libraries on UNIX-like systems
)
