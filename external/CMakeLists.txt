# Disable third-party test targets by default.
set(BUILD_TESTING OFF CACHE BOOL "Disable third-party tests by default" FORCE)

# nlohmann_json
set(JSON_BuildTests OFF CACHE BOOL "Disable nlohmann_json tests" FORCE)
set(JSON_Install OFF CACHE BOOL "Disable nlohmann_json install target" FORCE)
add_subdirectory(json)

# glfw
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Disable GLFW examples" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "Disable GLFW tests" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "Disable GLFW docs" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "Disable GLFW install target" FORCE)
add_subdirectory(glfw)

# tinyobjloader
set(TINYOBJLOADER_WITH_PYTHON OFF CACHE BOOL "Disable tinyobjloader Python module" FORCE)
set(TINYOBJLOADER_BUILD_TEST_LOADER OFF CACHE BOOL "Disable tinyobjloader test loader" FORCE)
set(TINYOBJLOADER_BUILD_OBJ_STICHER OFF CACHE BOOL "Disable tinyobjloader obj sticher tool" FORCE)
add_subdirectory(tinyobjloader)

# volk
set(VOLK_INSTALL OFF CACHE BOOL "Disable volk install target" FORCE)
add_subdirectory(volk)

# fbx sdk wrapper
add_subdirectory(fbx_sdk)

# fmod sdk wrapper
add_subdirectory(fmod)

# OpenEXR / Imath / OpenJPH
set(OPENEXR_BUILD_LIBS ON CACHE BOOL "Build OpenEXR libraries" FORCE)
set(OPENEXR_BUILD_TOOLS OFF CACHE BOOL "Disable OpenEXR tools" FORCE)
set(OPENEXR_BUILD_EXAMPLES OFF CACHE BOOL "Disable OpenEXR examples" FORCE)
set(OPENEXR_BUILD_PYTHON OFF CACHE BOOL "Disable OpenEXR python bindings" FORCE)
set(OPENEXR_BUILD_OSS_FUZZ OFF CACHE BOOL "Disable OpenEXR oss-fuzz targets" FORCE)
set(OPENEXR_TEST_LIBRARIES OFF CACHE BOOL "Disable OpenEXR library tests" FORCE)
set(OPENEXR_TEST_TOOLS OFF CACHE BOOL "Disable OpenEXR tool tests" FORCE)
set(OPENEXR_TEST_PYTHON OFF CACHE BOOL "Disable OpenEXR python tests" FORCE)
set(OPENEXR_INSTALL OFF CACHE BOOL "Disable OpenEXR install target" FORCE)
set(OPENEXR_INSTALL_TOOLS OFF CACHE BOOL "Disable OpenEXR tools install" FORCE)
set(OPENEXR_INSTALL_DEVELOPER_TOOLS OFF CACHE BOOL "Disable OpenEXR developer tools install" FORCE)
set(OPENEXR_INSTALL_PKG_CONFIG OFF CACHE BOOL "Disable OpenEXR pkg-config generation" FORCE)
set(OPENEXR_INSTALL_DOCS OFF CACHE BOOL "Disable OpenEXR docs install" FORCE)
set(BUILD_WEBSITE OFF CACHE BOOL "Disable OpenEXR website generation" FORCE)
set(OJPH_BUILD_TESTS OFF CACHE BOOL "Disable OpenJPH tests" FORCE)
set(OJPH_BUILD_EXECUTABLES OFF CACHE BOOL "Disable OpenJPH executables" FORCE)
set(OJPH_BUILD_STREAM_EXPAND OFF CACHE BOOL "Disable OpenJPH stream_expand tool" FORCE)
add_subdirectory(openexr)

# glm
set(GLM_BUILD_TESTS OFF CACHE BOOL "Disable GLM tests" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "Disable GLM install target" FORCE)
add_subdirectory(glm) # glm::glm

# SPIRV headers
set(SPIRV_HEADERS_ENABLE_TESTS OFF CACHE BOOL "Disable SPIRV-Headers tests" FORCE)
set(SPIRV_HEADERS_ENABLE_INSTALL OFF CACHE BOOL "Disable SPIRV-Headers install target" FORCE)
add_subdirectory(SPIRV-Headers)

# SPIRV-Cross
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "Disable SPIRV-Cross tests" FORCE)
set(SPIRV_CROSS_CLI OFF CACHE BOOL "Disable SPIRV-Cross CLI" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "Disable SPIRV-Cross install target" FORCE)
add_subdirectory(SPIRV-Cross)

# PhysX wrapper
add_subdirectory(physx)

# VulkanMemoryAllocator
set(VMA_ENABLE_INSTALL OFF CACHE BOOL "Disable VMA install target" FORCE)
set(VMA_BUILD_DOCUMENTATION OFF CACHE BOOL "Disable VMA docs" FORCE)
set(VMA_BUILD_SAMPLES OFF CACHE BOOL "Disable VMA samples" FORCE)
add_subdirectory(VulkanMemoryAllocator)

# SPIRV-Tools
set(SPIRV_SKIP_TESTS ON CACHE BOOL "Skip building SPIRV-Tools tests" FORCE)
set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "Skip building SPIRV-Tools executables" FORCE)
set(SPIRV_TOOLS_BUILD_STATIC ON CACHE BOOL "Build SPIRV-Tools static libraries" FORCE)
set(SKIP_SPIRV_TOOLS_INSTALL ON CACHE BOOL "Disable SPIRV-Tools install target" FORCE)
add_subdirectory(SPIRV-Tools)

# glslang
set(GLSLANG_TESTS OFF CACHE BOOL "Disable glslang tests" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "Disable glslang install target" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "Disable glslang standalone binaries" FORCE)
add_subdirectory(glslang)

# zlib
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "Disable zlib example tests" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "Disable zlib install target" FORCE)
add_subdirectory(zlib)

# shaderc
set(SHADERC_SKIP_TESTS ON CACHE BOOL "Skip building shaderc tests" FORCE)
set(SHADERC_SKIP_EXAMPLES ON CACHE BOOL "Skip building shaderc examples" FORCE)
set(SHADERC_SKIP_EXECUTABLES ON CACHE BOOL "Skip building shaderc executables" FORCE)
set(SHADERC_SKIP_COPYRIGHT_CHECK ON CACHE BOOL "Skip shaderc copyright check target" FORCE)
set(SHADERC_SKIP_INSTALL ON CACHE BOOL "Disable shaderc install target" FORCE)
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

if(UNIX)
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
