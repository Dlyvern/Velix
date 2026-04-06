cmake_minimum_required(VERSION 3.16)
project(VelixCore LANGUAGES CXX)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(INCLUDE_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(SRC_DIR src)

file(GLOB_RECURSE PROJECT_HEADERS CONFIGURE_DEPENDS ${INCLUDE_DIR}/*.h ${INCLUDE_DIR}/*.hpp)
file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS ${SRC_DIR}/*.cpp)

find_package(Vulkan REQUIRED)

add_library(${PROJECT_NAME} SHARED
        ${PROJECT_SOURCES}
        ${PROJECT_HEADERS}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:DEBUG_BUILD>
    $<$<CONFIG:Release>:RELEASE_BUILD>
)

# Export volk function-pointer globals so VelixEngine.so and plugins can resolve them.
target_compile_definitions(volk PUBLIC VOLK_DEFAULT_VISIBILITY)

# set_target_properties(${PROJECT_NAME} PROPERTIES
#     IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/bin/libVelixCore.a"
#     INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/include"
# )

target_include_directories(${PROJECT_NAME} PUBLIC
        ${INCLUDE_DIR}
        ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        glfw
        volk_headers   # propagate volk include dirs, but not the static lib
        stb
        glm::glm
        VulkanMemoryAllocator
    PRIVATE
        volk           # keep the actual function-pointer globals inside VelixCore.so only
)

install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${INCLUDE_DIR}/ DESTINATION include)

if (WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif()
if (UNIX)
    target_compile_definitions(${PROJECT_NAME} PRIVATE VK_USE_PLATFORM_XLIB_KHR)
endif()
