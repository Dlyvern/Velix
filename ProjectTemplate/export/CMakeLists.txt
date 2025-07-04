cmake_minimum_required(VERSION 3.15)
project(Game)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

foreach(file ${SOURCE_FILES})
    if("${file}" MATCHES "CMakeFiles")
        list(REMOVE_ITEM SOURCE_FILES ${file})
    endif()
endforeach()

find_package(ElixirCore REQUIRED)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE ${ELIXIR_CORE_INCLUDE})
target_link_libraries(${PROJECT_NAME} PRIVATE  Elixir::ElixirCore)