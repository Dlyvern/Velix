cmake_minimum_required(VERSION 3.22)
project(VelixSDK VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(${PROJECT_NAME} SHARED
    src/VXObject.cpp
    src/VXActor.cpp
)

add_library(VelixSDK::VelixSDK ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Core/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Engine/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/external/glm>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/external/physx/PhysX/physx/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        VelixCore
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE VelixEngine)
endif()

target_compile_definitions(${PROJECT_NAME}
    PUBLIC
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_FORCE_RADIANS
    PRIVATE
        VELIX_SDK_BUILD
)

install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
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

install(DIRECTORY
    ${CMAKE_SOURCE_DIR}/external/glm/glm
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

