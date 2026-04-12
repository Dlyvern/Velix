cmake_minimum_required(VERSION 3.16)
project(VelixEngine)

set(CMAKE_CXX_STANDARD 20)
set(INCLUDE_DIR include)
set(SRC_DIR src)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
file(GLOB_RECURSE PROJECT_HEADERS CONFIGURE_DEPENDS ${INCLUDE_DIR}/*.h ${INCLUDE_DIR}/*.hpp)
file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS ${SRC_DIR}/*.cpp)

add_library(${PROJECT_NAME} SHARED
        ${PROJECT_SOURCES}
        ${PROJECT_HEADERS}
)

target_include_directories(${PROJECT_NAME} PUBLIC
        ${INCLUDE_DIR}
)

if(TARGET OpenEXR::OpenEXR)
    target_link_libraries(${PROJECT_NAME} PUBLIC OpenEXR::OpenEXR)
    target_compile_definitions(${PROJECT_NAME} PUBLIC ELIX_HAS_OPENEXR=1)
elseif(TARGET OpenEXR)
    target_link_libraries(${PROJECT_NAME} PUBLIC OpenEXR)
    target_compile_definitions(${PROJECT_NAME} PUBLIC ELIX_HAS_OPENEXR=1)
else()
    message(WARNING "OpenEXR target not found. .exr texture loading will be disabled.")
endif()

if(TARGET LZ4::lz4)
    target_link_libraries(${PROJECT_NAME} PUBLIC LZ4::lz4)
    target_compile_definitions(${PROJECT_NAME} PUBLIC ELIX_HAS_LZ4=1)
else()
    message(WARNING "LZ4 target not found. LZ4 compression for ElixBundle will be disabled.")
endif()

if(TARGET ZLIB::ZLIB)
    target_link_libraries(${PROJECT_NAME} PUBLIC ZLIB::ZLIB)
    target_compile_definitions(${PROJECT_NAME} PUBLIC ELIX_HAS_ZLIB=1)
elseif(TARGET zlib)
    target_link_libraries(${PROJECT_NAME} PUBLIC zlib)
    target_compile_definitions(${PROJECT_NAME} PUBLIC ELIX_HAS_ZLIB=1)
else()
    find_package(ZLIB QUIET)
    if(ZLIB_FOUND)
        target_link_libraries(${PROJECT_NAME} PUBLIC ZLIB::ZLIB)
        target_compile_definitions(${PROJECT_NAME} PUBLIC ELIX_HAS_ZLIB=1)
    else()
        message(WARNING "ZLIB not found. Texture asset byte compression will be disabled.")
    endif()
endif()

target_link_libraries(${PROJECT_NAME}
        PUBLIC
        nlohmann_json
        tinyobjloader
        fbx_sdk
        VelixCore
        spirv-cross-core
        spirv-cross-cpp
        spirv-cross-glsl
        shaderc
        physx
        fmod
        freetype
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PUBLIC dbghelp)
    # On Windows, volk's global function-pointer table lives inside VelixCore.dll.
    # Other DLLs cannot import data symbols from it without __declspec(dllimport),
    # which volk's extern declarations don't have.  Give Engine its own private volk
    # copy and populate it after VulkanContext::create() finishes.
    target_link_libraries(${PROJECT_NAME} PRIVATE volk)
endif()

if(UNIX)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${INCLUDE_DIR}/ DESTINATION include)
