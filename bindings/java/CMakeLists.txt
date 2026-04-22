cmake_minimum_required(VERSION 3.14...3.29)

project(SpecUtilsJavaJNI LANGUAGES CXX)

find_package(JNI REQUIRED)
find_package(Java 9 REQUIRED COMPONENTS Development)
include(UseJava)

# Turn on the C bindings in the main SpecUtils library — the JNI glue calls
# into them rather than calling C++ directly. The C bindings layer is the
# same one used by the C# wrapper.
set(SpecUtils_C_BINDINGS ON CACHE BOOL "" FORCE)
set(SpecUtils_ENABLE_D3_CHART ON CACHE BOOL "" FORCE)
set(SpecUtils_D3_SUPPORT_FILE_STATIC ON CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ${CMAKE_CURRENT_BINARY_DIR}/SpecUtils)

# --- Java sources & JNI header generation -----------------------------------
#
# `javac -h` (JDK 9+) replaces the removed-in-JDK-10 `javah` tool. It compiles
# Java sources AND emits a `.h` header for every class that declares `native`
# methods, naming headers like `gov_sandia_specutils_internal_Native.h`.
# Target bytecode stays at Java 8 via `--release 8` so the resulting .jar
# runs on any JRE 8+.

file(GLOB_RECURSE JAVA_MAIN_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/main/java/*.java")

set(JAVA_CLASSES_DIR "${CMAKE_CURRENT_BINARY_DIR}/classes")
set(JNI_HEADERS_DIR  "${CMAKE_CURRENT_BINARY_DIR}/jni_headers")
set(NATIVE_HEADER    "${JNI_HEADERS_DIR}/gov_sandia_specutils_internal_Native.h")
set(JAR_OUTPUT       "${CMAKE_CURRENT_BINARY_DIR}/SpecUtils.jar")

file(MAKE_DIRECTORY "${JAVA_CLASSES_DIR}")
file(MAKE_DIRECTORY "${JNI_HEADERS_DIR}")

# Target Java 8 bytecode so the .jar runs on any JRE 8+.
set(JAVAC_RELEASE_FLAG --release 8)

add_custom_command(
  OUTPUT "${NATIVE_HEADER}" "${JAR_OUTPUT}"
  COMMAND ${Java_JAVAC_EXECUTABLE} ${JAVAC_RELEASE_FLAG}
    -d "${JAVA_CLASSES_DIR}"
    -h "${JNI_HEADERS_DIR}"
    ${JAVA_MAIN_SOURCES}
  COMMAND ${Java_JAR_EXECUTABLE} cf "${JAR_OUTPUT}" -C "${JAVA_CLASSES_DIR}" .
  DEPENDS ${JAVA_MAIN_SOURCES}
  COMMENT "Compiling Java, generating JNI header, packaging SpecUtils.jar"
  VERBATIM
)

add_custom_target(SpecUtilsJavaCompile ALL
  DEPENDS "${NATIVE_HEADER}" "${JAR_OUTPUT}"
)

# --- Native shared library (JNI glue) ---------------------------------------

add_library(SpecUtilsJava SHARED
  src/main/cpp/SpecUtilsJni.cpp
)

target_include_directories(SpecUtilsJava PRIVATE
  ${JNI_INCLUDE_DIRS}
  ${JNI_HEADERS_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..          # for top-level SpecUtils headers
  ${CMAKE_CURRENT_BINARY_DIR}/SpecUtils      # for generated SpecUtils_config.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../c           # for SpecUtils_c.h
)

target_link_libraries(SpecUtilsJava PRIVATE SpecUtils)

set_target_properties(SpecUtilsJava PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

# Match the SWIG binding's suffix convention on macOS so that
# System.loadLibrary("SpecUtilsJava") finds the file on all platforms.
if(APPLE)
  set_target_properties(SpecUtilsJava PROPERTIES SUFFIX ".jnilib")
endif()

add_dependencies(SpecUtilsJava SpecUtilsJavaCompile)

# --- Install ---------------------------------------------------------------

install(TARGETS SpecUtilsJava
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib
)
install(FILES "${JAR_OUTPUT}" DESTINATION lib)
