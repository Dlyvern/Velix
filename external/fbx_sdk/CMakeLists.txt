cmake_minimum_required(VERSION 3.16)

project(fbx_sdk LANGUAGES CXX)

set(FBX_SDK_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/include")

if(WIN32)
    set(FBX_PLATFORM "windows")
elseif(APPLE)
    set(FBX_PLATFORM "macos")
else()
    set(FBX_PLATFORM "linux")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

if(WIN32)
    set(FBX_LIB_DIR "${CMAKE_CURRENT_LIST_DIR}/lib/${FBX_PLATFORM}/${CMAKE_BUILD_TYPE}")
    set(FBX_LIB "${FBX_LIB_DIR}/libfbxsdk-md.lib")
elseif(APPLE)
    set(FBX_LIB_DIR "${CMAKE_CURRENT_LIST_DIR}/lib/${FBX_PLATFORM}/${CMAKE_BUILD_TYPE}")
    set(FBX_LIB "${FBX_LIB_DIR}/libfbxsdk.dylib")
else()
    set(FBX_LIB_DIR "${CMAKE_CURRENT_LIST_DIR}/lib/${FBX_PLATFORM}/${CMAKE_BUILD_TYPE}")
    set(FBX_LIB "${FBX_LIB_DIR}/libfbxsdk.so")
endif()

if(NOT EXISTS "${FBX_LIB}")
    message(FATAL_ERROR "FBX SDK library not found: ${FBX_LIB}")
endif()

add_library(fbx_sdk STATIC IMPORTED GLOBAL)

set_target_properties(fbx_sdk PROPERTIES
    IMPORTED_LOCATION "${FBX_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${FBX_SDK_INCLUDE_DIR}"
)

message(STATUS "FBX SDK: ${FBX_LIB}, includes: ${FBX_SDK_INCLUDE_DIR}")