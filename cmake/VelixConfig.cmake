cmake_minimum_required(VERSION 3.16)

# VelixConfig.cmake — find_package(Velix) support
# Locates the installed Velix SDK: headers + shared libraries.
#
# Defines:
#   Velix::Engine   — VelixEngine shared library
#   Velix::Core     — VelixCore shared library
#   Velix::SDK      — VelixSDK shared library

get_filename_component(_VELIX_INSTALL_PREFIX "${CMAKE_CURRENT_LIST_FILE}" PATH)
get_filename_component(_VELIX_INSTALL_PREFIX "${_VELIX_INSTALL_PREFIX}" PATH)

set(_VELIX_INCLUDE_DIR "${_VELIX_INSTALL_PREFIX}/include")
set(_VELIX_LIB_DIR     "${_VELIX_INSTALL_PREFIX}/lib")

foreach(_lib Engine Core SDK)
    set(_so "${_VELIX_LIB_DIR}/libVelix${_lib}.so")
    if(NOT EXISTS "${_so}")
        set(_so "${_VELIX_LIB_DIR}/libVelix${_lib}.dll")
    endif()
    if(NOT EXISTS "${_so}")
        message(FATAL_ERROR "VelixConfig: could not find libVelix${_lib} in ${_VELIX_LIB_DIR}")
    endif()

    if(NOT TARGET Velix::${_lib})
        add_library(Velix::${_lib} SHARED IMPORTED)
        set_target_properties(Velix::${_lib} PROPERTIES
            IMPORTED_LOCATION             "${_so}"
            INTERFACE_INCLUDE_DIRECTORIES "${_VELIX_INCLUDE_DIR}"
        )
    endif()
endforeach()

set(Velix_FOUND TRUE)
