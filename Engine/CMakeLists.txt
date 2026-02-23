cmake_minimum_required(VERSION 3.7)
project(VelixEngine)

set(CMAKE_CXX_STANDARD 20)
set(INCLUDE_DIR include)
set(SRC_DIR src)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
file(GLOB_RECURSE PROJECT_HEADERS  ${INCLUDE_DIR}/*.h ${INCLUDE_DIR}/*.hpp)
file(GLOB_RECURSE PROJECT_SOURCES ${SRC_DIR}/*.cpp)

add_library(${PROJECT_NAME} STATIC
        ${PROJECT_SOURCES}
        ${PROJECT_HEADERS}
)

target_include_directories(${PROJECT_NAME} PUBLIC
        ${INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME}
        PUBLIC
        nlohmann_json
        tinyobjloader
        fbx_sdk
        VelixCore
        spirv-cross-core
        spirv-cross-cpp
        spirv-cross-glsl
        shaderc
        physx
)