cmake_minimum_required(VERSION 3.16)
project(VelixEditor)

set(CMAKE_CXX_STANDARD 20)

set(INCLUDE_DIR include)
set(SRC_DIR src)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
file(GLOB_RECURSE PROJECT_HEADERS CONFIGURE_DEPENDS ${INCLUDE_DIR}/*.h ${INCLUDE_DIR}/*.hpp)
file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS ${SRC_DIR}/*.cpp)

add_library(${PROJECT_NAME} STATIC
        ${PROJECT_SOURCES}
        ${PROJECT_HEADERS}
)

target_include_directories(${PROJECT_NAME} PUBLIC
        ${INCLUDE_DIR}
)

if(NOT TARGET CURL::libcurl)
    message(FATAL_ERROR "Vendored CURL target CURL::libcurl was not found. Ensure external/curl is initialized.")
endif()

target_link_libraries(${PROJECT_NAME}
        PUBLIC
        imgui
        imguizmo
        imgui_node_editor
        imgui_text_edit
        VelixCore
        VelixEngine
        VelixSDK
        CURL::libcurl
)
