cmake_minimum_required(VERSION 3.14...3.29)

project(SpecUtilsJava LANGUAGES CXX)

find_package(SWIG REQUIRED)
include(UseSWIG)

find_package(JNI REQUIRED)
find_package(Java REQUIRED)
include(UseJava)

# Build the main SpecUtils library as a subdirectory
set(SpecUtils_ENABLE_D3_CHART ON CACHE BOOL "" FORCE)
set(SpecUtils_D3_SUPPORT_FILE_STATIC ON CACHE BOOL "" FORCE)
set(SpecUtils_JAVA_SWIG ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ${CMAKE_CURRENT_BINARY_DIR}/SpecUtils)

set_source_files_properties(SpecUtils.i PROPERTIES
  CPLUSPLUS ON
  SWIG_MODULE_NAME SpecUtilsSwig
)

# SWIG needs to find headers relative to the SpecUtils project root
set(SPECUTILS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

swig_add_library(SpecUtilsJni
  TYPE SHARED
  LANGUAGE java
  OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gov/sandia/specutils"
  SOURCES SpecUtils.i
)

set_target_properties(SpecUtilsJni PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
  SWIG_USE_TARGET_INCLUDE_DIRECTORIES TRUE
)

target_include_directories(SpecUtilsJni
  PRIVATE
    ${SPECUTILS_ROOT}
    ${CMAKE_CURRENT_BINARY_DIR}/SpecUtils  # For SpecUtils_config.h and D3SpectrumExportResources.h
    ${SPECUTILS_ROOT}/3rdparty
    ${JNI_INCLUDE_DIRS}
)

target_link_libraries(SpecUtilsJni PRIVATE SpecUtils)

# Set the Java package output directory
set(SWIG_JAVA_PACKAGE "gov.sandia.specutils")
set_property(TARGET SpecUtilsJni PROPERTY SWIG_COMPILE_OPTIONS -package ${SWIG_JAVA_PACKAGE})

# Use --release 8 to target Java 8 bytecode when building with JDK 9+.
# JDK 8 natively produces class version 52 and doesn't support the --release flag.
if(Java_VERSION_MAJOR GREATER 8)
  set(JAVAC_RELEASE_FLAG "--release 8")
else()
  set(JAVAC_RELEASE_FLAG "")
endif()

# Create a CMake script to compile Java sources into a JAR after SWIG generates them.
# We use a script because the glob needs to run at build time (not configure time),
# and because cmd.exe on Windows does not expand *.java globs.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/build_jar.cmake" "
file(GLOB JAVA_SOURCES \"${CMAKE_CURRENT_BINARY_DIR}/gov/sandia/specutils/*.java\")
execute_process(
  COMMAND \${JAVAC} -d \"${CMAKE_CURRENT_BINARY_DIR}/classes\" ${JAVAC_RELEASE_FLAG} \${JAVA_SOURCES}
  RESULT_VARIABLE JAVAC_RESULT
)
if(NOT JAVAC_RESULT EQUAL 0)
  message(FATAL_ERROR \"javac failed with exit code \${JAVAC_RESULT}\")
endif()
execute_process(
  COMMAND \${JAR} cf \"${CMAKE_CURRENT_BINARY_DIR}/SpecUtils.jar\" -C \"${CMAKE_CURRENT_BINARY_DIR}/classes\" .
  RESULT_VARIABLE JAR_RESULT
)
if(NOT JAR_RESULT EQUAL 0)
  message(FATAL_ERROR \"jar failed with exit code \${JAR_RESULT}\")
endif()
")

add_custom_command(
  TARGET SpecUtilsJni POST_BUILD
  COMMAND ${CMAKE_COMMAND}
    -DJAVAC=${Java_JAVAC_EXECUTABLE}
    -DJAR=${Java_JAR_EXECUTABLE}
    -P "${CMAKE_CURRENT_BINARY_DIR}/build_jar.cmake"
  COMMENT "Compiling Java sources and creating SpecUtils.jar"
)

# Make sure the classes directory exists
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/classes")

# Install targets
install(TARGETS SpecUtilsJni
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/SpecUtils.jar"
  DESTINATION lib
)
