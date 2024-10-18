cmake_minimum_required (VERSION 3.25)
project (specutils-swig-fortran Fortran CXX C)

# Set the SWIG library path
#set(SWIG_LIB /usr/local/share/swig/4.2.0/)

# Define the SWIG interface file
set(SWIG_INTERFACE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/SpecUtilsFortran.i)

# Add a custom command to generate the wrapper code
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/SpecUtilsFortran_wrap.cxx  ${CMAKE_CURRENT_SOURCE_DIR}/SpecUtilsWrap.f90
    COMMAND ${CMAKE_COMMAND} -E echo "Generating SWIG wrapper..."
    COMMAND swig -I../../../ -fortran -c++ -outcurrentdir -debug-classes ${SWIG_INTERFACE_FILE}
    DEPENDS ${SWIG_INTERFACE_FILE}
    COMMENT "Generating SWIG wrapper code"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(specutils_fortran_wrap ${CMAKE_CURRENT_SOURCE_DIR}/SpecUtilsFortran_wrap.cxx )
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
