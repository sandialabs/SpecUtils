cmake_minimum_required (VERSION 3.25)
project (specutils-csharp VERSION 2025.1.0 LANGUAGES CSharp CXX)

INCLUDE(CSharpUtilities)


set(SWIG_INTERFACE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/SpecUtilsCSharp.i)
set(SWIG_CPP_WRAPPER ${CMAKE_CURRENT_SOURCE_DIR}/SpecUtilsCSharp_wrap.cxx)

# add_custom_command(
#     OUTPUT ${SWIG_CPP_WRAPPER} ${SWIG_FORTRAN_WRAPPER}
#     COMMAND ${CMAKE_COMMAND} -E echo "Generating SWIG wrapper..."
#     COMMAND swig -I../../../ -fortran -c++ -outdir ${CMAKE_CURRENT_SOURCE_DIR}  -o ${SWIG_CPP_WRAPPER} ${SWIG_INTERFACE_FILE}
#     DEPENDS ${SWIG_INTERFACE_FILE} 
#             ${CMAKE_SOURCE_DIR}/SpecUtils/EnergyCalibration.h 
#             ${CMAKE_SOURCE_DIR}/SpecUtils/SpecFile.h
#             ${CMAKE_SOURCE_DIR}/SpecUtils/FileSystem.h
#     COMMENT "Generating SWIG **Fortran** wrapper code"
#     WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
# )

add_library(specutils_csharp_wrap SHARED ${SWIG_CPP_WRAPPER} )
target_link_libraries(specutils_csharp_wrap PRIVATE SpecUtils)

# Install the library
install(TARGETS specutils_csharp_wrap
    EXPORT SpecUtilsTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}  # For shared libraries on UNIX-like systems
)

CONFIGURE_FILE("${CMAKE_CURRENT_SOURCE_DIR}/AssemblyInfo.cs.template" "${CMAKE_CURRENT_SOURCE_DIR}/csharp_out/AssemblyInfo.cs")

file(GLOB CSHARP_SRC csharp_out/*.cs)

add_library(specutils_csharp SHARED ${CSHARP_SRC})