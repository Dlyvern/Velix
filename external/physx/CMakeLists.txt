cmake_minimum_required(VERSION 3.16)

project(physx LANGUAGES CXX)

if(APPLE)
    message(ERROR "PhysX is not supported on macOS. Disabling PhysX.")
    return()
endif()

set(PHYSX_EXTERNAL_ROOT ${CMAKE_CURRENT_LIST_DIR})
set(PHYSX_ROOT ${PHYSX_EXTERNAL_ROOT}/PhysX/physx)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_PHYSX_BUILD_TYPE_DEFAULT "checked")
else()
    set(_PHYSX_BUILD_TYPE_DEFAULT "release")
endif()

set(PHYSX_BUILD_TYPE "${_PHYSX_BUILD_TYPE_DEFAULT}" CACHE STRING "PhysX build type (debug/checked/profile/release)")
set_property(CACHE PHYSX_BUILD_TYPE PROPERTY STRINGS debug checked profile release)

message(STATUS "PhysX build type: ${PHYSX_BUILD_TYPE} (${CMAKE_BUILD_TYPE})")

if(WIN32)
    set(PHYSX_LIB_DIR "${PHYSX_EXTERNAL_ROOT}/lib/windows/${PHYSX_BUILD_TYPE}")
    set(PHYSX_LIBS_TO_CHECK
        "${PHYSX_LIB_DIR}/PhysX_64.lib"
        "${PHYSX_LIB_DIR}/PhysXPvdSDK_static_64.lib"
        "${PHYSX_LIB_DIR}/PhysXVehicle2_static_64.lib"
        "${PHYSX_LIB_DIR}/PhysXCharacterKinematic_static_64.lib"
        "${PHYSX_LIB_DIR}/PhysXExtensions_static_64.lib"
        "${PHYSX_LIB_DIR}/PhysXCooking_64.lib"
        "${PHYSX_LIB_DIR}/PhysXCommon_64.lib"
        "${PHYSX_LIB_DIR}/PhysXFoundation_64.lib"
        "${PHYSX_LIB_DIR}/PhysXTask_static_64.lib"
    )
elseif(UNIX)
    set(PHYSX_LIB_DIR "${PHYSX_EXTERNAL_ROOT}/lib/linux/${PHYSX_BUILD_TYPE}")
    set(PHYSX_LIBS_TO_CHECK
        "${PHYSX_LIB_DIR}/libPhysX_static_64.a"
        "${PHYSX_LIB_DIR}/libPhysXPvdSDK_static_64.a"
        "${PHYSX_LIB_DIR}/libPhysXVehicle2_static_64.a"
        "${PHYSX_LIB_DIR}/libPhysXCharacterKinematic_static_64.a"
        "${PHYSX_LIB_DIR}/libPhysXExtensions_static_64.a"
        "${PHYSX_LIB_DIR}/libPhysXCooking_static_64.a"
        "${PHYSX_LIB_DIR}/libPhysXCommon_static_64.a"
        "${PHYSX_LIB_DIR}/libPhysXFoundation_static_64.a"
    )
endif()

set(PHYSX_PREBUILT_FOUND TRUE)
foreach(lib IN LISTS PHYSX_LIBS_TO_CHECK)
    if(NOT EXISTS "${lib}")
        message(STATUS "Missing prebuilt PhysX lib: ${lib}")
        set(PHYSX_PREBUILT_FOUND FALSE)
        break()
    endif()
endforeach()

if(NOT PHYSX_PREBUILT_FOUND)
    message(STATUS "Prebuilt PhysX not found. Building it manually...")

    if(WIN32)
        execute_process(
            COMMAND ${CMAKE_CURRENT_LIST_DIR}/scripts/build_physx_windows.bat
            WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
            RESULT_VARIABLE PHYSX_BUILD_RESULT
        )
    elseif(UNIX)
        execute_process(
            COMMAND bash ${CMAKE_CURRENT_LIST_DIR}/scripts/build_physx_linux.sh
            WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
            RESULT_VARIABLE PHYSX_BUILD_RESULT
        )
    endif()

    if(NOT PHYSX_BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to build PhysX")
    endif()
else()
    message(STATUS "Using prebuilt PhysX from ${PHYSX_LIB_DIR}")
endif()

add_library(${PROJECT_NAME} INTERFACE)

target_include_directories(${PROJECT_NAME} SYSTEM INTERFACE
        $<BUILD_INTERFACE:${PHYSX_ROOT}/include/>
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} INTERFACE
        ${PHYSX_LIB_DIR}/PhysX_64.lib
        ${PHYSX_LIB_DIR}/PhysXPvdSDK_static_64.lib
        ${PHYSX_LIB_DIR}/PhysXVehicle2_static_64.lib
        ${PHYSX_LIB_DIR}/PhysXCharacterKinematic_static_64.lib
        ${PHYSX_LIB_DIR}/PhysXExtensions_static_64.lib
        ${PHYSX_LIB_DIR}/PhysXCooking_64.lib
        ${PHYSX_LIB_DIR}/PhysXCommon_64.lib
        ${PHYSX_LIB_DIR}/PhysXFoundation_64.lib
        ${PHYSX_LIB_DIR}/PhysXTask_static_64.lib
    )
elseif(UNIX)
    target_link_libraries(${PROJECT_NAME} INTERFACE
        ${PHYSX_LIB_DIR}/libPhysX_static_64.a
        ${PHYSX_LIB_DIR}/libPhysXPvdSDK_static_64.a
        ${PHYSX_LIB_DIR}/libPhysXVehicle2_static_64.a
        ${PHYSX_LIB_DIR}/libPhysXCharacterKinematic_static_64.a
        ${PHYSX_LIB_DIR}/libPhysXExtensions_static_64.a
        ${PHYSX_LIB_DIR}/libPhysXCooking_static_64.a
        ${PHYSX_LIB_DIR}/libPhysXCommon_static_64.a
        ${PHYSX_LIB_DIR}/libPhysXFoundation_static_64.a
    )
endif()