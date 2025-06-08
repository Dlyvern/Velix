cmake_minimum_required(VERSION 3.7)
project(Elixir)

set(CMAKE_CXX_STANDARD 23)
set(INCLUDE_DIR include)
set(SRC_DIR src)
set(LIBRARIES_DIR libraries)

file(GLOB_RECURSE PROJECT_HEADERS  ${INCLUDE_DIR}/*.h ${INCLUDE_DIR}/*.hpp)
file(GLOB_RECURSE PROJECT_SOURCES ${SRC_DIR}/*.cpp)

find_package(ElixirCore QUIET)

if(NOT ElixirCore_FOUND)
    message(STATUS "ElixirCore not found - cloning from GitHub...")

    include(FetchContent)

    FetchContent_Declare(
            ElixirCore
            GIT_REPOSITORY https://github.com/Dlyvern/ElixirCore
            GIT_TAG        main
            GIT_SHALLOW    TRUE
    )

    FetchContent_MakeAvailable(ElixirCore)

    message(STATUS "ElixirCore cloned and built from source")
else()
    message(STATUS "Found ElixirCore: ${ElixirCore_DIR}")
endif()

add_executable(${PROJECT_NAME}
        ${PROJECT_SOURCES}
        ${PROJECT_HEADERS}
)

target_include_directories(${PROJECT_NAME} PRIVATE
        ${INCLUDE_DIR}
        ${LIBRARIES_DIR}/imgui
        ${LIBRARIES_DIR}/ImGuizmo
        ${LIBRARIES_DIR}/stb
)

target_link_libraries(${PROJECT_NAME}
        PRIVATE
        Elixir::ElixirCore
)

target_sources(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCES} PRIVATE ${PROJECT_HEADERS}
        libraries/imgui/imgui.cpp
        libraries/imgui/imgui_draw.cpp
        libraries/imgui/imgui_widgets.cpp
        libraries/imgui/imgui_tables.cpp
        libraries/imgui/backends/imgui_impl_glfw.cpp
        libraries/imgui/backends/imgui_impl_opengl3.cpp

        libraries/ImGuizmo/GraphEditor.cpp
        libraries/ImGuizmo/ImCurveEdit.cpp
        libraries/ImGuizmo/ImGradient.cpp
        libraries/ImGuizmo/ImGuizmo.cpp
        libraries/ImGuizmo/ImSequencer.cpp
)