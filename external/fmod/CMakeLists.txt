cmake_minimum_required(VERSION 3.16)

project(fmod_sdk LANGUAGES CXX)

set(FMOD_SDK_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/include")

if(WIN32)
    set(FMOD_LIB_DIR "${CMAKE_CURRENT_LIST_DIR}/lib/windows")
    set(FMOD_IMPLIB "${FMOD_LIB_DIR}/fmod_vc.lib")
    set(FMOD_RUNTIME_LIBRARY "${FMOD_LIB_DIR}/fmod.dll")
else()
    set(FMOD_LIB_DIR "${CMAKE_CURRENT_LIST_DIR}/lib/linux")
    set(FMOD_RUNTIME_LIBRARY "${FMOD_LIB_DIR}/libfmod.so.14.8")
endif()

if(WIN32 AND NOT EXISTS "${FMOD_IMPLIB}")
    message(FATAL_ERROR "FMOD import library not found: ${FMOD_IMPLIB}")
endif()

if(NOT EXISTS "${FMOD_RUNTIME_LIBRARY}")
    message(FATAL_ERROR "FMOD runtime library not found: ${FMOD_RUNTIME_LIBRARY}")
endif()

add_library(fmod SHARED IMPORTED GLOBAL)

if(WIN32)
    set_target_properties(fmod PROPERTIES
        IMPORTED_LOCATION "${FMOD_RUNTIME_LIBRARY}"
        IMPORTED_IMPLIB "${FMOD_IMPLIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${FMOD_SDK_INCLUDE_DIR}"
    )
else()
    set_target_properties(fmod PROPERTIES
        IMPORTED_LOCATION "${FMOD_RUNTIME_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FMOD_SDK_INCLUDE_DIR}"
    )
endif()

set(FMOD_RUNTIME_LIBRARY "${FMOD_RUNTIME_LIBRARY}" CACHE FILEPATH "Path to FMOD runtime library" FORCE)
message(STATUS "FMOD: ${FMOD_RUNTIME_LIBRARY}, includes: ${FMOD_SDK_INCLUDE_DIR}")
