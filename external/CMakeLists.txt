add_subdirectory(json)
add_subdirectory(glfw)

add_library(stb INTERFACE)

target_include_directories(stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)

enable_language(C)
add_library(volk STATIC volk/volk.c)
target_include_directories(volk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/volk)