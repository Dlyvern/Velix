cmake_minimum_required(VERSION 3.16)
project(Velix LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    
    add_compile_options(
        "$<$<CONFIG:Debug>:/MTd>"
        "$<$<CONFIG:Release>:/MT>"
    )

    add_compile_options(/std:c++20)
endif()

if(WIN32)
    set(VELIX_CMAKE_EXECUTABLE_NAME "cmake.exe")
else()
    set(VELIX_CMAKE_EXECUTABLE_NAME "cmake")
endif()

set(VELIX_BUNDLED_CMAKE_DIR "" CACHE PATH "Path to bundled CMake root directory (contains bin/${VELIX_CMAKE_EXECUTABLE_NAME})")

add_subdirectory(external)

add_subdirectory(Core)

add_subdirectory(Engine)

add_subdirectory(Editor)

add_subdirectory(VelixSDK)

add_subdirectory(Tools)

add_executable(${PROJECT_NAME} main.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE VelixEngine VelixCore VelixEditor)

# Required for runtime-loaded game modules that reference engine symbols
# (e.g. ScriptsRegister::instance from REGISTER_SCRIPT).
set_target_properties(${PROJECT_NAME} PROPERTIES ENABLE_EXPORTS ON)

if(UNIX)
    target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--export-dynamic")
endif()

set(RESOURCE_DIR "${CMAKE_SOURCE_DIR}/resources")
set(OUTPUT_RESOURCE_DIR "${CMAKE_BINARY_DIR}/resources")

if(EXISTS ${RESOURCE_DIR})
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_RESOURCE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory 
                "${RESOURCE_DIR}" 
                "${OUTPUT_RESOURCE_DIR}"
        COMMENT "Copying resources to build directory"
    )
    message(STATUS "Resources will be copied to: ${OUTPUT_RESOURCE_DIR}")
else()
    message(WARNING "Resource directory not found: ${RESOURCE_DIR}")
endif()

set(VELIX_BUNDLED_CMAKE_DIR_RESOLVED "")
if(VELIX_BUNDLED_CMAKE_DIR)
    if(EXISTS "${VELIX_BUNDLED_CMAKE_DIR}/bin/${VELIX_CMAKE_EXECUTABLE_NAME}")
        set(VELIX_BUNDLED_CMAKE_DIR_RESOLVED "${VELIX_BUNDLED_CMAKE_DIR}")
    else()
        message(WARNING "VELIX_BUNDLED_CMAKE_DIR is set but no executable was found at ${VELIX_BUNDLED_CMAKE_DIR}/bin/${VELIX_CMAKE_EXECUTABLE_NAME}")
    endif()
else()
    set(_velix_cmake_candidates
        "${CMAKE_SOURCE_DIR}/tools/cmake"
        "${CMAKE_SOURCE_DIR}/cmake"
        "${CMAKE_SOURCE_DIR}/third_party/cmake"
        "${CMAKE_SOURCE_DIR}/external/cmake"
    )

    foreach(_candidate IN LISTS _velix_cmake_candidates)
        if(EXISTS "${_candidate}/bin/${VELIX_CMAKE_EXECUTABLE_NAME}")
            set(VELIX_BUNDLED_CMAKE_DIR_RESOLVED "${_candidate}")
            break()
        endif()
    endforeach()
endif()

if(VELIX_BUNDLED_CMAKE_DIR_RESOLVED)
    set(VELIX_BUNDLED_CMAKE_BUILD_DIR "${CMAKE_BINARY_DIR}/tools/cmake")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VELIX_BUNDLED_CMAKE_BUILD_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${VELIX_BUNDLED_CMAKE_DIR_RESOLVED}"
                "${VELIX_BUNDLED_CMAKE_BUILD_DIR}"
        COMMENT "Copying bundled CMake to build directory"
    )
    message(STATUS "Bundled CMake will be copied from: ${VELIX_BUNDLED_CMAKE_DIR_RESOLVED}")
else()
    message(WARNING "Bundled CMake not found. Editor will use PATH cmake unless VELIX_CMAKE is provided.")
endif()

if(UNIX)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH "$ORIGIN;$ORIGIN/lib"
        INSTALL_RPATH "$ORIGIN;$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
    )
endif()


if(WIN32)
    set(DYNAMIC_LIB_EXTENSION ".dll")
else()
    set(DYNAMIC_LIB_EXTENSION ".so")
endif()

if(EXISTS ${FBX_DLL})
    # Copy next to the executable so the OS loader finds it at runtime
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FBX_DLL}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/libfbxsdk${DYNAMIC_LIB_EXTENSION}"
        COMMENT "Copying FBX SDK next to executable"
    )
    message(STATUS "FBX SDK will be copied next to executable")
else()
    message(WARNING "FBX SDK not found: ${FBX_DLL}")
endif()

set(FMOD_WINDOWS_RUNTIME_CANDIDATE "${CMAKE_SOURCE_DIR}/external/fmod/lib/windows/fmod.dll")
set(FMOD_LINUX_RUNTIME_CANDIDATE "${CMAKE_SOURCE_DIR}/external/fmod/lib/linux/libfmod.so.14.8")
set(FMOD_BUILD_RUNTIMES_FOUND FALSE)

if(EXISTS "${FMOD_WINDOWS_RUNTIME_CANDIDATE}")
    set(FMOD_BUILD_RUNTIMES_FOUND TRUE)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/lib"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FMOD_WINDOWS_RUNTIME_CANDIDATE}"
            "${CMAKE_BINARY_DIR}/lib/fmod.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FMOD_WINDOWS_RUNTIME_CANDIDATE}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/fmod.dll"
        COMMENT "Copying FMOD Windows runtime to build output"
    )
    message(STATUS "FMOD Windows runtime will be copied to: ${CMAKE_BINARY_DIR}/lib/fmod.dll")
endif()

if(EXISTS "${FMOD_LINUX_RUNTIME_CANDIDATE}")
    set(FMOD_BUILD_RUNTIMES_FOUND TRUE)

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>"

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FMOD_LINUX_RUNTIME_CANDIDATE}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/libfmod.so.14.8"

        # Force-recreate symlinks so rebuilds don't leave stale ones.
        # cmake -E create_symlink fails if the link already exists, so remove first.
        COMMAND ${CMAKE_COMMAND} -E remove
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/libfmod.so.14"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/libfmod.so"

        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "libfmod.so.14.8"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/libfmod.so.14"

        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "libfmod.so.14"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/libfmod.so"

        COMMENT "Copying FMOD Linux runtime next to executable"
    )

    message(STATUS "FMOD Linux runtime will be copied next to the executable")
endif()
if(NOT FMOD_BUILD_RUNTIMES_FOUND)
    message(WARNING "FMOD runtime libraries were not found under external/fmod/lib")
endif()

if(WIN32 AND PHYSX_RUNTIME_DLLS)
    foreach(PHYSX_RUNTIME_DLL IN LISTS PHYSX_RUNTIME_DLLS)
        get_filename_component(_velix_physx_runtime_name "${PHYSX_RUNTIME_DLL}" NAME)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PHYSX_RUNTIME_DLL}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/${_velix_physx_runtime_name}"
            COMMENT "Copying PhysX runtime ${_velix_physx_runtime_name} next to executable"
        )
    endforeach()
    message(STATUS "PhysX runtime DLLs will be copied next to executable")
elseif(WIN32)
    message(WARNING "PhysX runtime DLLs were not found. Windows executable may fail to launch unless PhysX DLLs are on PATH.")
endif()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

if(EXISTS ${RESOURCE_DIR})
    install(DIRECTORY ${RESOURCE_DIR}/
        DESTINATION resources
        USE_SOURCE_PERMISSIONS
    )
    message(STATUS "Resources will be installed to: resources/")
endif()

if(VELIX_BUNDLED_CMAKE_DIR_RESOLVED)
    install(DIRECTORY "${VELIX_BUNDLED_CMAKE_DIR_RESOLVED}/"
        DESTINATION tools/cmake
        USE_SOURCE_PERMISSIONS
    )
endif()

if(EXISTS ${FBX_DLL})
    if(WIN32)
        install(FILES ${FBX_DLL} DESTINATION ${CMAKE_INSTALL_BINDIR})
    else()
        install(FILES ${FBX_DLL} DESTINATION ${CMAKE_INSTALL_LIBDIR})
    endif()
endif()

if(WIN32 AND EXISTS "${FMOD_WINDOWS_RUNTIME_CANDIDATE}")
    install(FILES "${FMOD_WINDOWS_RUNTIME_CANDIDATE}" DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(WIN32 AND PHYSX_RUNTIME_DLLS)
    install(FILES ${PHYSX_RUNTIME_DLLS} DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(UNIX AND EXISTS "${FMOD_LINUX_RUNTIME_CANDIDATE}")
    install(FILES "${FMOD_LINUX_RUNTIME_CANDIDATE}" DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES "${FMOD_LINUX_RUNTIME_CANDIDATE}" DESTINATION ${CMAKE_INSTALL_LIBDIR} RENAME "libfmod.so.14")
    install(FILES "${FMOD_LINUX_RUNTIME_CANDIDATE}" DESTINATION ${CMAKE_INSTALL_LIBDIR} RENAME "libfmod.so")
endif()

set(CPACK_PACKAGE_NAME "VelixEngine")
set(CPACK_PACKAGE_VERSION "0.0.0")
set(CPACK_PACKAGE_VENDOR "Dlyvern")
set(CPACK_PACKAGE_DESCRIPTION "Velix Game Engine")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Dlyvern/Velix")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

if(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
else()
    set(CPACK_GENERATOR "ZIP;DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "wf-sergey@mail.ru")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libvulkan1, libglfw3, libx11-6")
endif()

include(CPack)
