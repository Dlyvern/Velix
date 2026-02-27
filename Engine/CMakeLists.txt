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

if(TARGET OpenEXR::OpenEXR)
    target_link_libraries(${PROJECT_NAME} PUBLIC OpenEXR::OpenEXR)
    target_compile_definitions(${PROJECT_NAME} PUBLIC ELIX_HAS_OPENEXR=1)
elseif(TARGET OpenEXR)
    target_link_libraries(${PROJECT_NAME} PUBLIC OpenEXR)
    target_compile_definitions(${PROJECT_NAME} PUBLIC ELIX_HAS_OPENEXR=1)
else()
    message(WARNING "OpenEXR target not found. .exr texture loading will be disabled.")
endif()

if(TARGET ZLIB::ZLIB)
    target_link_libraries(${PROJECT_NAME} PUBLIC ZLIB::ZLIB)
    target_compile_definitions(${PROJECT_NAME} PUBLIC ELIX_HAS_ZLIB=1)
elseif(TARGET zlib)
    target_link_libraries(${PROJECT_NAME} PUBLIC zlib)
    target_compile_definitions(${PROJECT_NAME} PUBLIC ELIX_HAS_ZLIB=1)
else()
    find_package(ZLIB QUIET)
    if(ZLIB_FOUND)
        target_link_libraries(${PROJECT_NAME} PUBLIC ZLIB::ZLIB)
        target_compile_definitions(${PROJECT_NAME} PUBLIC ELIX_HAS_ZLIB=1)
    else()
        message(WARNING "ZLIB not found. Texture asset byte compression will be disabled.")
    endif()
endif()

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
        fmod
)
