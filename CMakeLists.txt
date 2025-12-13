cmake_minimum_required(VERSION 3.16)
project(Velix LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    
    add_compile_options(
        "$<$<CONFIG:Debug>:/MTd>"
        "$<$<CONFIG:Release>:/MT>"
    )

    add_compile_options(/std:c++20)
endif()

add_subdirectory(external)

add_subdirectory(Core)

add_subdirectory(Engine)

add_subdirectory(Editor)

add_subdirectory(VelixSDK)

add_executable(${PROJECT_NAME} main.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE VelixEngine VelixCore VelixEditor)

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

set_target_properties(${PROJECT_NAME} PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/lib"
)

if(WIN32)
    set(DYNAMIC_LIB_EXTENSION ".dll")
elseif(APPLE)
    set(DYNAMIC_LIB_EXTENSION ".dylib")
else()
    set(DYNAMIC_LIB_EXTENSION ".so")
endif()

if(EXISTS ${FBX_DLL})
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/lib"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FBX_DLL}"
            "${CMAKE_BINARY_DIR}/lib/libfbxsdk${DYNAMIC_LIB_EXTENSION}"
        COMMENT "Copying FBX SDK to build directory"
    )
    message(STATUS "FBX SDK will be copied to: ${CMAKE_BINARY_DIR}/lib/")
else()
    message(WARNING "FBX SDK not found: ${FBX_DLL}")
endif()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION .
    BUNDLE DESTINATION .
    LIBRARY DESTINATION lib
)

if(EXISTS ${RESOURCE_DIR})
    install(DIRECTORY ${RESOURCE_DIR}/
        DESTINATION resources
        USE_SOURCE_PERMISSIONS
    )
    message(STATUS "Resources will be installed to: resources/")
endif()

install(FILES ${FBX_DLL} DESTINATION lib)

set(CPACK_PACKAGE_NAME "VelixEngine")
set(CPACK_PACKAGE_VERSION "0.0.0")
set(CPACK_PACKAGE_VENDOR "Dlyvern")
set(CPACK_PACKAGE_DESCRIPTION "Velix Game Engine")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Dlyvern/Velix")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

if(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
elseif(APPLE)
    set(CPACK_GENERATOR "ZIP;DragNDrop")
else()
    set(CPACK_GENERATOR "ZIP;DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "wf-sergey@mail.ru")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libvulkan1, libglfw3, libx11-6")
endif()

include(CPack)