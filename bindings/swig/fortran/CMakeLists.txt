cmake_minimum_required (VERSION 3.25)
project (specutils-swig-fortran Fortran CXX C)

set(SWIG_INTERFACE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/SpecUtilsFortran.i)
set(SWIG_FORTRAN_WRAPPER ${CMAKE_CURRENT_SOURCE_DIR}/SpecUtilsWrap.f90)
set(SWIG_CPP_WRAPPER ${CMAKE_CURRENT_SOURCE_DIR}/SpecUtilsFortran_wrap.cxx)

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

add_library(specutils_fortran_wrap ${SWIG_CPP_WRAPPER} )
set_property(TARGET specutils_fortran_wrap PROPERTY POSITION_INDEPENDENT_CODE ON )
target_compile_options(specutils_fortran_wrap PRIVATE
  $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:/EHsc>
  $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/EHsc>
)

add_library(specutils_fortran SHARED ${SWIG_FORTRAN_WRAPPER} )
target_link_libraries(specutils_fortran_wrap PUBLIC SpecUtils)
target_link_libraries(specutils_fortran PRIVATE specutils_fortran_wrap)

if( SpecUtils_BUILD_UNIT_TESTS )
  add_library(test_drive testdrive.F90)
  add_executable(test_specutils_fortran test_specutils_fortran.f90 ${SWIG_FORTRAN_WRAPPER} )
  target_link_libraries(test_specutils_fortran test_drive specutils_fortran_wrap )
  add_test(NAME test_specutils_fortran COMMAND test_specutils_fortran)
endif()



# Install the library
install(TARGETS specutils_fortran
    EXPORT SpecUtilsTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}  # For shared libraries on UNIX-like systems
)
