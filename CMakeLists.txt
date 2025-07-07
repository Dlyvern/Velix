cmake_minimum_required(VERSION 3.7)
project(Velix)

set(CMAKE_CXX_STANDARD 23)
set(INCLUDE_DIR include)
set(SRC_DIR src)
set(LIBRARIES_DIR libraries)

file(GLOB_RECURSE PROJECT_HEADERS  ${INCLUDE_DIR}/*.h ${INCLUDE_DIR}/*.hpp)
file(GLOB_RECURSE PROJECT_SOURCES ${SRC_DIR}/*.cpp)

find_package(VelixFlow QUIET)

if(NOT VelixFlow_FOUND)
    message(STATUS "VelixFlow not found - cloning from GitHub...")

    include(FetchContent)

    FetchContent_Declare(
            VelixFlow
            GIT_REPOSITORY https://github.com/Dlyvern/VelixFlow
            GIT_TAG        main
            GIT_SHALLOW    TRUE
    )

    FetchContent_MakeAvailable(VelixFlow)

    message(STATUS "VelixFlow cloned and built from source")
else()
    message(STATUS "Found VelixFlow: ${VelixFlow_DIR}")
endif()

add_executable(${PROJECT_NAME}
        ${PROJECT_SOURCES}
        ${PROJECT_HEADERS}
)

target_include_directories(${PROJECT_NAME} PRIVATE
        ${INCLUDE_DIR}
        ${LIBRARIES_DIR}/2ndParty/imgui
        ${LIBRARIES_DIR}/2ndParty/ImGuizmo
        ${LIBRARIES_DIR}/stb
)

target_link_libraries(${PROJECT_NAME}
        PRIVATE
        Velix::VelixFlow
)
option(VELIX_USE_PHYSX "Enable NVIDIA PhysX support" ON)

target_sources(${PROJECT_NAME} PRIVATE
        ${PROJECT_SOURCES} 
        ${PROJECT_HEADERS}
        libraries/2ndParty/imgui/imgui.cpp
        libraries/2ndParty/imgui/imgui_draw.cpp
        libraries/2ndParty/imgui/imgui_widgets.cpp
        libraries/2ndParty/imgui/imgui_tables.cpp
        libraries/2ndParty/imgui/backends/imgui_impl_glfw.cpp
        libraries/2ndParty/imgui/backends/imgui_impl_opengl3.cpp

        libraries/2ndParty/ImGuizmo/GraphEditor.cpp
        libraries/2ndParty/ImGuizmo/ImCurveEdit.cpp
        libraries/2ndParty/ImGuizmo/ImGradient.cpp
        libraries/2ndParty/ImGuizmo/ImGuizmo.cpp
        libraries/2ndParty/ImGuizmo/ImSequencer.cpp

        libraries/2ndParty/ImGuiColorTextEdit/TextEditor.cpp
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