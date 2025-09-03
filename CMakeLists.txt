cmake_minimum_required(VERSION 3.16)
project(Velix LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(external)

add_subdirectory(Core)

add_subdirectory(Engine)

add_subdirectory(Editor)

add_executable(${PROJECT_NAME} main.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE VelixEngine VelixCore VelixEditor)