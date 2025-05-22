cmake_minimum_required(VERSION 3.7)
project(Elixir)

set(CMAKE_CXX_STANDARD 23)
set(INCLUDE_DIR include)
set(SRC_DIR src)
set(LIBRARIES_DIR libraries)
set(PHYSX_ROOT "~/Libraries/PhysX/physx")

file(GLOB_RECURSE PROJECT_HEADERS  ${INCLUDE_DIR}/*.h ${INCLUDE_DIR}/*.hpp)
file(GLOB_RECURSE PROJECT_SOURCES ${SRC_DIR}/*.cpp)

find_package(ElixirCore REQUIRED)

include_directories(/usr/local/include
        ${INCLUDE_DIR}
        ${LIBRARIES_DIR}/imgui
        ${LIBRARIES_DIR}/ImGuizmo
        ${LIBRARIES_DIR}/stb
        $<BUILD_INTERFACE:${PHYSX_ROOT}/include>
)

link_directories("${PHYSX_ROOT}/bin/linux.x86_64/${PHYSX_BUILD_TYPE}")
link_directories(/usr/local/lib)

add_executable(${PROJECT_NAME}
        ${PROJECT_SOURCES}
        ${PROJECT_HEADERS}
)

target_link_libraries(${PROJECT_NAME}
        PRIVATE
        Elixir::ElixirCore
        -Wl,--start-group
        PhysXExtensions_static_64
        PhysX_static_64
        PhysXPvdSDK_static_64
        PhysXCommon_static_64
        PhysXFoundation_static_64
        PhysXCharacterKinematic_static_64
        -Wl,--end-group


        TestGame
)

if(USE_VULKAN)
    target_include_directories(${PROJECT_NAME} PRIVATE API/VulkanShit/include)
    target_sources(${PROJECT_NAME} PRIVATE API/VulkanShit/src/Shader.cpp)
else()
    target_include_directories(${PROJECT_NAME} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/API/GLitch/include>)
    target_sources(${PROJECT_NAME} PRIVATE
            API/GLitch/src/Shader.cpp
            API/GLitch/src/Texture.cpp
    )
endif()


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