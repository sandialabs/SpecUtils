cmake_minimum_required (VERSION 3.25)
project (SpecUtilsCSharp VERSION 2025.1.0 LANGUAGES CSharp CXX)

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

add_library(SpecUtilsWrap SHARED ${SWIG_CPP_WRAPPER} )
target_link_libraries(SpecUtilsWrap PUBLIC SpecUtils)

set (csharp_outdir "csharp_out")

CONFIGURE_FILE("${CMAKE_CURRENT_SOURCE_DIR}/AssemblyInfo.cs.template" "${CMAKE_CURRENT_SOURCE_DIR}/${csharp_outdir}/AssemblyInfo.cs")

file(GLOB CSHARP_SRC ${csharp_outdir}/*.cs)

add_library(SpecUtilsCSharp SHARED ${CSHARP_SRC})

# Set CLR assembly properties.
SET_TARGET_PROPERTIES(SpecUtilsCSharp PROPERTIES
    VS_DOTNET_REFERENCES "System;System.Core"
    VS_GLOBAL_ROOTNAMESPACE "Sandia.SpecUtils"
)

add_executable(test_app ${CMAKE_CURRENT_SOURCE_DIR}/Test.cs)
target_link_libraries(test_app SpecUtilsCSharp)

# Install the library
install(TARGETS SpecUtilsWrap SpecUtilsCSharp test_app
    EXPORT SpecUtilsTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}  # For shared libraries on UNIX-like systems
)
