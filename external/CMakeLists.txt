add_subdirectory(json)
add_subdirectory(glfw)
add_subdirectory(tinyobjloader)
add_subdirectory(volk)
add_subdirectory(fbx_sdk)
add_subdirectory(glm) #glm::glm
add_subdirectory(SPIRV-Headers)
add_subdirectory(SPIRV-Cross)
add_subdirectory(physx)
add_subdirectory(VulkanMemoryAllocator)
add_subdirectory(SPIRV-Tools)
add_subdirectory(glslang)

set(SHADERC_SKIP_TESTS ON CACHE BOOL "Skip building shaderc tests" FORCE)
set(SHADERC_SKIP_EXAMPLES ON CACHE BOOL "Skip building shaderc examples" FORCE)
set(SHADERC_ENABLE_INSTALL ON CACHE BOOL "Enable installation" FORCE)

set(SPIRV_SKIP_TESTS ON CACHE BOOL "Skip building SPIRV-Tools tests" FORCE)
set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "Skip building SPIRV-Tools executables" FORCE)
set(SPIRV_TOOLS_BUILD_STATIC ON CACHE BOOL "Build SPIRV-Tools static libraries" FORCE)

set(GLSANG_SKIP_TESTS ON CACHE BOOL "Skip building glslang tests" FORCE)
set(ENABLE_GLSLANG_INSTALL ON CACHE BOOL "Enable glslang install" FORCE)
add_subdirectory(shaderc)

target_compile_definitions(glm INTERFACE GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS)

add_library(stb INTERFACE)

target_include_directories(stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)


add_library(imgui STATIC
    ${CMAKE_CURRENT_LIST_DIR}/imgui/imgui.cpp
    ${CMAKE_CURRENT_LIST_DIR}/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_LIST_DIR}/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_LIST_DIR}/imgui/imgui_widgets.cpp

    ${CMAKE_CURRENT_LIST_DIR}/imgui/backends/imgui_impl_glfw.cpp
    ${CMAKE_CURRENT_LIST_DIR}/imgui/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/imgui
    ${CMAKE_CURRENT_LIST_DIR}/imgui/backends
)

target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK=1)

if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    find_package(Threads REQUIRED)
endif()

target_link_libraries(imgui PUBLIC volk glfw  $<$<PLATFORM_ID:Linux>:${X11_LIBRARIES}> $<$<PLATFORM_ID:Linux>:Threads::Threads>)


add_library(imgui_text_edit STATIC
    ${CMAKE_CURRENT_LIST_DIR}/ImGuiColorTextEdit/TextEditor.cpp
)

target_include_directories(imgui_text_edit PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/ImGuiColorTextEdit
)

target_link_libraries(imgui_text_edit PUBLIC imgui)


add_library(imguizmo STATIC
    ${CMAKE_CURRENT_LIST_DIR}/ImGuizmo/ImGuizmo.cpp
    ${CMAKE_CURRENT_LIST_DIR}/ImGuizmo/ImGradient.cpp
    ${CMAKE_CURRENT_LIST_DIR}/ImGuizmo/ImCurveEdit.cpp
    ${CMAKE_CURRENT_LIST_DIR}/ImGuizmo/GraphEditor.cpp
    ${CMAKE_CURRENT_LIST_DIR}/ImGuizmo/ImSequencer.cpp
)

target_include_directories(imguizmo PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/ImGuizmo/
)

target_link_libraries(imguizmo PUBLIC imgui)
