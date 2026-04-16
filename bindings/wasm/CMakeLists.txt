cmake_minimum_required(VERSION 3.15)
project(SpecUtilsWasm)

# SpecUtils options for WASM build
set(SpecUtils_USING_NO_THREADING ON CACHE BOOL "" FORCE)
set(SpecUtils_C_BINDINGS ON CACHE BOOL "" FORCE)
set(SpecUtils_ENABLE_D3_CHART OFF CACHE BOOL "" FORCE)
set(SpecUtils_D3_SUPPORT_FILE_STATIC OFF CACHE BOOL "" FORCE)
set(SpecUtils_ENABLE_URI_SPECTRA OFF CACHE BOOL "" FORCE)
set(SpecUtils_FLT_PARSE_METHOD "strtod" CACHE STRING "" FORCE)
set(SpecUtils_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(SpecUtils_BUILD_FUZZING_TESTS OFF CACHE BOOL "" FORCE)
set(SpecUtils_BUILD_REGRESSION_TEST OFF CACHE BOOL "" FORCE)
set(SpecUtils_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SpecUtils_JAVA_SWIG OFF CACHE BOOL "" FORCE)
set(SpecUtils_INJA_TEMPLATES OFF CACHE BOOL "" FORCE)
set(SpecUtils_USE_SIMD OFF CACHE BOOL "" FORCE)
set(SpecUtils_ENABLE_EQUALITY_CHECKS OFF CACHE BOOL "" FORCE)
set(PERFORM_DEVELOPER_CHECKS OFF CACHE BOOL "" FORCE)

# Enable C++ exception handling — SpecUtils uses try/catch extensively for parsing.
# Use native WASM exception handling (-fwasm-exceptions). MUST be set before
# add_subdirectory so the SpecUtils library is compiled with exception support.
# Supported by Chrome 95+, Firefox 100+, Safari 15.2+, Node.js 17+.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fwasm-exceptions")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fwasm-exceptions")

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ${CMAKE_CURRENT_BINARY_DIR}/SpecUtils)

add_executable(specutils_wasm SpecUtilsWasm.cpp)
target_link_libraries(specutils_wasm PRIVATE SpecUtils)
target_include_directories(specutils_wasm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# All C binding functions that we export to JS, plus our buffer helpers
set(EXPORTED_FUNCTIONS
  "_main"
  "_malloc"
  "_free"
  "_SpecUtils_SpecFile_create"
  "_SpecUtils_SpecFile_destroy"
  "_SpecFile_load_file"
  "_SpecUtils_write_to_file"
  "_SpecUtils_SpecFile_passthrough"
  "_SpecUtils_SpecFile_number_measurements"
  "_SpecUtils_SpecFile_number_gamma_channels"
  "_SpecUtils_SpecFile_get_measurement_by_index"
  "_SpecUtils_SpecFile_get_measurement_by_sample_det"
  "_SpecUtils_SpecFile_number_detectors"
  "_SpecUtils_SpecFile_detector_name"
  "_SpecUtils_SpecFile_number_gamma_detectors"
  "_SpecUtils_SpecFile_gamma_detector_name"
  "_SpecUtils_SpecFile_number_neutron_detectors"
  "_SpecUtils_SpecFile_neutron_detector_name"
  "_SpecUtils_SpecFile_number_samples"
  "_SpecUtils_SpecFile_sample_number"
  "_SpecUtils_SpecFile_sum_measurements"
  "_SpecUtils_SpecFile_sum_gamma_live_time"
  "_SpecUtils_SpecFile_sum_gamma_real_time"
  "_SpecUtils_SpecFile_gamma_count_sum"
  "_SpecUtils_SpecFile_neutron_counts_sum"
  "_SpecUtils_SpecFile_filename"
  "_SpecUtils_SpecFile_uuid"
  "_SpecUtils_SpecFile_manufacturer"
  "_SpecUtils_SpecFile_instrument_model"
  "_SpecUtils_SpecFile_instrument_type"
  "_SpecUtils_SpecFile_detector_type"
  "_SpecUtils_SpecFile_number_remarks"
  "_SpecUtils_SpecFile_remark"
  "_SpecUtils_Measurement_create"
  "_SpecUtils_Measurement_destroy"
  "_SpecUtils_Measurement_number_gamma_channels"
  "_SpecUtils_Measurement_gamma_channel_counts"
  "_SpecUtils_Measurement_energy_bounds"
  "_SpecUtils_Measurement_live_time"
  "_SpecUtils_Measurement_real_time"
  "_SpecUtils_Measurement_gamma_count_sum"
  "_SpecUtils_Measurement_neutron_count_sum"
  "_SpecUtils_Measurement_contained_neutron"
  "_SpecUtils_Measurement_sample_number"
  "_SpecUtils_Measurement_detector_name"
  "_SpecUtils_Measurement_title"
  "_SpecUtils_Measurement_start_time_usecs"
  "_SpecUtils_load_from_buffer"
  "_SpecUtils_export_to_buffer"
  "_SpecUtils_free_export_buffer"
)

# Join the list into a JSON-style array string
string(JOIN "," EXPORTED_FUNCTIONS_STR ${EXPORTED_FUNCTIONS})
set(EXPORTED_FUNCTIONS_STR "[${EXPORTED_FUNCTIONS_STR}]")

set(EXPORTED_RUNTIME_METHODS "[\"cwrap\",\"ccall\",\"FS\",\"HEAPU8\",\"HEAPF32\",\"HEAP32\",\"UTF8ToString\",\"stringToUTF8\",\"lengthBytesUTF8\"]")

set_target_properties(specutils_wasm PROPERTIES
  SUFFIX ".js"
)

target_link_options(specutils_wasm PRIVATE
  "SHELL:-sSINGLE_FILE=1"
  "SHELL:-sMODULARIZE=1"
  "SHELL:-sEXPORT_NAME='SpecUtilsModule'"
  "SHELL:-sALLOW_MEMORY_GROWTH=1"
  "SHELL:-sINITIAL_MEMORY=67108864"
  "SHELL:-sSTACK_SIZE=8388608"
  "SHELL:-sEXPORTED_FUNCTIONS=${EXPORTED_FUNCTIONS_STR}"
  "SHELL:-sEXPORTED_RUNTIME_METHODS=${EXPORTED_RUNTIME_METHODS}"
  "SHELL:-sNO_EXIT_RUNTIME=1"
  "SHELL:-sWASM=1"
  "SHELL:-sENVIRONMENT=web"
  "SHELL:-sFORCE_FILESYSTEM=1"
  -fwasm-exceptions
)

# ---- Node.js CLI tool for spectrum file conversion ----
# Uses Emscripten with NODERAWFS for direct filesystem access via Node.js.
# Run with: node specutils_cli.js <input_file> [output_file.n42]
add_executable(specutils_cli examples/cli/specutils_cli.cpp)
target_link_libraries(specutils_cli PRIVATE SpecUtils)

set_target_properties(specutils_cli PROPERTIES
  SUFFIX ".js"
)

target_link_options(specutils_cli PRIVATE
  "SHELL:-sSINGLE_FILE=1"
  "SHELL:-sALLOW_MEMORY_GROWTH=1"
  "SHELL:-sINITIAL_MEMORY=67108864"
  "SHELL:-sSTACK_SIZE=8388608"
  "SHELL:-sENVIRONMENT=node"
  "SHELL:-sNODERAWFS=1"
  "SHELL:-sEXIT_RUNTIME=1"
  -fwasm-exceptions
)


# Find Python for assembly script
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Custom target: assemble self-contained HTML viewer
add_custom_target(specutils_html ALL
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/examples/viewer/assemble_html.py
    --wasm-js ${CMAKE_CURRENT_BINARY_DIR}/specutils_wasm.js
    --d3-js ${CMAKE_CURRENT_SOURCE_DIR}/../../d3_resources/d3.v3.min.js
    --chart-js ${CMAKE_CURRENT_SOURCE_DIR}/../../d3_resources/SpectrumChartD3.js
    --chart-css ${CMAKE_CURRENT_SOURCE_DIR}/../../d3_resources/SpectrumChartD3.css
    --app-js ${CMAKE_CURRENT_SOURCE_DIR}/specutils_web.js
    --viewer-template ${CMAKE_CURRENT_SOURCE_DIR}/examples/viewer/specutils_web.html
    --output-dir ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS specutils_wasm
  COMMENT "Assembling self-contained HTML viewer"
)
