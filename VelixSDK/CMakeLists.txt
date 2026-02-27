cmake_minimum_required(VERSION 3.22)
project(VelixSDK VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(${PROJECT_NAME} SHARED
    src/VXObject.cpp
)

add_library(VelixSDK::VelixSDK ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Core/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Engine/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        VelixEngine
        VelixCore
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        VELIX_SDK_BUILD
)

install(TARGETS ${PROJECT_NAME}
    EXPORT VelixSDKTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES
    ${CMAKE_SOURCE_DIR}/Core/include/Core/Macros.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Core
)

install(FILES
    ${CMAKE_SOURCE_DIR}/Engine/include/Engine/Scripting/Script.hpp
    ${CMAKE_SOURCE_DIR}/Engine/include/Engine/Scripting/ScriptsRegister.hpp
    ${CMAKE_SOURCE_DIR}/Engine/include/Engine/Scripting/ScriptMacroses.hpp
    ${CMAKE_SOURCE_DIR}/Engine/include/Engine/Scripting/VelixAPI.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Engine/Scripting
)

export(EXPORT VelixSDKTargets
    FILE ${CMAKE_BINARY_DIR}/VelixSDKTargets.cmake
    NAMESPACE VelixSDK::
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/VelixSDKConfig.cmake.in
    ${CMAKE_BINARY_DIR}/VelixSDKConfig.cmake
    INSTALL_DESTINATION .
    INSTALL_PREFIX ${CMAKE_BINARY_DIR}
)

install(EXPORT VelixSDKTargets
    FILE VelixSDKTargets.cmake
    NAMESPACE VelixSDK::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VelixSDK
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/VelixSDKConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/VelixSDKConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VelixSDK
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/VelixSDKConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/VelixSDKConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/VelixSDKConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VelixSDK
)
