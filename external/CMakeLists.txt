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
if(WIN32)
    # GLFW stores process-wide state (initialization, monitors, windows, callbacks).
    # When it is linked statically into multiple Windows DLLs, each module gets its
    # own private copy of that state.  Velix initialises GLFW from Engine and creates
    # windows from Core, so Windows needs a single shared GLFW runtime.
    set(GLFW_LIBRARY_TYPE SHARED CACHE STRING "Build GLFW as a shared library on Windows" FORCE)
endif()
add_subdirectory(glfw)

# tinyobjloader
set(TINYOBJLOADER_WITH_PYTHON OFF CACHE BOOL "Disable tinyobjloader Python module" FORCE)
set(TINYOBJLOADER_BUILD_TEST_LOADER OFF CACHE BOOL "Disable tinyobjloader test loader" FORCE)
set(TINYOBJLOADER_BUILD_OBJ_STICHER OFF CACHE BOOL "Disable tinyobjloader obj sticher tool" FORCE)
add_subdirectory(tinyobjloader)

# volk
set(VOLK_INSTALL OFF CACHE BOOL "Disable volk install target" FORCE)
add_subdirectory(volk)

# freetype
set(FT_DISABLE_ZLIB TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI TRUE CACHE BOOL "" FORCE)
set(FT_ENABLE_ERROR_STRINGS FALSE CACHE BOOL "" FORCE)
# set(SKIP_INSTALL_ALL TRUE CACHE BOOL "" FORCE)
# set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(freetype)

# fbx sdk wrapper
add_subdirectory(fbx_sdk)

# fmod sdk wrapper
add_subdirectory(fmod)

# PhysX wrapper
add_subdirectory(physx)


# OpenEXR / Imath / OpenJPH
set(IMATH_BUILD_TESTING OFF CACHE BOOL "Disable Imath tests" FORCE)
set(IMATH_INSTALL OFF CACHE BOOL "Disable Imath install target" FORCE)
set(IMATH_INSTALL_PKG_CONFIG OFF CACHE BOOL "Disable Imath pkg-config generation" FORCE)
add_subdirectory(Imath)

# Force OpenEXR to skip find_package(Imath) entirely.
# Imath::Imath is already defined above via add_subdirectory, so OpenEXR will
# use it directly without triggering FetchContent (no internet needed).
set(OPENEXR_FORCE_INTERNAL_IMATH ON CACHE BOOL "Use already-added Imath target" FORCE)

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

# zlib — force static-only so the engine doesn't produce a z.dll dependency on Windows
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "Disable zlib example tests" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "Disable zlib install target" FORCE)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "Disable zlib shared library" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "Enable zlib static library" FORCE)
set(_velix_saved_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(zlib)
set(BUILD_SHARED_LIBS "${_velix_saved_BUILD_SHARED_LIBS}" CACHE BOOL "" FORCE)

if(TARGET zlibstatic AND NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()

# lz4
set(LZ4_BUNDLED_MODE ON CACHE BOOL "Build lz4 as a vendored dependency" FORCE)
set(LZ4_BUILD_CLI OFF CACHE BOOL "Disable lz4 CLI" FORCE)
set(LZ4_BUILD_LEGACY_LZ4C OFF CACHE BOOL "Disable lz4 legacy CLI" FORCE)
set(_velix_saved_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
set(_velix_saved_BUILD_STATIC_LIBS "${BUILD_STATIC_LIBS}")
set(_velix_had_BUILD_SHARED_LIBS FALSE)
set(_velix_had_BUILD_STATIC_LIBS FALSE)
if(DEFINED BUILD_SHARED_LIBS)
    set(_velix_had_BUILD_SHARED_LIBS TRUE)
endif()
if(DEFINED BUILD_STATIC_LIBS)
    set(_velix_had_BUILD_STATIC_LIBS TRUE)
endif()
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
add_subdirectory(lz4/build/cmake)
if(TARGET lz4 AND NOT TARGET LZ4::lz4)
    add_library(LZ4::lz4 ALIAS lz4)
endif()
if(_velix_had_BUILD_SHARED_LIBS)
    set(BUILD_SHARED_LIBS "${_velix_saved_BUILD_SHARED_LIBS}" CACHE BOOL "" FORCE)
else()
    unset(BUILD_SHARED_LIBS CACHE)
endif()
if(_velix_had_BUILD_STATIC_LIBS)
    set(BUILD_STATIC_LIBS "${_velix_saved_BUILD_STATIC_LIBS}" CACHE BOOL "" FORCE)
else()
    unset(BUILD_STATIC_LIBS CACHE)
endif()

# curl
set(_velix_saved_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
set(_velix_saved_BUILD_STATIC_LIBS "${BUILD_STATIC_LIBS}")
set(_velix_had_BUILD_SHARED_LIBS FALSE)
set(_velix_had_BUILD_STATIC_LIBS FALSE)
if(DEFINED BUILD_SHARED_LIBS)
    set(_velix_had_BUILD_SHARED_LIBS TRUE)
endif()
if(DEFINED BUILD_STATIC_LIBS)
    set(_velix_had_BUILD_STATIC_LIBS TRUE)
endif()
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "Disable curl executable" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Disable curl examples" FORCE)
set(CURL_DISABLE_INSTALL ON CACHE BOOL "Disable curl install target" FORCE)
set(HTTP_ONLY ON CACHE BOOL "Disable non-HTTP curl protocols" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "Disable LDAP in curl" FORCE)
set(CURL_DISABLE_LDAPS ON CACHE BOOL "Disable LDAPS in curl" FORCE)
set(CURL_ZLIB OFF CACHE BOOL "Disable zlib support in curl" FORCE)
set(CURL_BROTLI OFF CACHE BOOL "Disable brotli support in curl" FORCE)
set(CURL_ZSTD OFF CACHE BOOL "Disable zstd support in curl" FORCE)
set(USE_NGHTTP2 OFF CACHE BOOL "Disable HTTP/2 in curl" FORCE)
set(USE_LIBIDN2 OFF CACHE BOOL "Disable libidn2 in curl" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "Disable libpsl in curl" FORCE)
if(WIN32)
    set(CURL_USE_SCHANNEL ON CACHE BOOL "Use Schannel in bundled curl" FORCE)
    set(CURL_USE_OPENSSL OFF CACHE BOOL "Disable OpenSSL in bundled curl on Windows" FORCE)
else()
    set(CURL_USE_SCHANNEL OFF CACHE BOOL "Schannel is unavailable on non-Windows" FORCE)
    set(CURL_USE_OPENSSL ON CACHE BOOL "Use OpenSSL in bundled curl" FORCE)
endif()
add_subdirectory(curl)
if(_velix_had_BUILD_SHARED_LIBS)
    set(BUILD_SHARED_LIBS "${_velix_saved_BUILD_SHARED_LIBS}" CACHE BOOL "" FORCE)
else()
    unset(BUILD_SHARED_LIBS CACHE)
endif()
if(_velix_had_BUILD_STATIC_LIBS)
    set(BUILD_STATIC_LIBS "${_velix_saved_BUILD_STATIC_LIBS}" CACHE BOOL "" FORCE)
else()
    unset(BUILD_STATIC_LIBS CACHE)
endif()

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

add_library(imgui_node_editor STATIC
    ${CMAKE_CURRENT_LIST_DIR}/imgui-node-editor/imgui_node_editor.cpp
    ${CMAKE_CURRENT_LIST_DIR}/imgui-node-editor/imgui_node_editor_api.cpp
    ${CMAKE_CURRENT_LIST_DIR}/imgui-node-editor/imgui_canvas.cpp
    ${CMAKE_CURRENT_LIST_DIR}/imgui-node-editor/crude_json.cpp
)

target_include_directories(imgui_node_editor PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/imgui-node-editor
)

target_link_libraries(imgui_node_editor PUBLIC imgui)
