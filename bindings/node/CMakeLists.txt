cmake_policy(SET CMP0048 NEW)
project( SpecUtilsJS )

# npm install -g cmake-js 
# npm install --save-dev node-addon-api
# cmake-js --CDSpecUtils_FLT_PARSE_METHOD="strtod" --CDCMAKE_BUILD_TYPE="Release"
# cmake-js build --target install

cmake_minimum_required(VERSION 3.1.0 FATAL_ERROR)

set( CMAKE_CXX_STANDARD 11 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_POSITION_INDEPENDENT_CODE ON )

IF(WIN32)
  set( MSVC_RUNTIME "static" )
  include( cmake/ConfigureMsvc.txt )
  configure_msvc_runtime() 
  set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj -D_SCL_SECURE_NO_WARNINGS /MP /wd4996 /wd4267 /DWINVER=0x0601 /D_WIN32_WINNT=0x0601" )
    #0x0601==Win7, 0x0501==WinXP
ENDIF(WIN32)


include_directories( ${CMAKE_CURRENT_BINARY_DIR} )
add_subdirectory( ${CMAKE_CURRENT_SOURCE_DIR}/../.. ${CMAKE_CURRENT_BINARY_DIR}/LibSpecUtils )

include_directories(${CMAKE_JS_INC})
set( SOURCE_FILES example.js SpecUtilsJS.h SpecUtilsJS.cpp )
add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} ${CMAKE_JS_SRC})
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".node")
target_link_libraries(${PROJECT_NAME} PUBLIC ${CMAKE_JS_LIB} SpecUtils )

target_include_directories( ${PROJECT_NAME} PUBLIC 
                            node_modules/node-addon-api/ 
                            ${CMAKE_CURRENT_SOURCE_DIR}/../.. 
)


install( FILES ${CMAKE_CURRENT_SOURCE_DIR}/example.js DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/node_release/ )
install( TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/node_release/ )



