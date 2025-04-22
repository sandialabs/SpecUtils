cmake_minimum_required (VERSION 3.25)
project (specutils-swig-fortran Fortran CXX C)

set(SWIG_INTERFACE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/SpecUtilsFortran.i)
set(SWIG_FORTRAN_WRAPPER ${CMAKE_CURRENT_SOURCE_DIR}/SpecUtilsWrap.f90)
set(SWIG_CPP_WRAPPER ${CMAKE_CURRENT_SOURCE_DIR}/SpecUtilsFortran_wrap.cxx)



set( CMAKE_VERBOSE_MAKEFILE on )

if (SpecUtils_FORTRAN_SWIG_GEN)
  add_custom_command(
      OUTPUT ${SWIG_CPP_WRAPPER} ${SWIG_FORTRAN_WRAPPER}
      COMMAND ${CMAKE_COMMAND} -E echo "Generating SWIG wrapper..."
      COMMAND swig -I../../../ -fortran -c++ -outdir ${CMAKE_CURRENT_SOURCE_DIR}  -o ${SWIG_CPP_WRAPPER} ${SWIG_INTERFACE_FILE}
      DEPENDS ${SWIG_INTERFACE_FILE} 
              ${CMAKE_SOURCE_DIR}/SpecUtils/EnergyCalibration.h 
              ${CMAKE_SOURCE_DIR}/SpecUtils/SpecFile.h
              ${CMAKE_SOURCE_DIR}/SpecUtils/FileSystem.h
      COMMENT "Generating SWIG **Fortran** wrapper code"
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )
endif()

add_library(specutils_fortran_cpp_wrap SHARED ${SWIG_CPP_WRAPPER} )
target_link_libraries(specutils_fortran_cpp_wrap PRIVATE SpecUtils)

# Suppress warning C4297 but only for MSVC builds using generator expressions
target_compile_options(specutils_fortran_cpp_wrap PRIVATE
  "$<$<CXX_COMPILER_ID:MSVC>:/wd4297>"
)

# Install the library
install(TARGETS specutils_fortran_cpp_wrap
RUNTIME DESTINATION  ${CMAKE_INSTALL_LIBDIR}/../build
ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/../build  
)


# if( SpecUtils_BUILD_UNIT_TESTS )
#   add_library(test_drive testdrive.F90)
#   add_executable(test_specutils_fortran test_specutils_fortran.f90 )
#   target_link_libraries(test_specutils_fortran test_drive specutils_fortran )
#   add_test(NAME test_specutils_fortran COMMAND test_specutils_fortran)
# endif()
