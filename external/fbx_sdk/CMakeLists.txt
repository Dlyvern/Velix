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
    set(FBX_LIB_DIR "${CMAKE_CURRENT_LIST_DIR}/lib/${FBX_PLATFORM}/${CMAKE_BUILD_TYPE}" CACHE PATH "Path to FBX shared library")
    set(FBX_LIB "${FBX_LIB_DIR}/libfbxsdk.lib" CACHE PATH "Path to FBX shared library")
    set(FBX_DLL "${FBX_LIB_DIR}/libfbxsdk.dll" CACHE PATH "Path to FBX shared library")
elseif(APPLE)
    set(FBX_LIB_DIR "${CMAKE_CURRENT_LIST_DIR}/lib/${FBX_PLATFORM}/${CMAKE_BUILD_TYPE}" CACHE PATH "Path to FBX shared library")
    set(FBX_LIB "${FBX_LIB_DIR}/libfbxsdk.a" CACHE PATH "Path to FBX shared library")
    set(FBX_DLL "${FBX_LIB_DIR}/libfbxsdk.dylib" CACHE PATH "Path to FBX shared library")
else()
    set(FBX_LIB_DIR "${CMAKE_CURRENT_LIST_DIR}/lib/${FBX_PLATFORM}/${CMAKE_BUILD_TYPE}" CACHE PATH "Path to FBX shared library")
    set(FBX_LIB "${FBX_LIB_DIR}/libfbxsdk.a" CACHE PATH "Path to FBX shared library")
    set(FBX_DLL "${FBX_LIB_DIR}/libfbxsdk.so" CACHE PATH "Path to FBX shared library")
endif()

if(NOT EXISTS "${FBX_DLL}")
    message(FATAL_ERROR "FBX SDK library not found: ${FBX_DLL}")
endif()

if(NOT EXISTS "${FBX_LIB}")
    message(FATAL_ERROR "FBX SDK library not found: ${FBX_LIB}")
endif()

add_library(fbx_sdk SHARED IMPORTED GLOBAL)

set_target_properties(fbx_sdk PROPERTIES
    IMPORTED_LOCATION "${FBX_DLL}"
    IMPORTED_IMPLIB "${FBX_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${FBX_SDK_INCLUDE_DIR}"
)

message(STATUS "FBX SDK: ${FBX_DLL}, includes: ${FBX_SDK_INCLUDE_DIR}")