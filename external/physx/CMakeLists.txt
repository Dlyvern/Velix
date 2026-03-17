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
            COMMAND ${CMAKE_CURRENT_LIST_DIR}/scripts/build_physx_windows.bat ${PHYSX_BUILD_TYPE}
            WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
            RESULT_VARIABLE PHYSX_BUILD_RESULT
        )
    elseif(UNIX)
        execute_process(
            COMMAND bash ${CMAKE_CURRENT_LIST_DIR}/scripts/build_physx_linux.sh ${PHYSX_BUILD_TYPE}
            WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
            RESULT_VARIABLE PHYSX_BUILD_RESULT
        )
    endif()

    if(NOT PHYSX_BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to build PhysX")
    endif()

    foreach(lib IN LISTS PHYSX_LIBS_TO_CHECK)
        if(NOT EXISTS "${lib}")
            message(FATAL_ERROR "PhysX build completed, but the expected library is still missing: ${lib}")
        endif()
    endforeach()
else()
    message(STATUS "Using prebuilt PhysX from ${PHYSX_LIB_DIR}")
endif()

if(WIN32)
    set(_PHYSX_RUNTIME_DLLS "")

    file(GLOB _PHYSX_RUNTIME_DLLS_FROM_LIB_DIR "${PHYSX_LIB_DIR}/PhysX*.dll")
    list(APPEND _PHYSX_RUNTIME_DLLS ${_PHYSX_RUNTIME_DLLS_FROM_LIB_DIR})

    file(GLOB _PHYSX_RUNTIME_DLLS_FROM_BUILD_DIR "${PHYSX_ROOT}/bin/win.x86_64.*.mt/${PHYSX_BUILD_TYPE}/PhysX*.dll")
    list(APPEND _PHYSX_RUNTIME_DLLS ${_PHYSX_RUNTIME_DLLS_FROM_BUILD_DIR})

    list(REMOVE_DUPLICATES _PHYSX_RUNTIME_DLLS)
    list(SORT _PHYSX_RUNTIME_DLLS)

    if(_PHYSX_RUNTIME_DLLS)
        set(PHYSX_RUNTIME_DLLS "${_PHYSX_RUNTIME_DLLS}" CACHE STRING "PhysX runtime DLLs for Windows staging" FORCE)
        message(STATUS "PhysX runtime DLLs found: ${PHYSX_RUNTIME_DLLS}")
    else()
        set(PHYSX_RUNTIME_DLLS "" CACHE STRING "PhysX runtime DLLs for Windows staging" FORCE)
        message(WARNING "PhysX runtime DLLs were not found under ${PHYSX_LIB_DIR} or ${PHYSX_ROOT}/bin. Windows builds will require these DLLs on PATH.")
    endif()
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
        winmm
        Advapi32
        ws2_32
        Psapi
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

    # GPU PhysX: present when built with a CUDA-capable preset
    file(GLOB _PHYSX_GPU_SO "${PHYSX_LIB_DIR}/libPhysXGpu*.so")
    if(_PHYSX_GPU_SO)
        list(GET _PHYSX_GPU_SO 0 _PHYSX_GPU_SO_PATH)
        message(STATUS "PhysX GPU library found: ${_PHYSX_GPU_SO_PATH}")
        target_compile_definitions(${PROJECT_NAME} INTERFACE PHYSX_GPU_ENABLED=1)
        # Expose the .so path so the top-level build can copy it next to the executable
        set(PHYSX_GPU_SO_PATH "${_PHYSX_GPU_SO_PATH}" CACHE FILEPATH "Path to libPhysXGpu.so" FORCE)
    else()
        message(STATUS "PhysX GPU library not found in ${PHYSX_LIB_DIR} — CPU-only mode")
        set(PHYSX_GPU_SO_PATH "" CACHE FILEPATH "Path to libPhysXGpu.so" FORCE)
    endif()
endif()
