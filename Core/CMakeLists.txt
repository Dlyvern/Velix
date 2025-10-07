cmake_minimum_required(VERSION 3.16)
project(VelixCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(INCLUDE_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(SRC_DIR src)

file(GLOB_RECURSE PROJECT_HEADERS  ${INCLUDE_DIR}/*.h ${INCLUDE_DIR}/*.hpp)
file(GLOB_RECURSE PROJECT_SOURCES ${SRC_DIR}/*.cpp)

find_package(Vulkan REQUIRED)

add_library(${PROJECT_NAME} STATIC
        ${PROJECT_SOURCES}
        ${PROJECT_HEADERS}
)

target_include_directories(${PROJECT_NAME} PUBLIC
        ${INCLUDE_DIR}
        ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
        glfw
        volk
        stb
        glm::glm
)

if (WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif()
if (UNIX AND NOT APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE VK_USE_PLATFORM_XLIB_KHR)
endif()