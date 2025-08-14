cmake_minimum_required(VERSION 3.7)
project(Velix)

set(CMAKE_CXX_STANDARD 23)
set(INCLUDE_DIR include)
set(SRC_DIR src)
set(LIBRARIES_DIR libraries)

file(GLOB_RECURSE PROJECT_HEADERS  ${INCLUDE_DIR}/*.h ${INCLUDE_DIR}/*.hpp)
file(GLOB_RECURSE PROJECT_SOURCES ${SRC_DIR}/*.cpp)

find_package(VelixFlow QUIET)

add_executable(${PROJECT_NAME}
        ${PROJECT_SOURCES}
        ${PROJECT_HEADERS}
)


option(VELIX_USE_PHYSX "Enable NVIDIA PhysX support" ON)

if(NOT VelixFlow_FOUND)
    message(STATUS "VelixFlow not found - cloning from GitHub...")

    include(FetchContent)

    FetchContent_Declare(
            VelixFlow
            GIT_REPOSITORY https://github.com/Dlyvern/VelixFlow
            GIT_TAG        main
            GIT_SHALLOW    TRUE
    )

    set(VelixFlow_INSTALL_DIR ${CMAKE_BINARY_DIR}/_deps/install)
    set(CMAKE_INSTALL_PREFIX ${VelixFlow_INSTALL_DIR})

    FetchContent_MakeAvailable(VelixFlow)

    message(STATUS "VelixFlow cloned and built from source")
else()
        message(STATUS "Found VelixFlow: ${VelixFlow_DIR}")
        add_custom_command(
                TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${VelixFlow_DIR}/../../../include/VelixFlow/"
                "${CMAKE_BINARY_DIR}/include/VelixFlow"
        )

        get_filename_component(VELIXFLOW_INSTALL_PREFIX "${VelixFlow_DIR}/../.." ABSOLUTE)

        set(VELIXFLOW_LIB_DIR "${VELIXFLOW_INSTALL_PREFIX}")
        message(STATUS "VelixFlow libs should be in: ${VELIXFLOW_LIB_DIR}")

        file(GLOB VELIXFLOW_LIBS
                "${VELIXFLOW_LIB_DIR}/libVelixFlow.a*"
                "${VELIXFLOW_LIB_DIR}/libVelixGL.so*"
                "${VELIXFLOW_LIB_DIR}/libVelixVK.so*"
                "${VELIXFLOW_LIB_DIR}/libfmod.so.14*"
        )

        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

        foreach(lib IN LISTS VELIXFLOW_LIBS)
    if(EXISTS "${lib}")
        add_custom_command(
            TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${lib}"
            "${CMAKE_BINARY_DIR}/lib"
        )
        message(STATUS "FOUND: ${lib}")
    else()
        message(WARNING "VelixFlow runtime library not found: ${lib}")
    endif()
            endforeach()
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
        ${INCLUDE_DIR}
        ${LIBRARIES_DIR}/stb
)

target_link_libraries(${PROJECT_NAME}
        PRIVATE
        Velix::VelixFlow
)

if(UNIX)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH "$ORIGIN/lib"
        INSTALL_RPATH "$ORIGIN/lib"
    )
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR})

target_sources(${PROJECT_NAME} PRIVATE
        ${PROJECT_SOURCES} 
        ${PROJECT_HEADERS}
)

add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/resources
    ${CMAKE_BINARY_DIR}/resources
)


install(TARGETS ${PROJECT_NAME} DESTINATION .)
install(DIRECTORY resources DESTINATION .)
install(DIRECTORY template DESTINATION .)
install(DIRECTORY ProjectTemplate DESTINATION .)
install(DIRECTORY "${CMAKE_BINARY_DIR}/lib" DESTINATION .)