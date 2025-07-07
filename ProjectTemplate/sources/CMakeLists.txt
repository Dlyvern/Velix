cmake_minimum_required(VERSION 3.15)
project(GameLib)

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

find_package(VelixFlow REQUIRED)

add_library(GameLib SHARED ${SOURCE_FILES}
)

target_include_directories(GameLib PRIVATE ${VELIX_FLOW_INCLUDE})
target_link_libraries(GameLib PRIVATE  Velix::VelixFlow)